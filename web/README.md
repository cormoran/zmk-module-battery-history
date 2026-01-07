# Battery History Web UI

A responsive web application for viewing battery consumption history from ZMK keyboards.

## Features

- **Real-time Battery Display**: Large, color-coded current battery level indicator
- **History Chart**: Interactive SVG chart showing battery levels over time
- **Statistics Dashboard**: Min/max/average levels, drain rate, estimated remaining time
- **Device Metadata**: View recording interval and storage capacity
- **Dark Mode**: Automatic dark mode based on system preferences
- **Responsive Design**: Works on desktop and mobile devices

## Quick Start

```bash
# Install dependencies
npm install

# Generate TypeScript types from proto
npm run generate

# Run development server
npm run dev

# Build for production
npm run build

# Run tests
npm test
```

## Project Structure

```
src/
├── main.tsx              # React entry point
├── App.tsx               # Main application with connection UI
├── App.css               # Global styles
├── components/           # UI components
│   ├── BatteryHistorySection.tsx   # Main battery history display
│   ├── BatteryHistoryChart.tsx     # SVG chart component
│   ├── BatteryIndicator.tsx        # Battery level indicator
│   └── *.css                       # Component styles
└── proto/                # Generated protobuf TypeScript types
    └── zmk/battery_history/
        └── battery_history.ts

test/
├── App.spec.tsx                    # Tests for App component
├── BatteryHistorySection.spec.tsx  # Tests for battery history
└── setup.ts                        # Jest setup
```

## How It Works

### Protocol Definition

The protobuf schema is defined in `../proto/zmk/battery_history/battery_history.proto`:

```proto
message GetBatteryHistoryRequest {
    bool include_metadata = 1;
}

message BatteryHistoryEntry {
    uint32 timestamp = 1;
    uint32 battery_level = 2;
}

message GetBatteryHistoryResponse {
    repeated BatteryHistoryEntry entries = 1;
    DeviceMetadata metadata = 2;
    uint32 current_battery_level = 3;
}
```

### Code Generation

TypeScript types are generated using `ts-proto`:

```bash
npm run generate
```

### Using the Components

The main component is `BatteryHistorySection` which handles:

1. Finding the `zmk__battery_history` subsystem
2. Fetching battery history data via RPC
3. Displaying the chart, statistics, and current level

```typescript
import { BatteryHistorySection } from "./components/BatteryHistorySection";

// In a connected context:
<BatteryHistorySection />
```

## Testing

```bash
# Run all tests
npm test

# Run tests in watch mode
npm run test:watch

# Run tests with coverage
npm run test:coverage
```

## Dependencies

- **@cormoran/zmk-studio-react-hook**: React hooks for ZMK Studio
- **@zmkfirmware/zmk-studio-ts-client**: ZMK Studio TypeScript client
- **React 19**: Modern React with hooks
- **Vite**: Fast build tool and dev server
