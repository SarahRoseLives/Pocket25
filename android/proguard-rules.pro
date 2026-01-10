# Keep DSD Flutter Plugin
-keep class com.example.dsd_flutter.** { *; }

# Keep native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep Flutter plugin registration
-keep class io.flutter.embedding.engine.** { *; }
-keep class io.flutter.plugin.** { *; }
