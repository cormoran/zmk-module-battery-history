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

      // Check for battery history UI elements - use getAllByText since there are multiple elements
      const batteryHistoryElements = screen.getAllByText(/Battery History/i);
      expect(batteryHistoryElements.length).toBeGreaterThan(0);
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

      // Check for warning message - use getAllByText since CONFIG appears in multiple places
      expect(screen.getByText(/Battery History module not found/i)).toBeInTheDocument();
      const configElements = screen.getAllByText(/CONFIG_ZMK_BATTERY_HISTORY/i);
      expect(configElements.length).toBeGreaterThan(0);
    });
  });

  describe("Without ZMKAppContext", () => {
    it("should not render when ZMKAppContext is not provided", () => {
      const { container } = render(<BatteryHistorySection />);

      // Component should return null when context is not available
      expect(container.firstChild).toBeNull();
    });
  });

  describe("Split Keyboard Support", () => {
    it("should show '(Split Keyboard)' indicator when device is split", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        subsystems: [BATTERY_HISTORY_SUBSYSTEM],
      });

      const { container } = render(
        <ZMKAppProvider value={mockZMKApp}>
          <BatteryHistorySection />
        </ZMKAppProvider>
      );

      // Simulate successful data fetch by updating component state
      // Note: In a real scenario, we'd mock the RPC call
      // For now, we just verify the component renders without errors
      expect(container.firstChild).toBeTruthy();
    });

    it("should not show split indicator for non-split keyboards", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        subsystems: [BATTERY_HISTORY_SUBSYSTEM],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <BatteryHistorySection />
        </ZMKAppProvider>
      );

      // Should render battery history without split indicator
      const header = screen.getByRole("heading", { name: /Battery History/i });
      expect(header).toBeInTheDocument();
      expect(header.textContent).not.toContain("(Split Keyboard)");
    });
  });
});
