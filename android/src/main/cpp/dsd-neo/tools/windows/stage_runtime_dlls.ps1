[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$TargetBinary,

    [Parameter(Mandatory = $true)]
    [string]$DestinationDir,

    [Parameter(Mandatory = $true)]
    [string[]]$SearchDirs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-VsInstallationPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path $vswhere)) {
        return $null
    }

    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $null
    }
    if ([string]::IsNullOrWhiteSpace($path)) {
        return $null
    }
    return $path.Trim()
}

function Get-DumpbinPath {
    $cmd = Get-Command 'dumpbin' -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Path
    }

    $vsPath = Get-VsInstallationPath
    if (-not $vsPath) {
        return $null
    }

    $vcToolsRoot = Join-Path $vsPath 'VC\Tools\MSVC'
    if (!(Test-Path $vcToolsRoot)) {
        return $null
    }

    $vcTools = Get-ChildItem -Path $vcToolsRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object -Property Name -Descending |
        Select-Object -First 1
    if (-not $vcTools) {
        return $null
    }

    $dumpbin = Join-Path $vcTools.FullName 'bin\Hostx64\x64\dumpbin.exe'
    if (Test-Path $dumpbin) {
        return $dumpbin
    }

    return $null
}

function Get-ObjdumpPath {
    $cmd = Get-Command 'llvm-objdump' -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Path
    }

    $cmd = Get-Command 'objdump' -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Path
    }

    $candidates = @(
        'C:\msys64\mingw64\bin\objdump.exe',
        'C:\msys64\ucrt64\bin\objdump.exe',
        'C:\msys64\usr\bin\objdump.exe'
    )

    $runnerTemp = $env:RUNNER_TEMP
    if ($runnerTemp) {
        $candidates = @(
            (Join-Path $runnerTemp 'msys64\mingw64\bin\objdump.exe'),
            (Join-Path $runnerTemp 'msys64\ucrt64\bin\objdump.exe'),
            (Join-Path $runnerTemp 'msys64\usr\bin\objdump.exe')
        ) + $candidates
    }

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Get-DependentDllNames {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BinaryPath,

        [Parameter(Mandatory = $false)]
        [string]$DumpbinPath,

        [Parameter(Mandatory = $false)]
        [string]$ObjdumpPath
    )

    $names = @()

    if ($DumpbinPath) {
        $lines = & $DumpbinPath /DEPENDENTS $BinaryPath 2>$null
        $names = $lines |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ -match '^[A-Za-z0-9_.+-]+\.dll$' }
    } elseif ($ObjdumpPath) {
        $lines = & $ObjdumpPath -p $BinaryPath 2>$null
        $names = $lines |
            ForEach-Object { $_.ToString() } |
            ForEach-Object {
                if ($_ -match 'DLL Name:\s*(.+\.dll)\s*$') { $Matches[1] }
            } |
            Where-Object { $_ }
    } else {
        throw 'Neither dumpbin nor objdump could be found; cannot resolve DLL dependencies.'
    }

    return $names | Sort-Object -Unique
}

function Get-VsRedistDirs {
    $vsPath = Get-VsInstallationPath
    if (-not $vsPath) {
        return @()
    }

    $redistRoot = Join-Path $vsPath 'VC\Redist\MSVC'
    if (!(Test-Path $redistRoot)) {
        return @()
    }

    $dirs = @()
    $redist = Get-ChildItem -Path $redistRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object -Property Name -Descending |
        Select-Object -First 3
    foreach ($r in $redist) {
        foreach ($cand in @(
            (Join-Path $r.FullName 'x64\Microsoft.VC143.CRT'),
            (Join-Path $r.FullName 'x64\Microsoft.VC142.CRT'),
            (Join-Path $r.FullName 'x64\Microsoft.VC141.CRT')
        )) {
            if (Test-Path $cand) {
                $dirs += $cand
            }
        }
    }
    return $dirs | Select-Object -Unique
}

if (!(Test-Path $TargetBinary)) {
    throw "TargetBinary not found: $TargetBinary"
}

New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null

$destinationFull = (Resolve-Path $DestinationDir).Path
$targetFull = (Resolve-Path $TargetBinary).Path

$dumpbin = Get-DumpbinPath
$objdump = Get-ObjdumpPath

$system32 = Join-Path $env:SystemRoot 'System32'
if (Test-Path $system32) {
    $system32 = (Resolve-Path $system32).Path
}

$normalizedDirs = @()
$normalizedDirs += $destinationFull
$normalizedDirs += $SearchDirs
$normalizedDirs += (Get-VsRedistDirs)
$runnerTemp = $env:RUNNER_TEMP
if ($runnerTemp) {
    $normalizedDirs += @(
        (Join-Path $runnerTemp 'msys64\mingw64\bin'),
        (Join-Path $runnerTemp 'msys64\ucrt64\bin')
    )
}
$normalizedDirs += @(
    'C:\msys64\mingw64\bin',
    'C:\msys64\ucrt64\bin'
)
if ($system32) {
    $normalizedDirs += $system32
}

$normalizedDirs = $normalizedDirs |
    Where-Object { $_ -and -not [string]::IsNullOrWhiteSpace($_) } |
    ForEach-Object { $_.Trim().TrimEnd('\', '/') } |
    Select-Object -Unique

$ignoreNameRegex = '^(?i)(api-ms-win-|ext-ms-).+\.dll$'
$allowSystemCopyRegex = '^(?i)(vcruntime|msvcp|concrt|vcomp)[0-9]+(_[0-9]+)?\.dll$'

$queue = New-Object 'System.Collections.Generic.Queue[string]'
$visited = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
$missing = New-Object 'System.Collections.Generic.List[string]'

$queue.Enqueue($targetFull)

while ($queue.Count -gt 0) {
    $current = $queue.Dequeue()
    if ($visited.Contains($current)) {
        continue
    }
    $visited.Add($current) | Out-Null

    $deps = Get-DependentDllNames -BinaryPath $current -DumpbinPath $dumpbin -ObjdumpPath $objdump
    foreach ($dll in $deps) {
        if ($dll -match $ignoreNameRegex) {
            continue
        }

        $destDllPath = Join-Path $destinationFull $dll
        if (Test-Path $destDllPath) {
            $queue.Enqueue((Resolve-Path $destDllPath).Path)
            continue
        }

        $sourcePath = $null
        foreach ($dir in $normalizedDirs) {
            $candidate = Join-Path $dir $dll
            if (Test-Path $candidate) {
                $sourcePath = (Resolve-Path $candidate).Path
                break
            }
        }

        if (-not $sourcePath) {
            $missing.Add($dll)
            continue
        }

        $isSystem32 = $false
        if ($system32) {
            $isSystem32 = $sourcePath.StartsWith($system32, [System.StringComparison]::OrdinalIgnoreCase)
        }

        if ($isSystem32 -and ($dll -notmatch $allowSystemCopyRegex)) {
            continue
        }

        Copy-Item -Force $sourcePath $destinationFull
        $queue.Enqueue((Join-Path $destinationFull $dll))
    }
}

if ($missing.Count -gt 0) {
    $unique = $missing | Sort-Object -Unique
    throw ("Missing DLL dependencies (not found in search dirs):`n" + ($unique -join "`n"))
}

$manifest = Join-Path $destinationFull 'dlls-manifest.txt'
Get-ChildItem -Path $destinationFull -Filter '*.dll' -File -ErrorAction SilentlyContinue |
    Sort-Object -Property Name |
    Select-Object -ExpandProperty Name |
    Set-Content -Path $manifest -Encoding UTF8
