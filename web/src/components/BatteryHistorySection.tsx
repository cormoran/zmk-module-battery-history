/**
 * Battery History Section Component
 *
 * Main component for displaying battery history data from a ZMK device.
 * Handles data fetching, display, and interaction with the device.
 */

import { useContext, useState, useCallback, useEffect } from "react";
import {
  ZMKAppContext,
  ZMKCustomSubsystem,
} from "@cormoran/zmk-studio-react-hook";
import {
  Request,
  Response,
  GetBatteryHistoryResponse,
  BatteryHistoryEntry,
} from "../proto/zmk/battery_history/battery_history";
import { BatteryHistoryChart } from "./BatteryHistoryChart";
import { BatteryIndicator } from "./BatteryIndicator";
import "./BatteryHistorySection.css";

// Custom subsystem identifier - must match firmware registration
export const BATTERY_HISTORY_SUBSYSTEM = "zmk__battery_history";

interface BatteryHistoryState {
  data: GetBatteryHistoryResponse | null;
  isLoading: boolean;
  error: string | null;
  lastFetched: Date | null;
}

export function BatteryHistorySection() {
  const zmkApp = useContext(ZMKAppContext);
  const [state, setState] = useState<BatteryHistoryState>({
    data: null,
    isLoading: false,
    error: null,
    lastFetched: null,
  });

  const subsystem = zmkApp?.findSubsystem(BATTERY_HISTORY_SUBSYSTEM);

  /**
   * Fetch battery history from the device
   */
  const fetchBatteryHistory = useCallback(async () => {
    if (!zmkApp?.state.connection || !subsystem) return;

    setState((prev) => ({ ...prev, isLoading: true, error: null }));

    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );

      // Create the request
      const request = Request.create({
        getHistory: {
          includeMetadata: true,
        },
      });

      // Encode and send the request
      const payload = Request.encode(request).finish();
      const responsePayload = await service.callRPC(payload);

      if (responsePayload) {
        const resp = Response.decode(responsePayload);
        console.log("Battery history response:", resp);

        if (resp.error) {
          setState((prev) => ({
            ...prev,
            isLoading: false,
            error: resp.error?.message || "Unknown error",
          }));
        } else if (resp.getHistory) {
          setState({
            data: resp.getHistory,
            isLoading: false,
            error: null,
            lastFetched: new Date(),
          });
        }
      }
    } catch (error) {
      console.error("Failed to fetch battery history:", error);
      setState((prev) => ({
        ...prev,
        isLoading: false,
        error: error instanceof Error ? error.message : "Failed to fetch data",
      }));
    }
  }, [zmkApp, subsystem]);

  /**
   * Clear battery history on the device
   */
  const clearBatteryHistory = useCallback(async () => {
    if (!zmkApp?.state.connection || !subsystem) return;

    const confirmed = window.confirm(
      "Are you sure you want to clear all battery history data from the device?"
    );
    if (!confirmed) return;

    setState((prev) => ({ ...prev, isLoading: true, error: null }));

    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );

      const request = Request.create({
        clearHistory: {},
      });

      const payload = Request.encode(request).finish();
      const responsePayload = await service.callRPC(payload);

      if (responsePayload) {
        const resp = Response.decode(responsePayload);

        if (resp.error) {
          setState((prev) => ({
            ...prev,
            isLoading: false,
            error: resp.error?.message || "Unknown error",
          }));
        } else if (resp.clearHistory) {
          // Refetch to show empty state
          await fetchBatteryHistory();
        }
      }
    } catch (error) {
      console.error("Failed to clear battery history:", error);
      setState((prev) => ({
        ...prev,
        isLoading: false,
        error: error instanceof Error ? error.message : "Failed to clear data",
      }));
    }
  }, [zmkApp, subsystem, fetchBatteryHistory]);

  // Auto-fetch on mount when subsystem is available
  useEffect(() => {
    if (subsystem && !state.data && !state.isLoading) {
      fetchBatteryHistory();
    }
  }, [subsystem, state.data, state.isLoading, fetchBatteryHistory]);

  if (!zmkApp) return null;

  if (!subsystem) {
    return (
      <section className="card battery-section">
        <div className="warning-message">
          <p>
            ⚠️ Battery History module not found on this device.
          </p>
          <p className="warning-hint">
            Make sure your firmware is compiled with{" "}
            <code>CONFIG_ZMK_BATTERY_HISTORY=y</code> and{" "}
            <code>CONFIG_ZMK_BATTERY_HISTORY_STUDIO_RPC=y</code>
          </p>
        </div>
      </section>
    );
  }

  const { data, isLoading, error, lastFetched } = state;

  // Use metadata to determine if this is a split keyboard
  const isSplit = data?.metadata?.isSplit ?? false;
  // Always use sources array
  const sources = data?.sources ?? [];

  return (
    <section className="card battery-section">
      <div className="section-header">
        <h2>🔋 Battery History {isSplit ? "(Split Keyboard)" : ""}</h2>
        <div className="section-actions">
          <button
            className="btn btn-icon"
            onClick={fetchBatteryHistory}
            disabled={isLoading}
            title="Refresh data"
          >
            <span className={isLoading ? "spin" : ""}>🔄</span>
          </button>
          <button
            className="btn btn-icon btn-danger"
            onClick={clearBatteryHistory}
            disabled={isLoading}
            title="Clear history"
          >
            🗑️
          </button>
        </div>
      </div>

      {error && (
        <div className="error-message">
          <p>🚨 {error}</p>
        </div>
      )}

      {/* Global metadata */}
      {data?.metadata && (
        <div className="device-metadata">
          <div className="metadata-item">
            <span className="metadata-label">Recording Interval</span>
            <span className="metadata-value">
              {data.metadata.recordingIntervalMinutes} min
            </span>
          </div>
          <div className="metadata-item">
            <span className="metadata-label">Max Entries</span>
            <span className="metadata-value">{data.metadata.maxEntries}</span>
          </div>
          {isSplit && (
            <div className="metadata-item">
              <span className="metadata-label">Peripherals</span>
              <span className="metadata-value">{data.metadata.peripheralCount}</span>
            </div>
          )}
        </div>
      )}

      {/* Display each source (central and peripherals if split) */}
      {sources.map((source) => (
        <div key={source.source} className={isSplit ? "source-section" : ""}>
          {isSplit && <h3>📱 {source.sourceName}</h3>}
          
          {/* Current Battery Status */}
          <div className="battery-status-section">
            <BatteryIndicator
              level={source.currentBatteryLevel ?? 0}
              isLoading={isLoading && !data}
            />
            
            <div className="device-metadata">
              <div className="metadata-item">
                <span className="metadata-label">Current Entries</span>
                <span className="metadata-value">{source.entries.length}</span>
              </div>
            </div>
          </div>

          {/* Battery History Chart */}
          {source.entries.length > 0 && (
            <>
              <div className="chart-section">
                <h3>Battery Level Over Time</h3>
                <BatteryHistoryChart entries={source.entries} />
              </div>

              {/* Statistics */}
              <div className="stats-section">
                <BatteryStats entries={source.entries} />
              </div>
            </>
          )}
        </div>
      ))}

      {/* Last updated */}
      {lastFetched && (
        <div className="last-updated">
          Last updated: {lastFetched.toLocaleTimeString()}
        </div>
      )}
    </section>
  );
}

