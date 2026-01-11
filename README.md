# ZMK Battery History Module

Track and visualize battery consumption history for your ZMK-powered keyboard. This module stores battery level data over time and provides a web interface to analyze battery life patterns.

## Features

- **Battery History Tracking**: Automatically records battery levels at configurable intervals
- **Flash-Wear Optimization**: Batch writes to minimize flash storage wear (important for nRF52840-based boards)
- **Web UI Dashboard**: Beautiful, responsive interface to view battery history
- **Statistics**: View drain rate, estimated remaining time, and historical trends
- **Dark Mode**: Full dark mode support for comfortable viewing

## Quick Start

### 1. Add to your ZMK config

Add this module to your `config/west.yml`:

```yaml
manifest:
  remotes:
    - name: cormoran
      url-base: https://github.com/cormoran
  projects:
    - name: zmk-module-battery-history
      remote: cormoran
      revision: main
    # Required: Use the custom studio protocol fork
    - name: zmk
      remote: cormoran
      revision: v0.3+custom-studio-protocol
      import:
        file: app/west.yml
```

### 2. Enable in your shield config

Add to your `config/<shield>.conf`:

```conf
# Enable dependencies
CONFIG_ZMK_BATTERY_REPORTING=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_RUNTIME=y

# Enable battery history tracking
CONFIG_ZMK_BATTERY_HISTORY=y

# Enable web UI access via Studio protocol
CONFIG_ZMK_STUDIO=y
CONFIG_ZMK_BATTERY_HISTORY_STUDIO_RPC=y
```

### 3. Access the Web UI

1. Connect your keyboard via USB
2. Visit the web UI (hosted on GitHub Pages or run locally)
3. Click "Connect via USB" to view your battery history

## Configuration Options

| Config                                             | Default | Description                                                        |
| -------------------------------------------------- | ------- | ------------------------------------------------------------------ |
| `CONFIG_ZMK_BATTERY_HISTORY_MAX_ENTRIES`           | 192     | Maximum stored entries (~8 days at 1hr intervals)                  |
| `CONFIG_ZMK_BATTERY_HISTORY_INTERVAL_MINUTES`      | 5       | Recording interval in minutes (to memory)                          |
| `CONFIG_ZMK_BATTERY_HISTORY_SAVE_INTERVAL_MINUTES` | 5       | Battery history forced save interval in minutes (to flush storage) |
| `CONFIG_ZMK_BATTERY_HISTORY_SAVE_LEVEL_THRESHOLD`  | 2       | Battery level change threshold for saving (percentage)             |
| `CONFIG_ZMK_BATTERY_HISTORY_FORCE_SAVE_ON_SLEEP`   | 2       | Force save battery history on sleep (percentage)                   |
| `CONFIG_ZMK_BATTERY_IGNORE_ZERO_LEVEL`             | y       | Ignore zero percent battery level readings                         |
| `CONFIG_ZMK_BATTERY_SKIP_IF_USB_POWERED`           | y       | Skip battery history recording when USB powered                    |

## Web UI

The web interface provides:

- **Current Battery Level**: Large, color-coded display
- **History Chart**: Interactive graph showing battery over time
- **Statistics**: Min/max/average levels, drain rate, estimated remaining time
- **Device Metadata**: Recording interval, storage capacity

### Running Locally

```bash
cd web
npm install
npm run dev
```

### Building for Production

```bash
cd web
npm run build
```

## Development

### Project Structure

```
├── proto/zmk/battery_history/    # Protocol buffer definitions
├── src/battery_history/          # C implementation
├── include/zmk/battery_history/  # Header files
├── web/                          # React web UI
│   ├── src/components/           # UI components
│   └── test/                     # Jest tests
└── tests/                        # ZMK firmware tests
```

### Building & Testing

**Firmware tests:**

```bash
python -m unittest
```

**Web UI tests:**

```bash
cd web
npm test
```

## API Reference

### RPC Protocol

The module exposes these RPC endpoints via the `zmk__battery_history` subsystem:

- `GetBatteryHistory`: Retrieve all stored battery history entries
- `ClearBatteryHistory`: Clear stored history (for future backend sync support)

**Split Keyboard Support**: The protocol now supports split keyboards with separate battery tracking for central and peripheral sides. The response includes a `sources` array with battery data for each source.

### C API

```c
#include <zmk/battery_history/battery_history.h>

// Get number of stored entries
int zmk_battery_history_get_count(void);

// Get entry by index (0 = oldest)
int zmk_battery_history_get_entry(int index, struct zmk_battery_history_entry *entry);

// Get current battery level
int zmk_battery_history_get_current_level(void);

// Clear all history
int zmk_battery_history_clear(void);

// Split keyboard support (available when CONFIG_ZMK_SPLIT is enabled)
// Get number of sources (1 for non-split, 1+N for split with N peripherals)
int zmk_battery_history_get_source_count(void);

// Get battery data for a specific source (0 = central, 1+ = peripheral index)
int zmk_battery_history_get_count_for_source(uint8_t source);
int zmk_battery_history_get_entry_for_source(uint8_t source, int index,
                                              struct zmk_battery_history_entry *entry);
int zmk_battery_history_get_current_level_for_source(uint8_t source);
```

## Split Keyboard Support

This module now supports split keyboards with separate battery tracking for central and peripheral sides:

- **Automatic Detection**: The module automatically detects split keyboard configurations
- **Per-Source Tracking**: Battery history is tracked separately for each source (central + peripherals)
- **Web UI**: The web interface displays battery data for each source with separate charts and statistics
- **Backward Compatible**: Non-split keyboards continue to work without any changes

When using a split keyboard with `CONFIG_ZMK_SPLIT` enabled:
1. The central side tracks its own battery history
2. Peripheral battery levels are queried via BLE when available (`CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING`)
3. The web UI displays separate sections for each side with individual charts and statistics

## License

MIT License - see [LICENSE](LICENSE) for details.
