import 'package:flutter/material.dart';
import 'services/ble_service.dart';
import 'screens/scan_screen.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const FlexSenseApp());
}

class FlexSenseApp extends StatefulWidget {
  const FlexSenseApp({super.key});

  @override
  State<FlexSenseApp> createState() => _FlexSenseAppState();
}

class _FlexSenseAppState extends State<FlexSenseApp> {
  final BleService _bleService = BleService();

  @override
  void dispose() {
    _bleService.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'FlexSense',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorSchemeSeed: Colors.indigo,
        brightness: Brightness.light,
        useMaterial3: true,
      ),
      darkTheme: ThemeData(
        colorSchemeSeed: Colors.indigo,
        brightness: Brightness.dark,
        useMaterial3: true,
      ),
      themeMode: ThemeMode.system,
      home: ScanScreen(bleService: _bleService),
    );
  }
}
