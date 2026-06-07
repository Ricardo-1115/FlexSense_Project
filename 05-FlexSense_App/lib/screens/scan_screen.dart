import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../services/ble_service.dart';
import 'dashboard_screen.dart';

class ScanScreen extends StatefulWidget {
  final BleService bleService;
  const ScanScreen({super.key, required this.bleService});

  @override
  State<ScanScreen> createState() => _ScanScreenState();
}

class _ScanScreenState extends State<ScanScreen> {
  StreamSubscription<List<ScanResult>>? _scanSub;
  List<ScanResult> _results = [];
  bool _scanning = false;
  bool _connecting = false;
  bool _bluetoothOn = true;

  @override
  void initState() {
    super.initState();
    _init();
  }

  @override
  void dispose() {
    _scanSub?.cancel();
    widget.bleService.stopScan();
    super.dispose();
  }

  Future<void> _init() async {
    // 等待蓝牙就绪后自动开始扫描
    await _checkBluetooth();
    // 监听后续蓝牙状态变化
    FlutterBluePlus.adapterState.listen((state) {
      if (!mounted) return;
      final on = state == BluetoothAdapterState.on;
      setState(() => _bluetoothOn = on);
      if (on) _startScan();
    });
  }

  Future<void> _checkBluetooth() async {
    try {
      await FlutterBluePlus.turnOn();
    } catch (_) {}
    if (!mounted) return;
    // 检查当前状态
    final state = await FlutterBluePlus.adapterState.first;
    if (!mounted) return;
    _bluetoothOn = state == BluetoothAdapterState.on;
    if (_bluetoothOn) {
      await _startScan();
    }
  }

