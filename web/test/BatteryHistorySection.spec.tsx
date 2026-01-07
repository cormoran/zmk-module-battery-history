/**
 * Tests for BatteryHistorySection component
 * 
 * Tests the battery history display and interaction functionality.
 */

import { render, screen } from "@testing-library/react";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { BatteryHistorySection, BATTERY_HISTORY_SUBSYSTEM } from "../src/components/BatteryHistorySection";

describe("BatteryHistorySection Component", () => {
  describe("With Subsystem", () => {
    it("should render battery history section when subsystem is found", () => {
      // Create a connected mock ZMK app with the required subsystem
      const mockZMKApp = createConnectedMockZMKApp({
        deviceName: "Test Device",
        subsystems: [BATTERY_HISTORY_SUBSYSTEM],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <BatteryHistorySection />
        </ZMKAppProvider>
      );

      // Check for battery history UI elements
      expect(screen.getByText(/Battery History/i)).toBeInTheDocument();
    });

    it("should show refresh and clear buttons", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        subsystems: [BATTERY_HISTORY_SUBSYSTEM],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <BatteryHistorySection />
        </ZMKAppProvider>
      );

      // Check for action buttons
      expect(screen.getByTitle(/Refresh data/i)).toBeInTheDocument();
      expect(screen.getByTitle(/Clear history/i)).toBeInTheDocument();
    });
  });

  describe("Without Subsystem", () => {
    it("should show warning when subsystem is not found", () => {
      // Create a connected mock ZMK app without the required subsystem
      const mockZMKApp = createConnectedMockZMKApp({
        deviceName: "Test Device",
        subsystems: [], // No subsystems
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <BatteryHistorySection />
        </ZMKAppProvider>
      );

      // Check for warning message
      expect(screen.getByText(/Battery History module not found/i)).toBeInTheDocument();
      expect(screen.getByText(/CONFIG_ZMK_BATTERY_HISTORY/i)).toBeInTheDocument();
    });
  });

  describe("Without ZMKAppContext", () => {
    it("should not render when ZMKAppContext is not provided", () => {
      const { container } = render(<BatteryHistorySection />);

      // Component should return null when context is not available
      expect(container.firstChild).toBeNull();
    });
  });
});
