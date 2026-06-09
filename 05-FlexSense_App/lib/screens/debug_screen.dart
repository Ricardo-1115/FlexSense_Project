import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../models/sensor_data.dart';
import '../services/ble_service.dart';

class DebugScreen extends StatefulWidget {
  final BleService bleService;
  const DebugScreen({super.key, required this.bleService});

  @override
  State<DebugScreen> createState() => _DebugScreenState();
}

class _DebugScreenState extends State<DebugScreen>
    with SingleTickerProviderStateMixin {
  StreamSubscription<SensorData>? _sub;
  final List<_LogEntry> _log = [];
  late final TabController _tabCtrl;

  @override
  void initState() {
    super.initState();
    _tabCtrl = TabController(length: 2, vsync: this);
    _sub = widget.bleService.sensorDataStream?.listen(_onData);
    _readOnce();
  }

  @override
  void dispose() {
    _sub?.cancel();
    _tabCtrl.dispose();
    super.dispose();
  }

  void _onData(SensorData data) {
    if (!mounted) return;
    setState(() {
      _log.insert(0, _LogEntry(
        time: DateTime.now(),
        data: data,
      ));
      if (_log.length > 100) _log.removeLast();
    });
  }

  Future<void> _readOnce() async {
    final data = await widget.bleService.readOnce();
    if (data != null && mounted) {
      setState(() {
        _log.insert(0, _LogEntry(time: DateTime.now(), data: data));
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Scaffold(
      appBar: AppBar(
        title: const Text('原始数据'),
        bottom: TabBar(
          controller: _tabCtrl,
          tabs: const [
            Tab(text: '数据'),
            Tab(text: '日志'),
          ],
        ),
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: _readOnce,
          ),
          if (_log.isNotEmpty)
            IconButton(
              icon: const Icon(Icons.delete),
              onPressed: () => setState(() => _log.clear()),
            ),
        ],
      ),
      body: TabBarView(
        controller: _tabCtrl,
        children: [
          // ── Tab 1: 传感器数据 ──
          _buildDataTab(theme),
          // ── Tab 2: 调试日志 ──
          _buildLogTab(theme),
        ],
      ),
    );
  }

  Widget _buildDataTab(ThemeData theme) {
    return Column(
      children: [
        // Packet format info
        Container(
          width: double.infinity,
          padding: const EdgeInsets.all(12),
          color: theme.colorScheme.surfaceContainerHighest,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('数据包格式',
                  style: TextStyle(fontWeight: FontWeight.bold, fontSize: 13)),
              const SizedBox(height: 4),
              Text(
                'Service: b6b6ffff-9cf3-4a52-9f7b-6eb7b6cbf6b3\n'
                '0xFF01:  b6b6ff01-...  Temp+Hum+Bat+Flags  (1s notify)\n'
                '0xFF02:  b6b6ff02-...  FSR pressure        (100ms notify)\n'
                '0xFF01 包 (7字节): T(int16×100) + RH(uint16×10) + Bat(uint16) + Flags(uint8)\n'
                '0xFF02 包 (2字节): FSR_raw(uint16)',
                style: TextStyle(fontSize: 11, fontFamily: 'monospace',
                    color: theme.colorScheme.outline),
              ),
            ],
          ),
        ),
        // Log entries
        Expanded(
          child: _log.isEmpty
              ? const Center(child: Text('等待数据...'))
              : ListView.builder(
                  padding: const EdgeInsets.symmetric(vertical: 4),
                  itemCount: _log.length,
                  itemBuilder: (context, i) {
                    final entry = _log[i];
                    final t = entry.time;
                    final d = entry.data;
                    return Container(
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                      decoration: BoxDecoration(
                        border: Border(
                          bottom: BorderSide(
                            color: theme.colorScheme.outlineVariant,
                            width: 0.5,
                          ),
                        ),
                      ),
                      child: Row(
                        children: [
                          Text(
                            '${t.hour.toString().padLeft(2, '0')}:'
                            '${t.minute.toString().padLeft(2, '0')}:'
                            '${t.second.toString().padLeft(2, '0')}.'
                            '${t.millisecond.toString().padLeft(3, '0')}',
                            style: TextStyle(
                              fontSize: 11,
                              fontFamily: 'monospace',
                              color: theme.colorScheme.outline,
                            ),
                          ),
                          const SizedBox(width: 8),
                          Expanded(
                            child: Text(
                              'FSR=${d.fsrRaw}(${d.isPressed ? "按压" : "无"})  '
                              'F=${d.forceN.toStringAsFixed(3)}N  '
                              'T=${d.temperature.toStringAsFixed(1)}°C  '
                              'RH=${d.humidity.toStringAsFixed(1)}%  '
                              'Bat=${d.batteryMv}mV',
                              style: const TextStyle(fontSize: 12, fontFamily: 'monospace'),
                            ),
                          ),
                        ],
                      ),
                    );
                  },
                ),
        ),
      ],
    );
  }

  Widget _buildLogTab(ThemeData theme) {
    final logs = widget.bleService.debugLog;
    return Column(
      children: [
        Container(
          width: double.infinity,
          padding: const EdgeInsets.all(8),
          color: theme.colorScheme.surfaceContainerHighest,
          child: Row(
            children: [
              Text('BleService 调试日志', style: TextStyle(fontSize: 12, fontWeight: FontWeight.bold)),
              const Spacer(),
              Text('${logs.length} 条', style: TextStyle(fontSize: 11, color: theme.colorScheme.outline)),
              const SizedBox(width: 8),
              if (logs.isNotEmpty)
                IconButton(
                  icon: Icon(Icons.copy, size: 18),
                  tooltip: '复制日志',
                  onPressed: () {
                    Clipboard.setData(ClipboardData(text: logs.join('\n')));
                    ScaffoldMessenger.of(context).showSnackBar(
                      const SnackBar(content: Text('日志已复制'), duration: Duration(seconds: 1)),
                    );
                  },
                  padding: EdgeInsets.zero,
                  constraints: const BoxConstraints(),
                ),
              IconButton(
                icon: Icon(Icons.delete, size: 18),
                onPressed: () {
                  widget.bleService.clearDebugLog();
                  setState(() {});
                },
                padding: EdgeInsets.zero,
                constraints: const BoxConstraints(),
              ),
            ],
          ),
        ),
        Expanded(
          child: logs.isEmpty
              ? const Center(child: Text('暂无日志'))
              : ListView.builder(
                  padding: const EdgeInsets.symmetric(vertical: 2),
                  itemCount: logs.length,
                  itemBuilder: (context, i) {
                    return Container(
                      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                      decoration: BoxDecoration(
                        border: Border(
                          bottom: BorderSide(
                            color: theme.colorScheme.outlineVariant,
                            width: 0.3,
                          ),
                        ),
                      ),
                      child: Text(
                        logs[i],
                        style: const TextStyle(fontSize: 10, fontFamily: 'monospace'),
                      ),
                    );
                  },
                ),
        ),
      ],
    );
  }
}

class _LogEntry {
  final DateTime time;
  final SensorData data;
  _LogEntry({required this.time, required this.data});
}
