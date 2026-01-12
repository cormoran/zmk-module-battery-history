/**
 * Battery History Section Component
 *
 * Main component for displaying battery history data from a ZMK device.
 * Handles data fetching, display, and interaction with the device.
 * Supports split keyboards with multiple keyboard halves via notifications.
 */

import { useContext, useState, useCallback, useEffect, useRef } from "react";
import {
  ZMKAppContext,
  ZMKCustomSubsystem,
} from "@cormoran/zmk-studio-react-hook";
import {
  Request,
  Response,
  GetBatteryHistoryResponse,
  Notification,
  BatteryHistoryEntry,
} from "../proto/zmk/battery_history/battery_history";
import { BatteryHistoryChart } from "./BatteryHistoryChart";
import { BatteryIndicator } from "./BatteryIndicator";
import "./BatteryHistorySection.css";

// Custom subsystem identifier - must match firmware registration
export const BATTERY_HISTORY_SUBSYSTEM = "zmk__battery_history";

// Source names for display
const SOURCE_NAMES: Record<number, string> = {
  0: "Central",
  1: "Peripheral 1",
  2: "Peripheral 2",
};

interface BatteryHistoryData {
  entries: BatteryHistoryEntry[];
  metadata?: GetBatteryHistoryResponse["metadata"];
  currentBatteryLevel: number;
}

interface BatteryHistoryState {
  // Data per source ID (0 = central, 1+ = peripherals)
  dataBySource: Record<number, BatteryHistoryData>;
  // Currently selected source for display
  selectedSource: number;
  isLoading: boolean;
  error: string | null;
  lastFetched: Date | null;
  // Streaming state for receiving notifications
  streamingSource: number | null;
  streamingProgress: { current: number; total: number } | null;
}

