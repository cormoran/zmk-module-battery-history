/**
 * Battery History Chart Component
 *
 * A responsive SVG-based chart that displays battery level history over time.
 * Features:
 * - Gradient fill for visual appeal
 * - Hover tooltip for data points
 * - Responsive design with dark mode support
 * - Color-coded battery levels (green/yellow/red)
 */

import { useState, useMemo } from "react";
import type { BatteryHistoryEntry } from "../proto/zmk/battery_history/battery_history";
import "./BatteryHistoryChart.css";

interface BatteryHistoryChartProps {
  entries: BatteryHistoryEntry[];
  startTimestamp?: number;
}

interface TooltipData {
  x: number;
  y: number;
  entry: BatteryHistoryEntry;
  index: number;
}

/**
 * Format timestamp to readable time string
 */
function formatTimestamp(timestampSeconds: number): string {
  const hours = Math.floor(timestampSeconds / 3600);
  const minutes = Math.floor((timestampSeconds % 3600) / 60);

  if (hours > 24) {
    const days = Math.floor(hours / 24);
    const remainingHours = hours % 24;
    return `${days}d ${remainingHours}h ago`;
  }

  if (hours > 0) {
    return `${hours}h ${minutes}m ago`;
  }

  return `${minutes}m ago`;
}

/**
 * Get color based on battery level
 */
function getBatteryColor(level: number): string {
  if (level > 60) return "#22c55e"; // green
  if (level > 30) return "#eab308"; // yellow
  return "#ef4444"; // red
}

/**
 * Get color class based on battery level
 */
function getBatteryColorClass(level: number): string {
  if (level > 60) return "battery-good";
  if (level > 30) return "battery-medium";
  return "battery-low";
}

