import 'dart:async';
import 'dart:typed_data';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/sensor_data.dart';

class BleService {
  static const String serviceUuid = 'b6b6ffff-9cf3-4a52-9f7b-6eb7b6cbf6b3';
  static const String characteristicUuid = 'b6b6ff01-9cf3-4a52-9f7b-6eb7b6cbf6b3';
  static const String characteristicFsrUuid = 'b6b6ff02-9cf3-4a52-9f7b-6eb7b6cbf6b3';
  static const String deviceName = 'FlexSense';
  static const _uuidPrefix = 'b6b6ffff';

  BluetoothDevice? _device;
  StreamSubscription<List<int>>? _notifySubscription;
  StreamSubscription<List<int>>? _notifyFsrSubscription;
  StreamController<SensorData>? _sensorDataController;
  StreamSubscription<BluetoothConnectionState>? _connStateSub;
  bool _connected = false;
  bool _notifySetupDone = false;

  /// 合并两个特性的数据
  SensorData? _mergedData;

  /// 调试日志缓冲区
  final List<String> _debugLog = [];
  void _log(String msg) {
    final t = DateTime.now();
    final ts = '${t.hour.toString().padLeft(2, '0')}:${t.minute.toString().padLeft(2, '0')}:${t.second.toString().padLeft(2, '0')}.${t.millisecond.toString().padLeft(3, '0')}';
    final line = '[$ts] $msg';
    _debugLog.insert(0, line);
    if (_debugLog.length > 200) _debugLog.removeLast();
    print(line);
  }
  List<String> get debugLog => List.unmodifiable(_debugLog);

  BluetoothDevice? get device => _device;
  bool get isConnected => _connected;
  Stream<SensorData>? get sensorDataStream => _sensorDataController?.stream;

  /// 连接状态流（供外部监听）
  Stream<BluetoothConnectionState>? get connectionState =>
      _device?.connectionState;

  /// 判断设备是否 FlexSense
  static bool isFlexSense(ScanResult r) {
    final name = r.advertisementData.advName;
    if (name == 'FlexSense') return true;

    for (final uuid in r.advertisementData.serviceUuids) {
      if (uuid.str.toLowerCase().startsWith(_uuidPrefix)) return true;
    }
    return false;
  }

  /// 启动扫描
  Future<bool> startScan({Duration timeout = const Duration(seconds: 10)}) async {
    try {
      await FlutterBluePlus.startScan(
        timeout: timeout,
        androidUsesFineLocation: true,
      );
      return true;
    } catch (e) {
      return false;
    }
  }

  Stream<List<ScanResult>> get scanResults => FlutterBluePlus.scanResults;

  void stopScan() {
    FlutterBluePlus.stopScan();
  }

  Future<bool> connect(BluetoothDevice device) async {
    try {
      if (_device != null) {
        await _device!.disconnect();
        _device = null;
      }
      try { await device.disconnect(); } catch (_) {}

      _log('正在连接 ${device.remoteId} (mtu:256)...');
      _connStateSub = device.connectionState.listen((state) {
        final on = state == BluetoothConnectionState.connected;
        _log('连接状态变化: ${state.toString()} -> connected=$on');
        _connected = on;
      });

      await device.connect(mtu: 256);
      _device = device;

      _log('连接完成，执行 service discovery...');
      try {
        await _device!.discoverServices(
          subscribeToServicesChanged: false,
        ).timeout(const Duration(seconds: 8));
        _log('discovery 成功，${_device!.servicesList.length} 个服务');
      } catch (e) {
        _log('discovery 失败: $e');
      }

      _sensorDataController = StreamController<SensorData>.broadcast();
      _mergedData = null;
      _log('连接就绪');
      return true;
    } catch (e) {
      _log('连接失败: $e');
      _connStateSub?.cancel();
      _connStateSub = null;
      _connected = false;
      return false;
    }
  }

  Future<void> disconnect() async {
    _log('断开连接...');
    _connStateSub?.cancel();
    _connStateSub = null;
    _notifySubscription?.cancel();
    _notifySubscription = null;
    _notifyFsrSubscription?.cancel();
    _notifyFsrSubscription = null;
    await _sensorDataController?.close();
    _sensorDataController = null;
    _mergedData = null;
    _connected = false;
    _notifySetupDone = false;
    try {
      await _device?.disconnect();
    } catch (_) {}
    _device = null;
    _log('已断开');
  }

