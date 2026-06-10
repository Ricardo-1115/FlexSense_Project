import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/sensor_data.dart';
import '../services/ble_service.dart';
import '../widgets/sensor_card.dart';
import '../widgets/data_chart.dart';
import '../widgets/pressure_gauge.dart';
import 'scan_screen.dart';
import 'debug_screen.dart';

class DashboardScreen extends StatefulWidget {
  final BleService bleService;
  const DashboardScreen({super.key, required this.bleService});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  SensorData? _latest;
  final List<SensorData> _history = [];
  StreamSubscription<SensorData>? _sub;
  StreamSubscription<BluetoothConnectionState>? _connSub;
  bool _connected = false;
  bool _showCharts = true;

  static const int _maxHistory = 1000;

  @override
  void initState() {
    super.initState();
    _connected = widget.bleService.isConnected;
    _sub = widget.bleService.sensorDataStream?.listen(_onData);
    _connSub = widget.bleService.connectionState?.listen((state) {
      if (!mounted) return;
      final connected = state == BluetoothConnectionState.connected;
      if (connected != _connected) {
        setState(() => _connected = connected);
        if (!connected) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('连接已断开'), duration: Duration(seconds: 3)),
          );
        }
      }
    });
    _initSensor();
  }

  @override
  void dispose() {
    _sub?.cancel();
    _connSub?.cancel();
    _pollTimer?.cancel();
    super.dispose();
  }

  Timer? _pollTimer;

  Future<void> _initSensor() async {
    // 1. 先读一次（触发 discovery），拿到初始数据
    await _readOnce();

    // 2. 尝试设置通知（如果成功，后续数据自动推送）
    final ok = await widget.bleService.setupNotifications();
    if (ok && mounted) {
      return;
    }

    // 3. 通知失败，轮询兜底
    if (mounted) {
      if (!ok) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('通知订阅失败，使用轮询模式'), duration: Duration(seconds: 2)),
        );
      }
      _pollTimer = Timer.periodic(const Duration(seconds: 2), (_) {
        _readOnce();
      });
    }
  }

  void _onData(SensorData data) {
    if (!mounted) return;
    setState(() {
      _latest = data;
      _history.add(data);
      if (_history.length > _maxHistory) {
        _history.removeAt(0);
      }
    });
  }

  Future<void> _readOnce() async {
    final data = await widget.bleService.readOnce();
    if (data != null && mounted) {
      setState(() => _latest = data);
    }
  }

  Future<void> _disconnect() async {
    await widget.bleService.disconnect();
    if (!mounted) return;
    Navigator.pushReplacement(
      context,
      MaterialPageRoute(
        builder: (_) => ScanScreen(bleService: widget.bleService),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('FlexSense'),
        centerTitle: true,
        actions: [
          IconButton(
            icon: Icon(_showCharts ? Icons.show_chart : Icons.hide_source),
            tooltip: '波形开关',
            onPressed: () => setState(() => _showCharts = !_showCharts),
          ),
          IconButton(
            icon: const Icon(Icons.bug_report),
            tooltip: '原始数据',
            onPressed: () => Navigator.push(
              context,
              MaterialPageRoute(
                builder: (_) => DebugScreen(bleService: widget.bleService),
              ),
            ),
          ),
          IconButton(
            icon: const Icon(Icons.bluetooth_disabled),
            tooltip: '断开',
            onPressed: _disconnect,
          ),
        ],
      ),
      body: SafeArea(
        child: RefreshIndicator(
          onRefresh: _readOnce,
          child: ListView(
            padding: const EdgeInsets.fromLTRB(12, 8, 12, 24),
            children: [
              // ── 连接状态 ──
              _ConnectionBanner(connected: _connected),
              const SizedBox(height: 8),

              // ── 低电量警告 ──
              if (_latest != null && _latest!.lowBattery && !_latest!.lowPower)
                _LowBatteryBanner(),
              if (_latest != null && _latest!.lowBattery && !_latest!.lowPower)
                const SizedBox(height: 8),

              // ── 低功耗模式警告 ──
              if (_latest != null && _latest!.lowPower)
                _LowPowerBanner(),
              if (_latest != null && _latest!.lowPower)
                const SizedBox(height: 8),

              // ── 概览磁贴 ──
              if (_latest != null) ...[
                _SummaryRow(latest: _latest!),
                const SizedBox(height: 12),

                // ── 压力仪表盘 (圆形进度表) ──
                SensorCard(
                  title: 'FSR402 压力',
                  icon: Icons.compress,
                  child: PressureGauge(
                    adcRaw: _latest!.fsrRaw,
                    forceN: _latest!.forceN,
                    isPressed: _latest!.isPressed,
                    calibrated: _latest!.calibrated,
                  ),
                ),
                const SizedBox(height: 12),

                // ── 温度波形图 ──
                if (_showCharts)
                  SensorCard(
                    title: '温度实时曲线',
                    icon: Icons.thermostat,
                    child: SizedBox(
                      height: 160,
                      child: DataChart(
                        data: _history.map((d) => d.temperature).toList(),
                        color: Colors.orange,
                        label: '温度',
                        unit: '°C',
                      ),
                    ),
                  ),
                const SizedBox(height: 8),

                // ── 湿度波形图 ──
                if (_showCharts)
                  SensorCard(
                    title: '湿度实时曲线',
                    icon: Icons.water_drop,
                    child: SizedBox(
                      height: 160,
                      child: DataChart(
                        data: _history.map((d) => d.humidity).toList(),
                        color: Colors.blue,
                        label: '湿度',
                        unit: '%RH',
                      ),
                    ),
                  ),
                const SizedBox(height: 8),

                // ── 电池详情 ──
                SensorCard(
                  title: '电池',
                  icon: Icons.battery_std,
                  child: _BatteryDetail(mv: _latest!.batteryMv),
                ),
              ],

              if (_latest == null)
                const Padding(
                  padding: EdgeInsets.only(top: 80),
                  child: Center(
                    child: Column(
                      children: [
                        CircularProgressIndicator(),
                        SizedBox(height: 16),
                        Text('等待传感器数据...'),
                      ],
                    ),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

// ── 连接状态横幅 ──
class _ConnectionBanner extends StatelessWidget {
  final bool connected;
  const _ConnectionBanner({required this.connected});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: connected
            ? theme.colorScheme.primaryContainer
            : theme.colorScheme.errorContainer,
        borderRadius: BorderRadius.circular(8),
      ),
      child: Row(
        children: [
          Icon(
            connected ? Icons.bluetooth_connected : Icons.bluetooth_disabled,
            size: 18,
          ),
          const SizedBox(width: 8),
          Text(
            connected ? 'FlexSense 已连接' : '未连接',
            style: TextStyle(fontSize: 13),
          ),
          const Spacer(),
          if (connected)
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                SizedBox(
                  width: 8, height: 8,
                  child: CircularProgressIndicator(strokeWidth: 2),
                ),
                const SizedBox(width: 6),
                Text('实时', style: TextStyle(fontSize: 12)),
              ],
            ),
        ],
      ),
    );
  }
}

// ── 低电量警告横幅 ──
class _LowBatteryBanner extends StatelessWidget {
  const _LowBatteryBanner();

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
      decoration: BoxDecoration(
        color: Colors.yellow.shade100,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.yellow.shade600),
      ),
      child: Row(
        children: [
          Icon(Icons.battery_alert, size: 18, color: Colors.yellow.shade800),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              '电池电量低 — 请及时充电',
              style: TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w600,
                color: Colors.yellow.shade900,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

// ── 低功耗模式警告横幅 ──
class _LowPowerBanner extends StatelessWidget {
  const _LowPowerBanner();

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
      decoration: BoxDecoration(
        color: Colors.orange.shade100,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.orange.shade400),
      ),
      child: Row(
        children: [
          Icon(Icons.power_off, size: 18, color: Colors.orange.shade800),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              '低功耗模式 — 电池电量低，设备已进入睡眠',
              style: TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w600,
                color: Colors.orange.shade900,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

// ── 4 个概览磁贴 ──
class _SummaryRow extends StatelessWidget {
  final SensorData latest;
  const _SummaryRow({required this.latest});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Row(
      children: [
        Expanded(child: _MiniTile(
          icon: Icons.compress, label: '压力',
          value: latest.isPressed ? '${(latest.fsrRaw / 4095 * 100).toInt()}%' : '--',
          color: latest.isPressed ? theme.colorScheme.tertiary : theme.colorScheme.outline,
        )),
        const SizedBox(width: 6),
        Expanded(child: _MiniTile(
          icon: Icons.thermostat, label: '温度',
          value: '${latest.temperature.toStringAsFixed(1)}°',
          color: Colors.orange,
        )),
        const SizedBox(width: 6),
        Expanded(child: _MiniTile(
          icon: Icons.water_drop, label: '湿度',
          value: '${latest.humidity.toStringAsFixed(1)}%',
          color: Colors.blue,
        )),
        const SizedBox(width: 6),
        Expanded(child: _MiniTile(
          icon: Icons.battery_std, label: '电池',
          value: latest.batteryPercent,
          color: latest.batteryMv > 3500
              ? Colors.green
              : latest.batteryMv > 3100 ? Colors.orange : Colors.red,
        )),
      ],
    );
  }
}

class _MiniTile extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  final Color color;

  const _MiniTile({
    required this.icon, required this.label,
    required this.value, required this.color,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 8),
      decoration: BoxDecoration(
        color: theme.colorScheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(8),
      ),
      child: Column(
        children: [
          Icon(icon, color: color, size: 18),
          const SizedBox(height: 2),
          Text(value,
            style: TextStyle(
              fontSize: 15, fontWeight: FontWeight.bold, color: color,
            ),
          ),
          Text(label, style: TextStyle(fontSize: 10, color: theme.colorScheme.outline)),
        ],
      ),
    );
  }
}

// ── 电池详情 ──
class _BatteryDetail extends StatelessWidget {
  final int mv;
  const _BatteryDetail({required this.mv});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final pct = ((mv - 3000) / (4200 - 3000) * 100).clamp(0, 100);
    final color = mv > 3500 ? Colors.green : mv > 3100 ? Colors.orange : Colors.red;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          crossAxisAlignment: CrossAxisAlignment.end,
          children: [
            Text('$mv', style: theme.textTheme.headlineMedium?.copyWith(
              fontWeight: FontWeight.bold, color: color,
            )),
            const SizedBox(width: 4),
            Padding(
              padding: const EdgeInsets.only(bottom: 6),
              child: Text('mV', style: TextStyle(color: theme.colorScheme.outline)),
            ),
            const Spacer(),
            Padding(
              padding: const EdgeInsets.only(bottom: 6),
              child: Text('${pct.toInt()}%',
                style: TextStyle(fontWeight: FontWeight.w600, color: color)),
            ),
          ],
        ),
        const SizedBox(height: 6),
        ClipRRect(
          borderRadius: BorderRadius.circular(4),
          child: LinearProgressIndicator(
            value: pct / 100,
            minHeight: 8,
            backgroundColor: theme.colorScheme.surfaceContainerHighest,
            color: color,
          ),
        ),
        const SizedBox(height: 6),
        Row(
          children: [
            Text('空 3.0V', style: TextStyle(fontSize: 11, color: theme.colorScheme.outline)),
            const Spacer(),
            Text('满 4.2V', style: TextStyle(fontSize: 11, color: theme.colorScheme.outline)),
          ],
        ),
      ],
    );
  }
}
