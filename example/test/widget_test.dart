import 'package:flutter_test/flutter_test.dart';

import 'package:pocket25/main.dart';

void main() {
  testWidgets('Pocket25 app smoke test', (WidgetTester tester) async {
    // Build our app and trigger a frame.
    await tester.pumpWidget(const Pocket25App());

    // Verify that the app starts with the Scanner screen
    expect(find.text('Scanner'), findsOneWidget);
    expect(find.text('Log'), findsOneWidget);
    expect(find.text('Settings'), findsOneWidget);
  });
}