export function BatteryHistoryChart({
  entries,
  startTimestamp,
}: BatteryHistoryChartProps) {
  const [tooltip, setTooltip] = useState<TooltipData | null>(null);

  // Chart dimensions
  const chartWidth = 600;
  const chartHeight = 250;
  const padding = useMemo(
    () => ({ top: 20, right: 30, bottom: 40, left: 50 }),
    []
  );
  const innerWidth = chartWidth - padding.left - padding.right;
  const innerHeight = chartHeight - padding.top - padding.bottom;

  const mappedEntries = useMemo(() => {
    const newEntries = new Array<BatteryHistoryEntry>(entries.length);
    entries.reduce((timestampOffset, entry, i) => {
      // Ensure timestamps are strictly increasing by at least 60s
      // Since timestamp resets at device reboot, we adjust accordingly

      const timestamp = (() => {
        if (i > 0 && entry.timestamp < entries[i - 1].timestamp) {
          timestampOffset = entries[i - 1].timestamp + 60;
        }
        return entry.timestamp + timestampOffset;
      })();
      newEntries[i] = {
        ...entry,
        timestamp: timestamp,
      };
      return timestampOffset;
    }, 0);
    return newEntries;
  }, [entries]);

  // Calculate chart data
  const chartData = useMemo(() => {
    if (mappedEntries.length === 0) {
      return {
        points: [],
        pathData: "",
        areaData: "",
        xLabels: [],
        yLabels: [0, 25, 50, 75, 100],
      };
    }

    const maxTimestamp = Math.max(...mappedEntries.map((e) => e.timestamp));
    const minTimestamp = Math.min(...mappedEntries.map((e) => e.timestamp));
    const timeRange = maxTimestamp - minTimestamp || 1;

    // Scale functions
    const scaleX = (timestamp: number) =>
      padding.left + ((timestamp - minTimestamp) / timeRange) * innerWidth;
    const scaleY = (level: number) =>
      padding.top + ((100 - level) / 100) * innerHeight;

    // Generate points
    const points = mappedEntries.map((entry, index) => ({
      x: scaleX(entry.timestamp),
      y: scaleY(entry.batteryLevel),
      entry,
      index,
    }));

    // Generate SVG path for line
    const pathData = points
      .map((p, i) => `${i === 0 ? "M" : "L"} ${p.x} ${p.y}`)
      .join(" ");

    // Generate SVG path for area fill
    const areaData = `${pathData} L ${points[points.length - 1].x} ${
      padding.top + innerHeight
    } L ${points[0].x} ${padding.top + innerHeight} Z`;

    // X-axis labels (time)
    const xLabels: { x: number; label: string }[] = [];
    const labelCount = Math.min(6, mappedEntries.length);
    if (mappedEntries.length > 0) {
      for (let i = 0; i < labelCount; i++) {
        const idx = Math.floor(
          (i * (mappedEntries.length - 1)) / (labelCount - 1)
        );
        const entry = mappedEntries[idx];
        if (entry) {
          const now =
            startTimestamp || mappedEntries[mappedEntries.length - 1].timestamp;
          const ago = now - entry.timestamp;
          xLabels.push({
            x: scaleX(entry.timestamp),
            label: formatTimestamp(ago),
          });
        }
      }
    }

    return {
      points,
      pathData,
      areaData,
      xLabels,
      yLabels: [0, 25, 50, 75, 100],
    };
  }, [mappedEntries, innerWidth, innerHeight, padding, startTimestamp]);

  if (mappedEntries.length === 0) {
    return (
      <div className="chart-empty">
        <div className="chart-empty-icon">📊</div>
        <p>No battery history data available yet.</p>
        <p className="chart-empty-hint">
          Data will appear as the device records battery levels.
        </p>
      </div>
    );
  }

  const handleMouseMove =
    (point: (typeof chartData.points)[0]) =>
    (e: React.MouseEvent<SVGCircleElement>) => {
      const rect = e.currentTarget.closest("svg")?.getBoundingClientRect();
      if (rect) {
        setTooltip({
          ...point,
          x: e.clientX - rect.left,
          y: e.clientY - rect.top,
        });
      }
    };

  return (
    <div className="chart-container">
      <svg
        viewBox={`0 0 ${chartWidth} ${chartHeight}`}
        className="battery-chart"
        preserveAspectRatio="xMidYMid meet"
      >
        {/* Gradient definitions */}
        <defs>
          <linearGradient id="areaGradient" x1="0%" y1="0%" x2="0%" y2="100%">
            <stop offset="0%" stopColor="#4a90d9" stopOpacity="0.4" />
            <stop offset="100%" stopColor="#4a90d9" stopOpacity="0.05" />
          </linearGradient>
          <filter id="glow">
            <feGaussianBlur stdDeviation="2" result="coloredBlur" />
            <feMerge>
              <feMergeNode in="coloredBlur" />
              <feMergeNode in="SourceGraphic" />
            </feMerge>
          </filter>
        </defs>

        {/* Grid lines */}
        {chartData.yLabels.map((level) => {
          const y = padding.top + ((100 - level) / 100) * innerHeight;
          return (
            <g key={level}>
              <line
                x1={padding.left}
                y1={y}
                x2={padding.left + innerWidth}
                y2={y}
                className="grid-line"
              />
              <text
                x={padding.left - 10}
                y={y + 4}
                className="axis-label"
                textAnchor="end"
              >
                {level}%
              </text>
            </g>
          );
        })}

        {/* X-axis labels */}
        {chartData.xLabels.map((label, i) => (
          <text
            key={i}
            x={label.x}
            y={chartHeight - 10}
            className="axis-label"
            textAnchor="middle"
          >
            {label.label}
          </text>
        ))}

        {/* Area fill */}
        <path
          d={chartData.areaData}
          className="chart-area"
          fill="url(#areaGradient)"
        />

        {/* Line */}
        <path
          d={chartData.pathData}
          className="chart-line"
          filter="url(#glow)"
        />

        {/* Data points */}
        {chartData.points.map((point) => (
          <circle
            key={point.index}
            cx={point.x}
            cy={point.y}
            r={4}
            className="chart-point"
            style={{ fill: getBatteryColor(point.entry.batteryLevel) }}
            onMouseEnter={handleMouseMove(point)}
            onMouseLeave={() => setTooltip(null)}
          />
        ))}

        {/* Axis lines */}
        <line
          x1={padding.left}
          y1={padding.top}
          x2={padding.left}
          y2={padding.top + innerHeight}
          className="axis-line"
        />
        <line
          x1={padding.left}
          y1={padding.top + innerHeight}
          x2={padding.left + innerWidth}
          y2={padding.top + innerHeight}
          className="axis-line"
        />
      </svg>

      {/* Tooltip */}
      {tooltip && (
        <div
          className="chart-tooltip"
          style={{
            left: tooltip.x + 10,
            top: tooltip.y - 10,
          }}
        >
          <div className="tooltip-level">
            <span
              className={`tooltip-indicator ${getBatteryColorClass(
                tooltip.entry.batteryLevel
              )}`}
            ></span>
            {tooltip.entry.batteryLevel}%
          </div>
          <div className="tooltip-time">
            {formatTimestamp(
              (startTimestamp ||
                mappedEntries[mappedEntries.length - 1].timestamp) -
                tooltip.entry.timestamp
            )}
          </div>
        </div>
      )}

      {/* Legend */}
      <div className="chart-legend">
        <span className="legend-item">
          <span className="legend-dot battery-good"></span>
          Good ({">"}60%)
        </span>
        <span className="legend-item">
          <span className="legend-dot battery-medium"></span>
          Medium (30-60%)
        </span>
        <span className="legend-item">
          <span className="legend-dot battery-low"></span>
          Low ({"<"}30%)
        </span>
      </div>
    </div>
  );
}

export default BatteryHistoryChart;
