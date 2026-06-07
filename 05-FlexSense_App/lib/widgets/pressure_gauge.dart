import 'dart:math';
import 'package:flutter/material.dart';

class PressureGauge extends StatelessWidget {
  final int adcRaw;
  final double forceN;
  final bool isPressed;
  final bool calibrated;

  const PressureGauge({
    super.key,
    required this.adcRaw,
    required this.forceN,
    required this.isPressed,
    this.calibrated = false,
  });

  int get _percent => (adcRaw / 4095 * 100).clamp(0, 100).toInt();

  Color get _color {
    if (!isPressed) return Colors.grey;
    if (_percent > 70) return Colors.red;
    if (_percent > 30) return Colors.orange;
    return Colors.green;
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Container(
      padding: const EdgeInsets.symmetric(vertical: 16),
      child: Column(
        children: [
          SizedBox(
            width: 200,
            height: 200,
            child: CustomPaint(
              painter: _GaugePainter(
                percent: _percent,
                isPressed: isPressed,
                color: _color,
                backgroundColor: theme.colorScheme.surfaceContainerHighest,
              ),
              child: Center(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text(
                      '$_percent%',
                      style: TextStyle(
                        fontSize: 36,
                        fontWeight: FontWeight.bold,
                        color: isPressed
                            ? theme.colorScheme.onSurface
                            : theme.colorScheme.outline,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      '${forceN.toStringAsFixed(3)} N',
                      style: TextStyle(
                        fontSize: 14,
                        color: theme.colorScheme.outline,
                        fontFamily: 'monospace',
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
          const SizedBox(height: 8),
          AnimatedContainer(
            duration: const Duration(milliseconds: 200),
            padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 6),
            decoration: BoxDecoration(
              color: isPressed
                  ? _color.withValues(alpha: 0.2)
                  : theme.colorScheme.surfaceContainerHighest,
              borderRadius: BorderRadius.circular(20),
            ),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(
                  isPressed ? Icons.touch_app : Icons.touch_app_outlined,
                  size: 18,
                  color: isPressed ? _color : theme.colorScheme.outline,
                ),
                const SizedBox(width: 6),
                Text(
                  isPressed ? '已按压' : '无按压',
                  style: TextStyle(
                    fontWeight: FontWeight.w600,
                    color: isPressed ? _color : theme.colorScheme.outline,
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 8),
          Text(
            'ADC: $adcRaw / 4095    ${calibrated ? "已校准" : "未校准"}',
            style: TextStyle(
              fontSize: 11,
              fontFamily: 'monospace',
              color: theme.colorScheme.outline,
            ),
          ),
        ],
      ),
    );
  }
}

class _GaugePainter extends CustomPainter {
  final int percent;
  final bool isPressed;
  final Color color;
  final Color backgroundColor;

  _GaugePainter({
    required this.percent,
    required this.isPressed,
    required this.color,
    required this.backgroundColor,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = min(size.width, size.height) / 2 - 16;
    const strokeWidth = 20.0;

    // Background ring
    final bgPaint = Paint()
      ..color = backgroundColor
      ..style = PaintingStyle.stroke
      ..strokeWidth = strokeWidth
      ..strokeCap = StrokeCap.round;
    canvas.drawCircle(center, radius, bgPaint);

    // Foreground arc (filled)
    if (isPressed && percent > 0) {
      final sweep = 2 * pi * (percent / 100);
      final startAngle = -pi / 2; // start from top

      final fgPaint = Paint()
        ..color = color
        ..style = PaintingStyle.stroke
        ..strokeWidth = strokeWidth
        ..strokeCap = StrokeCap.round;

      canvas.drawArc(
        Rect.fromCircle(center: center, radius: radius),
        startAngle,
        sweep,
        false,
        fgPaint,
      );

      // Draw small dots at the arc ends for visual flair
      if (percent > 5) {
        final dotPaint = Paint()
          ..color = color
          ..style = PaintingStyle.fill;
        final endAngle = startAngle + sweep;
        canvas.drawCircle(
          Offset(center.dx + radius * cos(endAngle), center.dy + radius * sin(endAngle)),
          5,
          dotPaint,
        );
      }
    }

    // Scale marks (small tick marks around the ring)
    final tickPaint = Paint()
      ..color = backgroundColor.withValues(alpha: 0.5)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1;
    for (int i = 0; i <= 20; i++) {
      final angle = -pi / 2 + (2 * pi) * (i / 20);
      final innerR = radius + strokeWidth / 2 + 4;
      if (i % 5 == 0) {
        // Major tick - longer
        canvas.drawLine(
          Offset(center.dx + innerR * cos(angle), center.dy + innerR * sin(angle)),
          Offset(center.dx + (innerR + 6) * cos(angle), center.dy + (innerR + 6) * sin(angle)),
          tickPaint..strokeWidth = 2,
        );
      }
    }
  }

  @override
  bool shouldRepaint(_GaugePainter old) =>
      old.percent != percent || old.isPressed != isPressed || old.color != color;
}
