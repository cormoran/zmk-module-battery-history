/**
 * Battery Status Indicator Component
 *
 * A visual indicator showing the current battery level with
 * animated fill and color-coded status.
 */

import "./BatteryIndicator.css";

interface BatteryIndicatorProps {
  level: number;
  isLoading?: boolean;
}

/**
 * Get status class based on battery level
 */
function getStatusClass(level: number): string {
  if (level > 60) return "status-good";
  if (level > 30) return "status-medium";
  return "status-low";
}

/**
 * Get status text based on battery level
 */
function getStatusText(level: number): string {
  if (level > 80) return "Excellent";
  if (level > 60) return "Good";
  if (level > 40) return "Moderate";
  if (level > 20) return "Low";
  return "Critical";
}

export function BatteryIndicator({ level, isLoading }: BatteryIndicatorProps) {
  const statusClass = getStatusClass(level);
  const statusText = getStatusText(level);

  return (
    <div className={`battery-indicator ${statusClass} ${isLoading ? "loading" : ""}`}>
      <div className="battery-visual">
        <div className="battery-body">
          <div
            className="battery-fill"
            style={{ width: `${Math.min(100, Math.max(0, level))}%` }}
          />
          <div className="battery-segments">
            <div className="segment"></div>
            <div className="segment"></div>
            <div className="segment"></div>
            <div className="segment"></div>
          </div>
        </div>
        <div className="battery-tip"></div>
      </div>

      <div className="battery-info">
        <div className="battery-percentage">
          {isLoading ? (
            <span className="loading-dots">...</span>
          ) : (
            <>
              <span className="percentage-value">{level}</span>
              <span className="percentage-symbol">%</span>
            </>
          )}
        </div>
        <div className="battery-status">{isLoading ? "Loading" : statusText}</div>
      </div>
    </div>
  );
}

export default BatteryIndicator;
