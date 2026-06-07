import 'dart:typed_data';

class SensorData {
  final int fsrRaw;
  final double temperature;
  final double humidity;
  final int batteryMv;

  // 分压配置（与固件 fsr402_pcb_cfg 对齐）
  static const int _rFixed = 30000;  // Ω, PCB 版 30kΩ
  static const int _vRefMv = 3300;   // mV
  static const double _forceK = 5000.0;
  static const int _adcMax = 4095;
  static const int _adcRangeMv = 3100; // 12dB attenuation

  SensorData({
    required this.fsrRaw,
    required this.temperature,
    required this.humidity,
    required this.batteryMv,
  });

  // ── 按压判断 ──
  bool get isPressed => fsrRaw > 50;

  // ── 估算电压 (mV) — 同固件无校准回退逻辑 ──
  int get voltageMv => fsrRaw * _adcRangeMv ~/ _adcMax;

  // ── 估算 FSR 电阻 (Ω) ──
  int get resistanceOhm {
    final v = voltageMv;
    if (v <= 0) return 0x7FFFFFFF; // 开路
    if (v >= _vRefMv) return 0;     // 短路
    return (_rFixed * (_vRefMv - v) ~/ v);
  }

  // ── 估算压力 (N) ──
  double get forceN {
    final r = resistanceOhm;
    if (r == 0 || r >= 1000000) return 0.0;
    return _forceK / r;
  }

  // ── ADC 是否经硬件校准 — BLE 数据包不含此信息，默认 false ──
  bool get calibrated => false;

  // ── 电池电量百分比 ──
  String get batteryPercent {
    const full = 4200;
    const empty = 3000;
    final pct = ((batteryMv - empty) / (full - empty) * 100).clamp(0, 100);
    return '${pct.toInt()}%';
  }

  double get batteryPercentRaw {
    const full = 4200;
    const empty = 3000;
    return ((batteryMv - empty) / (full - empty) * 100).clamp(0, 100);
  }

  // ── 合并更新 ──
  SensorData copyWith({
    int? fsrRaw,
    double? temperature,
    double? humidity,
    int? batteryMv,
  }) {
    return SensorData(
      fsrRaw: fsrRaw ?? this.fsrRaw,
      temperature: temperature ?? this.temperature,
      humidity: humidity ?? this.humidity,
      batteryMv: batteryMv ?? this.batteryMv,
    );
  }

  /// 全数据包解析（8 字节, 旧格式兼容）
  static SensorData fromBytes(Uint8List data) {
    final b = ByteData.sublistView(data);
    return SensorData(
      fsrRaw: b.getUint16(0, Endian.little),
      temperature: b.getInt16(2, Endian.little) / 100.0,
      humidity: b.getUint16(4, Endian.little) / 10.0,
      batteryMv: b.getUint16(6, Endian.little),
    );
  }

  /// 0xFF01 数据包解析（6 字节: temp + humidity + battery）
  static SensorData fromDataPacket(Uint8List data) {
    final b = ByteData.sublistView(data);
    return SensorData(
      fsrRaw: 0,
      temperature: b.getInt16(0, Endian.little) / 100.0,
      humidity: b.getUint16(2, Endian.little) / 10.0,
      batteryMv: b.getUint16(4, Endian.little),
    );
  }

  /// 0xFF02 FSR 数据包解析（2 字节: fsr_raw）
  static SensorData fromFsrPacket(Uint8List data) {
    final b = ByteData.sublistView(data);
    return SensorData(
      fsrRaw: b.getUint16(0, Endian.little),
      temperature: 0,
      humidity: 0,
      batteryMv: 0,
    );
  }

  @override
  String toString() {
    return 'FSR=$fsrRaw T=${temperature.toStringAsFixed(1)}°C '
        'RH=${humidity.toStringAsFixed(1)}% Bat=${batteryMv}mV '
        'F=${forceN.toStringAsFixed(3)}N';
  }
}
