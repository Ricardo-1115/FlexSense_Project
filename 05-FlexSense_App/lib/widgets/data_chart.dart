import 'dart:math';
import 'package:flutter/material.dart';
import 'package:fl_chart/fl_chart.dart';

class DataChart extends StatelessWidget {
  final List<double> data;
  final Color color;
  final String label;
  final String? unit;
  final double? fixedMinY;
  final double? fixedMaxY;
  final bool showGrid;

  const DataChart({
    super.key,
    required this.data,
    required this.color,
    required this.label,
    this.unit,
    this.fixedMinY,
    this.fixedMaxY,
    this.showGrid = true,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    if (data.isEmpty) {
      return Center(
        child: Text('等待 $label 数据...',
            style: TextStyle(color: theme.colorScheme.outline, fontSize: 12)),
      );
    }

    final rawMin = data.reduce(min);
    final rawMax = data.reduce(max);
    final range = rawMax - rawMin;

    final minY = fixedMinY ?? max(0, rawMin - range * 0.15);
    final maxY = fixedMaxY ?? rawMax + range * 0.15;
    final effectiveRange = maxY - minY;

    // Avoid division by zero
    final safeMaxY = effectiveRange < 0.01 ? maxY + 1 : maxY;

    final spots = <FlSpot>[];
    for (int i = 0; i < data.length; i++) {
      spots.add(FlSpot(i.toDouble(), data[i]));
    }

    return Padding(
      padding: const EdgeInsets.only(right: 12, top: 4),
      child: LineChart(
        LineChartData(
          minX: 0,
          maxX: max(spots.length - 1, 1).toDouble(),
          minY: minY,
          maxY: safeMaxY,
          clipData: const FlClipData.all(),
          gridData: FlGridData(
            show: showGrid,
            drawVerticalLine: false,
            horizontalInterval: effectiveRange / 4,
            getDrawingHorizontalLine: (value) => FlLine(
              color: theme.colorScheme.outlineVariant,
              strokeWidth: 0.5,
            ),
          ),
          titlesData: FlTitlesData(
            leftTitles: AxisTitles(
              sideTitles: SideTitles(
                showTitles: true,
                reservedSize: 48,
                getTitlesWidget: (value, meta) {
                  String text;
                  if (value >= 100) {
                    text = value.toStringAsFixed(0);
                  } else if (value >= 1) {
                    text = value.toStringAsFixed(1);
                  } else {
                    text = value.toStringAsFixed(2);
                  }
                  return Padding(
                    padding: const EdgeInsets.only(right: 4),
                    child: Text(
                      text,
                      style: TextStyle(
                        fontSize: 10,
                        fontFamily: 'monospace',
                        color: theme.colorScheme.outline,
                      ),
                    ),
                  );
                },
              ),
            ),
            bottomTitles: const AxisTitles(
              sideTitles: SideTitles(showTitles: false),
            ),
            topTitles: const AxisTitles(
              sideTitles: SideTitles(showTitles: false),
            ),
            rightTitles: const AxisTitles(
              sideTitles: SideTitles(showTitles: false),
            ),
          ),
          borderData: FlBorderData(show: false),
          lineTouchData: LineTouchData(
            enabled: true,
            touchTooltipData: LineTouchTooltipData(
              getTooltipItems: (touchedSpots) {
                return touchedSpots.map((spot) {
                  final v = spot.y;
                  String text;
                  if (v >= 100) {
                    text = v.toStringAsFixed(0);
                  } else if (v >= 1) {
                    text = v.toStringAsFixed(1);
                  } else {
                    text = v.toStringAsFixed(2);
                  }
                  if (unit != null) text += ' $unit';
                  return LineTooltipItem(
                    text,
                    TextStyle(
                      color: color,
                      fontWeight: FontWeight.bold,
                      fontSize: 12,
                      fontFamily: 'monospace',
                    ),
                  );
                }).toList();
              },
            ),
          ),
          lineBarsData: [
            LineChartBarData(
              spots: spots,
              isCurved: true,
              preventCurveOverShooting: true,
              color: color,
              barWidth: 2,
              isStrokeCapRound: true,
              dotData: const FlDotData(show: false),
              belowBarData: BarAreaData(
                show: true,
                color: color.withValues(alpha: 0.08),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