export function BatteryHistorySection() {
  const zmkApp = useContext(ZMKAppContext);
  const [state, setState] = useState<BatteryHistoryState>({
    dataBySource: {},
    selectedSource: 0,
    isLoading: false,
    error: null,
    lastFetched: null,
    streamingSource: null,
    streamingProgress: null,
  });

  // Buffer for accumulating streaming entries
  const streamingBufferRef = useRef<BatteryHistoryEntry[]>([]);

  const subsystem = zmkApp?.findSubsystem(BATTERY_HISTORY_SUBSYSTEM);

  /**
   * Handle incoming notifications for battery history entries
   */
  useEffect(() => {
    if (!zmkApp || !subsystem) return;

    const unsubscribe = zmkApp.onNotification({
      type: "custom",
      subsystemIndex: subsystem.index,
      callback: (notification) => {
        if (!notification.payload) return;

        try {
          const decoded = Notification.decode(notification.payload);
          if (decoded.batteryHistory) {
            const entry = decoded.batteryHistory;
            const sourceId = entry.sourceId;
            
            console.log(
              `Received battery history notification: source=${sourceId}, idx=${entry.entryIndex}/${entry.totalEntries}, last=${entry.isLast}`
            );

            // On first entry, set up streaming state
            if (entry.entryIndex === 0) {
              streamingBufferRef.current = [];
              setState((prev) => ({
                ...prev,
                streamingSource: sourceId,
                streamingProgress: { current: 0, total: entry.totalEntries },
              }));
            }

            // Add entry to buffer if valid
            if (entry.entry && entry.totalEntries > 0) {
              streamingBufferRef.current.push(entry.entry);
              setState((prev) => ({
                ...prev,
                streamingProgress: {
                  current: entry.entryIndex + 1,
                  total: entry.totalEntries,
                },
              }));
            }

            // On last entry, finalize the data
            if (entry.isLast) {
              const entries = [...streamingBufferRef.current];
              streamingBufferRef.current = [];

              setState((prev) => ({
                ...prev,
                dataBySource: {
                  ...prev.dataBySource,
                  [sourceId]: {
                    entries,
                    currentBatteryLevel:
                      entries.length > 0
                        ? entries[entries.length - 1].batteryLevel
                        : 0,
                  },
                },
                streamingSource: null,
                streamingProgress: null,
                lastFetched: new Date(),
              }));
            }
          }
        } catch (error) {
          console.error("Failed to decode notification:", error);
        }
      },
    });

    return unsubscribe;
  }, [zmkApp, subsystem]);

  /**
   * Fetch battery history from the device (central)
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
          setState((prev) => ({
            ...prev,
            dataBySource: {
              ...prev.dataBySource,
              0: {
                entries: resp.getHistory!.entries,
                metadata: resp.getHistory!.metadata,
                currentBatteryLevel: resp.getHistory!.currentBatteryLevel,
              },
            },
            isLoading: false,
            error: null,
            lastFetched: new Date(),
          }));
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
   * Request battery history from a specific peripheral
   */
  const requestPeripheralHistory = useCallback(
    async (peripheralId: number) => {
      if (!zmkApp?.state.connection || !subsystem) return;

      setState((prev) => ({
        ...prev,
        isLoading: true,
        error: null,
        streamingSource: peripheralId,
        streamingProgress: { current: 0, total: 0 },
      }));
      streamingBufferRef.current = [];

      try {
        const service = new ZMKCustomSubsystem(
          zmkApp.state.connection,
          subsystem.index
        );

        const request = Request.create({
          requestPeripheralHistory: {
            peripheralId,
          },
        });

        const payload = Request.encode(request).finish();
        await service.callRPC(payload);
        // Response is just acknowledgement, data comes via notifications

        setState((prev) => ({
          ...prev,
          isLoading: false,
        }));
      } catch (error) {
        console.error("Failed to request peripheral battery history:", error);
        setState((prev) => ({
          ...prev,
          isLoading: false,
          streamingSource: null,
          streamingProgress: null,
          error:
            error instanceof Error
              ? error.message
              : "Failed to request data from peripheral",
        }));
      }
    },
    [zmkApp, subsystem]
  );

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
    if (subsystem && Object.keys(state.dataBySource).length === 0 && !state.isLoading) {
      fetchBatteryHistory();
    }
  }, [subsystem, state.dataBySource, state.isLoading, fetchBatteryHistory]);

  if (!zmkApp) return null;

  if (!subsystem) {
    return (
      <section className="card battery-section">
        <div className="warning-message">
          <p>⚠️ Battery History module not found on this device.</p>
          <p className="warning-hint">
            Make sure your firmware is compiled with{" "}
            <code>CONFIG_ZMK_BATTERY_HISTORY=y</code> and{" "}
            <code>CONFIG_ZMK_BATTERY_HISTORY_STUDIO_RPC=y</code>
          </p>
        </div>
      </section>
    );
  }

  const {
    dataBySource,
    selectedSource,
    isLoading,
    error,
    lastFetched,
    streamingSource,
    streamingProgress,
  } = state;
  const data = dataBySource[selectedSource];
  const availableSources = Object.keys(dataBySource).map(Number);

  return (
    <section className="card battery-section">
      <div className="section-header">
        <h2>🔋 Battery History</h2>
        <div className="section-actions">
          <button
            className="btn btn-icon"
            onClick={fetchBatteryHistory}
            disabled={isLoading}
            title="Refresh central data"
          >
            <span className={isLoading ? "spin" : ""}>🔄</span>
          </button>
          <button
            className="btn btn-icon"
            onClick={() => requestPeripheralHistory(1)}
            disabled={isLoading || streamingSource !== null}
            title="Request peripheral 1 data"
          >
            📡
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

      {/* Source selector */}
      {availableSources.length > 1 && (
        <div className="source-selector">
          <span className="selector-label">Keyboard half:</span>
          <div className="source-buttons">
            {availableSources.map((sourceId) => (
              <button
                key={sourceId}
                className={`btn btn-small ${
                  selectedSource === sourceId ? "btn-primary" : "btn-secondary"
                }`}
                onClick={() =>
                  setState((prev) => ({ ...prev, selectedSource: sourceId }))
                }
              >
                {SOURCE_NAMES[sourceId] || `Source ${sourceId}`}
              </button>
            ))}
          </div>
        </div>
      )}

      {/* Streaming progress */}
      {streamingProgress && streamingSource !== null && (
        <div className="streaming-progress">
          <span className="progress-label">
            Receiving data from {SOURCE_NAMES[streamingSource] || `Source ${streamingSource}`}...
          </span>
          <div className="progress-bar">
            <div
              className="progress-fill"
              style={{
                width: streamingProgress.total > 0
                  ? `${(streamingProgress.current / streamingProgress.total) * 100}%`
                  : "0%",
              }}
            />
          </div>
          <span className="progress-text">
            {streamingProgress.current} / {streamingProgress.total}
          </span>
        </div>
      )}

      {error && (
        <div className="error-message">
          <p>🚨 {error}</p>
        </div>
      )}

      {/* Current Battery Status */}
      <div className="battery-status-section">
        <BatteryIndicator
          level={data?.currentBatteryLevel ?? 0}
          isLoading={isLoading && !data}
          label={
            availableSources.length > 1
              ? SOURCE_NAMES[selectedSource] || `Source ${selectedSource}`
              : undefined
          }
        />

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
            <div className="metadata-item">
              <span className="metadata-label">Current Entries</span>
              <span className="metadata-value">{data.entries.length}</span>
            </div>
          </div>
        )}
      </div>

      {/* Battery History Chart */}
      <div className="chart-section">
        <h3>
          Battery Level Over Time
          {availableSources.length > 1 &&
            ` (${SOURCE_NAMES[selectedSource] || `Source ${selectedSource}`})`}
        </h3>
        <BatteryHistoryChart entries={data?.entries ?? []} />
      </div>

      {/* Statistics */}
      {data && data.entries.length > 0 && (
        <div className="stats-section">
          <BatteryStats entries={data.entries} />
        </div>
      )}

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
  entries: GetBatteryHistoryResponse["entries"];
}) {
  if (entries.length < 2) return null;

  // Calculate statistics
  const levels = entries.map((e) => e.batteryLevel);
  const minLevel = Math.min(...levels);
  const maxLevel = Math.max(...levels);
  const avgLevel = Math.round(
    levels.reduce((a, b) => a + b, 0) / levels.length
  );

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
