import 'package:flutter_test/flutter_test.dart';
import 'package:flexsense_app/main.dart';

void main() {
  testWidgets('App loads scan screen', (WidgetTester tester) async {
    await tester.pumpWidget(const FlexSenseApp());
    expect(find.text('FlexSense'), findsWidgets);
  });
}