/**
 * Battery statistics component
 */
function BatteryStats({
  entries,
}: {
  entries: BatteryHistoryEntry[];
}) {
  if (entries.length < 2) return null;

  // Calculate statistics
  const levels = entries.map((e: BatteryHistoryEntry) => e.batteryLevel);
  const minLevel = Math.min(...levels);
  const maxLevel = Math.max(...levels);
  const avgLevel = Math.round(levels.reduce((a: number, b: number) => a + b, 0) / levels.length);

  // Estimate drain rate (percentage per hour)
  const firstEntry = entries[0];
  const lastEntry = entries[entries.length - 1];
  const timeDiffHours = (lastEntry.timestamp - firstEntry.timestamp) / 3600;
  const levelDiff = firstEntry.batteryLevel - lastEntry.batteryLevel;
  const drainRate = timeDiffHours > 0 ? levelDiff / timeDiffHours : 0;

  // Estimate remaining time
  const remainingHours =
    drainRate > 0 ? lastEntry.batteryLevel / drainRate : null;

  return (
    <div className="battery-stats">
      <h3>📊 Statistics</h3>
      <div className="stats-grid">
        <div className="stat-item">
          <span className="stat-value">{minLevel}%</span>
          <span className="stat-label">Minimum</span>
        </div>
        <div className="stat-item">
          <span className="stat-value">{maxLevel}%</span>
          <span className="stat-label">Maximum</span>
        </div>
        <div className="stat-item">
          <span className="stat-value">{avgLevel}%</span>
          <span className="stat-label">Average</span>
        </div>
        <div className="stat-item">
          <span className="stat-value">
            {drainRate > 0 ? drainRate.toFixed(1) : "—"}%/h
          </span>
          <span className="stat-label">Drain Rate</span>
        </div>
        {remainingHours !== null && remainingHours > 0 && (
          <div className="stat-item stat-highlight">
            <span className="stat-value">
              {remainingHours > 24
                ? `${Math.round(remainingHours / 24)}d`
                : `${Math.round(remainingHours)}h`}
            </span>
            <span className="stat-label">Est. Remaining</span>
          </div>
        )}
      </div>
    </div>
  );
}

export default BatteryHistorySection;