  /// 读取一次传感器数据
  Future<SensorData?> readOnce() async {
    if (_device == null) {
      _log('readOnce: _device 为空');
      return null;
    }

    try {
      var services = _device!.servicesList;
      if (services.isEmpty) {
        try {
          services = await _device!.discoverServices(
            subscribeToServicesChanged: false,
          ).timeout(const Duration(seconds: 8));
        } on TimeoutException {
          _log('readOnce: discoverServices 超时');
          return null;
        }
      }

      // 找到目标服务
      BluetoothService? targetSvc;
      for (final svc in services) {
        if (svc.uuid.str128.toLowerCase() == serviceUuid.toLowerCase()) {
          targetSvc = svc;
          break;
        }
      }
      if (targetSvc == null) {
        _log('readOnce: 未找到目标服务');
        return null;
      }

      // 读取 0xFF01 (温湿度+电池) 和 0xFF02 (FSR)
      SensorData? dataPart;
      int? fsrRaw;

      for (final chr in targetSvc.characteristics) {
        final chrUuid = chr.uuid.str128.toLowerCase();

        if (chrUuid == characteristicUuid.toLowerCase()) {
          _log('readOnce: 读取 0xFF01...');
          try {
            final raw = await chr.read();
            if (raw.length >= 6) {
              dataPart = SensorData.fromDataPacket(Uint8List.fromList(raw));
              _log('readOnce: 0xFF01 OK T=${dataPart.temperature} RH=${dataPart.humidity} Bat=${dataPart.batteryMv}');
            }
          } catch (e) {
            _log('readOnce: 0xFF01 读取失败: $e');
          }
        }

        if (chrUuid == characteristicFsrUuid.toLowerCase()) {
          _log('readOnce: 读取 0xFF02...');
          try {
            final raw = await chr.read();
            if (raw.length >= 2) {
              final fsr = SensorData.fromFsrPacket(Uint8List.fromList(raw));
              fsrRaw = fsr.fsrRaw;
              _log('readOnce: 0xFF02 OK FSR=$fsrRaw');
            }
          } catch (e) {
            _log('readOnce: 0xFF02 读取失败: $e');
          }
        }
      }

      if (dataPart != null || fsrRaw != null) {
        final merged = SensorData(
          fsrRaw: fsrRaw ?? 0,
          temperature: dataPart?.temperature ?? 0,
          humidity: dataPart?.humidity ?? 0,
          batteryMv: dataPart?.batteryMv ?? 0,
          lowPower: dataPart?.lowPower ?? false,
        );
        _mergedData = merged;
        _sensorDataController?.add(merged);
        _log('readOnce: ✓ 合并完成 FSR=${merged.fsrRaw} T=${merged.temperature}');
        return merged;
      }

      _log('readOnce: 未读到任何数据');
    } catch (e) {
      _log('readOnce 异常: $e');
    }
    return null;
  }

  /// 设置通知订阅（订阅两个特性）
  Future<bool> setupNotifications() async {
    if (_device == null) {
      _log('setupNotifications: _device 为空');
      return false;
    }
    if (_notifySetupDone) {
      _log('setupNotifications: 已设置过');
      return true;
    }

    try {
      final services = _device!.servicesList;

      BluetoothService? targetSvc;
      for (final svc in services) {
        if (svc.uuid.str128.toLowerCase() == serviceUuid.toLowerCase()) {
          targetSvc = svc;
          break;
        }
      }
      if (targetSvc == null) {
        _log('setupNotifications: 未找到目标服务');
        return false;
      }

      bool dataOk = false;
      bool fsrOk = false;

      for (final chr in targetSvc.characteristics) {
        final chrUuid = chr.uuid.str128.toLowerCase();

        // 0xFF01: 温湿度+电池
        if (chrUuid == characteristicUuid.toLowerCase()) {
          _log('setupNotifications: 设置 0xFF01 通知...');
          _notifySubscription = chr.onValueReceived.listen((data) {
            if (data.length >= 6) {
              final pkt = SensorData.fromDataPacket(Uint8List.fromList(data));
              _mergedData = _mergedData?.copyWith(
                temperature: pkt.temperature,
                humidity: pkt.humidity,
                batteryMv: pkt.batteryMv,
                lowPower: pkt.lowPower,
              ) ?? pkt;
              if (_mergedData != null) {
                _sensorDataController?.add(_mergedData!);
              }
            }
          });
          await chr.setNotifyValue(true);
          dataOk = true;
          _log('setupNotifications: 0xFF01 通知设置成功');
        }

        // 0xFF02: FSR 压力
        if (chrUuid == characteristicFsrUuid.toLowerCase()) {
          _log('setupNotifications: 设置 0xFF02 通知...');
          _notifyFsrSubscription = chr.onValueReceived.listen((data) {
            if (data.length >= 2) {
              final pkt = SensorData.fromFsrPacket(Uint8List.fromList(data));
              _mergedData = _mergedData?.copyWith(
                fsrRaw: pkt.fsrRaw,
              ) ?? pkt;
              if (_mergedData != null) {
                _sensorDataController?.add(_mergedData!);
              }
            }
          });
          await chr.setNotifyValue(true);
          fsrOk = true;
          _log('setupNotifications: 0xFF02 通知设置成功');
        }
      }

      if (dataOk && fsrOk) {
        _notifySetupDone = true;
        _log('setupNotifications: ✓ 全部通知设置完成');
        return true;
      } else {
        _log('setupNotifications: 部分失败 data=$dataOk fsr=$fsrOk');
        return false;
      }
    } catch (e) {
      _log('setupNotifications 异常: $e');
      return false;
    }
  }

  void dispose() {
    _log('BleService dispose');
    stopScan();
    _connStateSub?.cancel();
    _notifySubscription?.cancel();
    _notifyFsrSubscription?.cancel();
    _sensorDataController?.close();
  }

  void clearDebugLog() => _debugLog.clear();
}
