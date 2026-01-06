/**
 * ZMK Battery History Module - Main Application
 * 
 * Web UI for viewing battery consumption history from ZMK keyboards.
 * Features:
 * - Real-time battery level display
 * - Historical battery data visualization
 * - Statistics and estimated battery life
 */

import { useContext, useState } from "react";
import "./App.css";
import { connect as serial_connect } from "@zmkfirmware/zmk-studio-ts-client/transport/serial";
import {
  ZMKConnection,
  ZMKCustomSubsystem,
  ZMKAppContext,
} from "@cormoran/zmk-studio-react-hook";
import { Request, Response } from "./proto/zmk/template/custom";
import { BatteryHistorySection, BATTERY_HISTORY_SUBSYSTEM } from "./components/BatteryHistorySection";

// Custom subsystem identifier for template - must match firmware registration
export const SUBSYSTEM_IDENTIFIER = "zmk__template";

function App() {
  return (
    <div className="app">
      <header className="app-header">
        <h1>🔋 Battery History</h1>
        <p>Monitor your keyboard's battery consumption over time</p>
      </header>

      <ZMKConnection
        renderDisconnected={({ connect, isLoading, error }) => (
          <section className="card connect-card">
            <div className="connect-content">
              <div className="connect-icon">⌨️</div>
              <h2>Connect Your Keyboard</h2>
              <p>Connect via USB serial to view battery history data.</p>
              
              {isLoading && (
                <div className="loading-indicator">
                  <span className="loading-spinner"></span>
                  <span>Connecting...</span>
                </div>
              )}
              
              {error && (
                <div className="error-message">
                  <p>🚨 {error}</p>
                </div>
              )}
              
              {!isLoading && (
                <button
                  className="btn btn-primary btn-large"
                  onClick={() => connect(serial_connect)}
                >
                  🔌 Connect via USB
                </button>
              )}
              
              <p className="connect-hint">
                Make sure your keyboard is connected and has Studio mode enabled.
              </p>
            </div>
          </section>
        )}
        renderConnected={({ disconnect, deviceName }) => (
          <>
            <section className="card device-card">
              <div className="device-status">
                <div className="device-info">
                  <span className="status-indicator connected"></span>
                  <span className="device-name">{deviceName}</span>
                </div>
                <button className="btn btn-secondary btn-small" onClick={disconnect}>
                  Disconnect
                </button>
              </div>
            </section>

            <BatteryHistorySection />
          </>
        )}
      />

      <footer className="app-footer">
        <p>
          <strong>ZMK Battery History Module</strong>
        </p>
        <p className="footer-hint">
          Data is stored on your keyboard and fetched each time you connect.
        </p>
      </footer>
    </div>
  );
}

export function RPCTestSection() {
  const zmkApp = useContext(ZMKAppContext);
  const [inputValue, setInputValue] = useState<number>(42);
  const [response, setResponse] = useState<string | null>(null);
  const [isLoading, setIsLoading] = useState(false);

  if (!zmkApp) return null;

  const subsystem = zmkApp.findSubsystem(SUBSYSTEM_IDENTIFIER);

  // Send a sample request to the firmware
  const sendSampleRequest = async () => {
    if (!zmkApp.state.connection || !subsystem) return;

    setIsLoading(true);
    setResponse(null);

    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );

      // Create the request using ts-proto
      const request = Request.create({
        sample: {
          value: inputValue,
        },
      });

      // Encode and send the request
      const payload = Request.encode(request).finish();
      const responsePayload = await service.callRPC(payload);

      if (responsePayload) {
        const resp = Response.decode(responsePayload);
        console.log("Decoded response:", resp);

        if (resp.sample) {
          setResponse(resp.sample.value);
        } else if (resp.error) {
          setResponse(`Error: ${resp.error.message}`);
        }
      }
    } catch (error) {
      console.error("RPC call failed:", error);
      setResponse(
        `Failed: ${error instanceof Error ? error.message : "Unknown error"}`
      );
    } finally {
      setIsLoading(false);
    }
  };

  if (!subsystem) {
    return (
      <section className="card">
        <div className="warning-message">
          <p>
            ⚠️ Subsystem "{SUBSYSTEM_IDENTIFIER}" not found. Make sure your
            firmware includes the template module.
          </p>
        </div>
      </section>
    );
  }

  return (
    <section className="card">
      <h2>RPC Test</h2>
      <p>Send a sample request to the firmware:</p>

      <div className="input-group">
        <label htmlFor="value-input">Value:</label>
        <input
          id="value-input"
          type="number"
          value={inputValue}
          onChange={(e) => setInputValue(parseInt(e.target.value) || 0)}
        />
      </div>

      <button
        className="btn btn-primary"
        disabled={isLoading}
        onClick={sendSampleRequest}
      >
        {isLoading ? "⏳ Sending..." : "📤 Send Request"}
      </button>

      {response && (
        <div className="response-box">
          <h3>Response from Firmware:</h3>
          <pre>{response}</pre>
        </div>
      )}
    </section>
  );
}

export default App;