  Future<void> _startScan() async {
    if (!_bluetoothOn) return;

    setState(() {
      _scanning = true;
      _results.clear();
    });

    // 先取消旧订阅
    _scanSub?.cancel();

    // 启动扫描，等待结果
    final started = await widget.bleService.startScan(timeout: const Duration(seconds: 10));
    if (!mounted) return;

    if (!started) {
      setState(() => _scanning = false);
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('扫描启动失败，请检查蓝牙和权限'), backgroundColor: Colors.red),
      );
      return;
    }

    _scanSub = widget.bleService.scanResults.listen(
      (results) {
        if (!mounted) return;
        setState(() => _results = results);
      },
      onDone: () {
        if (!mounted) return;
        setState(() => _scanning = false);
      },
      onError: (e) {
        if (!mounted) return;
        setState(() => _scanning = false);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('扫描出错: $e'), backgroundColor: Colors.red),
        );
      },
    );
  }

  List<ScanResult> get _sortedResults {
    final flex = <ScanResult>[];
    final others = <ScanResult>[];
    for (final r in _results) {
      if (BleService.isFlexSense(r)) {
        flex.add(r);
      } else {
        others.add(r);
      }
    }
    // 按 RSSI 降序排（信号强的靠前）
    flex.sort((a, b) => b.rssi.compareTo(a.rssi));
    others.sort((a, b) => b.rssi.compareTo(a.rssi));
    return [...flex, ...others];
  }

  Future<void> _connect(ScanResult result) async {
    setState(() => _connecting = true);

    final ok = await widget.bleService.connect(result.device);
    if (!mounted) return;

    setState(() => _connecting = false);

    if (ok) {
      Navigator.pushReplacement(
        context,
        MaterialPageRoute(
          builder: (_) => DashboardScreen(bleService: widget.bleService),
        ),
      );
    } else {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('连接失败，请重试'), backgroundColor: Colors.red),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final sorted = _sortedResults;
    final flexCount = sorted.where((r) => BleService.isFlexSense(r)).length;

    return Scaffold(
      appBar: AppBar(
        title: const Text('FlexSense'),
        centerTitle: true,
        actions: [
          if (_connecting)
            const Padding(
              padding: EdgeInsets.only(right: 16),
              child: SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              ),
            ),
          IconButton(
            icon: Icon(_scanning ? Icons.stop : Icons.refresh),
            onPressed: _scanning ? () => widget.bleService.stopScan() : _startScan,
          ),
        ],
      ),
      body: !_bluetoothOn
          ? Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.bluetooth_disabled, size: 64,
                      color: theme.colorScheme.error),
                  const SizedBox(height: 16),
                  Text('蓝牙未开启',
                      style: TextStyle(color: theme.colorScheme.error, fontSize: 18)),
                  const SizedBox(height: 8),
                  ElevatedButton(
                    onPressed: _checkBluetooth,
                    child: const Text('开启蓝牙'),
                  ),
                ],
              ),
            )
          : Column(
              children: [
                // 状态栏
                Container(
                  width: double.infinity,
                  padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 16),
                  color: theme.colorScheme.primaryContainer,
                  child: Row(
                    children: [
                      Icon(
                        _scanning ? Icons.bluetooth_searching : Icons.bluetooth,
                        size: 18,
                        color: theme.colorScheme.onPrimaryContainer,
                      ),
                      const SizedBox(width: 8),
                      Text(
                        _scanning ? '扫描中...' : '扫描完成',
                        style: TextStyle(color: theme.colorScheme.onPrimaryContainer),
                      ),
                      const Spacer(),
                      Text(
                        '${sorted.length} 台设备 (FlexSense $flexCount)',
                        style: TextStyle(
                          color: theme.colorScheme.onPrimaryContainer,
                          fontSize: 12,
                        ),
                      ),
                    ],
                  ),
                ),
                // 设备列表
                Expanded(
                  child: sorted.isEmpty
                      ? Center(
                          child: Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Icon(Icons.bluetooth_searching, size: 64,
                                  color: theme.colorScheme.outline),
                              const SizedBox(height: 16),
                              Text('扫描附近设备...',
                                  style: TextStyle(color: theme.colorScheme.outline)),
                            ],
                          ),
                        )
                      : ListView.builder(
                          padding: const EdgeInsets.symmetric(vertical: 6),
                          itemCount: sorted.length,
                          itemBuilder: (context, i) {
                            final r = sorted[i];
                            final isFlex = BleService.isFlexSense(r);
                            final name = r.advertisementData.advName.isNotEmpty
                                ? r.advertisementData.advName
                                : r.device.remoteId.str;
                            final addr = r.device.remoteId.str;
                            // RSSI 信号强度指示
                            final rssiPct = ((r.rssi + 100) / 100).clamp(0.0, 1.0);
                            final rssiColor = r.rssi > -50
                                ? Colors.green
                                : r.rssi > -70
                                    ? Colors.orange
                                    : Colors.red;

                            // 分隔标题
                            final showFlexHeader =
                                i == 0 && isFlex && flexCount > 0;
                            final showOtherHeader =
                                !isFlex && i > 0 && flexCount > 0 &&
                                    BleService.isFlexSense(sorted[i - 1]);

                            return Column(
                              children: [
                                if (showFlexHeader)
                                  _SectionHeader(
                                    title: 'FlexSense 设备',
                                    count: flexCount,
                                    color: theme.colorScheme.secondaryContainer,
                                  ),
                                if (showOtherHeader)
                                  _SectionHeader(
                                    title: '其他设备',
                                    count: sorted.length - flexCount,
                                    color: theme.colorScheme.surfaceContainerHighest,
                                  ),
                                Card(
                                  margin: const EdgeInsets.symmetric(
                                      horizontal: 12, vertical: 3),
                                  color: isFlex
                                      ? theme.colorScheme.secondaryContainer
                                      : null,
                                  child: ListTile(
                                    contentPadding: const EdgeInsets.symmetric(
                                        horizontal: 12, vertical: 2),
                                    leading: CircleAvatar(
                                      backgroundColor: isFlex
                                          ? theme.colorScheme.primary
                                          : theme.colorScheme.surfaceContainerHighest,
                                      radius: 18,
                                      child: Icon(
                                        isFlex ? Icons.fitness_center : Icons.devices,
                                        size: 20,
                                        color: isFlex
                                            ? theme.colorScheme.onPrimary
                                            : theme.colorScheme.outline,
                                      ),
                                    ),
                                    title: Text(
                                      name,
                                      style: TextStyle(
                                        fontWeight:
                                            isFlex ? FontWeight.bold : FontWeight.normal,
                                        fontSize: 14,
                                      ),
                                    ),
                                    subtitle: Padding(
                                      padding: const EdgeInsets.only(top: 2),
                                      child: Row(
                                        children: [
                                          // 信号强度指示条
                                          SizedBox(
                                            width: 36,
                                            height: 12,
                                            child: CustomPaint(
                                              painter: _SignalBarsPainter(
                                                level: (rssiPct * 4).round().clamp(1, 4),
                                                color: rssiColor,
                                              ),
                                            ),
                                          ),
                                          const SizedBox(width: 6),
                                          Text('${r.rssi} dBm',
                                              style: const TextStyle(fontSize: 11)),
                                          const SizedBox(width: 8),
                                          Expanded(
                                            child: Text(addr.substring(0, 17),
                                                style: const TextStyle(fontSize: 10),
                                                overflow: TextOverflow.ellipsis),
                                          ),
                                        ],
                                      ),
                                    ),
                                    trailing: SizedBox(
                                      height: 32,
                                      child: ElevatedButton(
                                        onPressed: _connecting
                                            ? null
                                            : () => _connect(r),
                                        style: ElevatedButton.styleFrom(
                                          padding: const EdgeInsets.symmetric(
                                              horizontal: 16),
                                          backgroundColor: isFlex
                                              ? theme.colorScheme.primary
                                              : theme.colorScheme.secondary,
                                        ),
                                        child: Text(
                                          '连接',
                                          style: TextStyle(
                                            fontSize: 13,
                                            color: isFlex
                                                ? theme.colorScheme.onPrimary
                                                : null,
                                          ),
                                        ),
                                      ),
                                    ),
                                  ),
                                ),
                              ],
                            );
                          },
                        ),
                ),
              ],
            ),
    );
  }
}

/// 分组标题
class _SectionHeader extends StatelessWidget {
  final String title;
  final int count;
  final Color color;
  const _SectionHeader({
    required this.title,
    required this.count,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
      child: Text(
        '$title  ($count)',
        style: TextStyle(
          fontSize: 12,
          fontWeight: FontWeight.w600,
          color: Theme.of(context).colorScheme.outline,
        ),
      ),
    );
  }
}

/// 信号强度条形图
class _SignalBarsPainter extends CustomPainter {
  final int level;
  final Color color;
  _SignalBarsPainter({required this.level, required this.color});

  @override
  void paint(Canvas canvas, Size size) {
    final barW = size.width / 5 - 2;
    for (int i = 0; i < 4; i++) {
      final h = size.height * (0.25 + i * 0.22);
      final paint = Paint()
        ..color = i < level ? color : Colors.grey.withValues(alpha: 0.3)
        ..style = PaintingStyle.fill;
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromLTWH(i * (barW + 2), size.height - h, barW, h),
          const Radius.circular(1.5),
        ),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(_SignalBarsPainter old) => old.level != level;
}
