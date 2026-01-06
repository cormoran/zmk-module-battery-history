# ZMK Battery History Module

This ZMK module tracks battery consumption history over time and provides a web UI to visualize the data.

## Features

- **Battery History Tracking**: Monitors battery levels and stores them in persistent storage
- **Configurable Save Interval**: Adjust recording frequency to minimize flash wear (default: 2 hours)
- **Web UI**: Professional interface to view battery consumption charts and manage history
- **Custom Studio RPC**: Retrieve and clear battery history via ZMK Studio protocol

## Setup

You can use this zmk-module with below setup.

1. Add dependency to your `config/west.yml`.

   ```yaml
   manifest:
   remotes:
       ...
       - name: cormoran
       url-base: https://github.com/cormoran
   projects:
       ...
       - name: zmk-module-battery-history
       remote: cormoran
       revision: main # or latest commit hash
       # Below setting required to use unofficial studio custom RPC feature
       - name: zmk
       remote: cormoran
       revision: v0.3+custom-studio-protocol
       import:
           file: app/west.yml
   ```

2. Enable the module in your `config/<shield>.conf`

   ```conf
   # Enable battery history tracking
   CONFIG_ZMK_BATTERY_HISTORY=y

   # Optionally enable Studio RPC for web UI access
   CONFIG_ZMK_STUDIO=y
   CONFIG_ZMK_BATTERY_HISTORY_STUDIO_RPC=y

   # Optional: Configure save interval (minutes)
   CONFIG_ZMK_BATTERY_HISTORY_SAVE_INTERVAL_MINUTES=120

   # Optional: Configure max entries to store
   CONFIG_ZMK_BATTERY_HISTORY_MAX_ENTRIES=200
   ```

## Configuration Options

- `CONFIG_ZMK_BATTERY_HISTORY`: Enable battery history tracking
- `CONFIG_ZMK_BATTERY_HISTORY_STUDIO_RPC`: Enable web UI access via Studio RPC
- `CONFIG_ZMK_BATTERY_HISTORY_MAX_ENTRIES`: Maximum history entries (default: 200, range: 10-500)
- `CONFIG_ZMK_BATTERY_HISTORY_SAVE_INTERVAL_MINUTES`: Save interval in minutes (default: 120, range: 30-1440)

### Storage Considerations

**Important for nRF52840 boards**: Frequent writes to flash can reduce device lifespan. The default 2-hour interval balances accuracy with flash longevity. For AAA battery monitoring:

- **Recommended**: 120-240 minutes (2-4 hours) for general use
- **Conservative**: 360-720 minutes (6-12 hours) for maximum flash life
- **Detailed**: 60 minutes for more granular data (not recommended long-term)

Each entry uses ~8 bytes. 200 entries = ~1.6KB of storage.

## Web UI

Access the web UI at: https://cormoran.github.io/zmk-module-battery-history/

1. Connect your keyboard via USB/Serial
2. Click "Connect Serial" 
3. Click "Fetch History" to view battery consumption chart
4. Use "Clear History" to erase stored data (useful before sending to backend)

The web UI displays:
- Battery consumption chart over time
- Current battery level
- Total data points stored
- Time range of collected data

## Development Guide

### Setup

There are two west workspace layout options.

#### Option1: Download dependencies in parent directory

This option is west's standard way. Choose this option if you want to re-use dependent projects in other zephyr module development.

```bash
mkdir west-workspace
cd west-workspace # this directory becomes west workspace root (topdir)
git clone <this repository>
# rm -r .west # if exists to reset workspace
west init -l . --mf tests/west-test.yml
west update --narrow
west zephyr-export
```

The directory structure becomes like below:

```
west-workspace
  - .west/config
  - build : build output directory
  - <this repository>
  # other dependencies
  - zmk
  - zephyr
  - ...
  # You can develop other zephyr modules in this workspace
  - your-other-repo
```

You can switch between modules by removing `west-workspace/.west` and re-executing `west init ...`.

#### Option2: Download dependencies in ./dependencies (Enabled in dev-container)

Choose this option if you want to download dependencies under this directory (like node_modules in npm). This option is useful for specifying cache target in CI. The layout is relatively easy to recognize if you want to isolate dependencies.

```bash
git clone <this repository>
cd <cloned directory>
west init -l west --mf west-test-standalone.yml
# If you use dev container, start from below commands. Above commands are executed
# automatically.
west update --narrow
west zephyr-export
```

The directory structure becomes like below:

```
<this repository>
  - .west/config
  - build : build output directory
  - dependencies
    - zmk
    - zephyr
    - ...
```

### Dev container

Dev container is configured for setup option2. The container creates below volumes to re-use resources among containers.

- zmk-dependencies: dependencies dir for setup option2
- zmk-build: build output directory
- zmk-root-user: /root, the same to ZMK's official dev container

### Web UI

Please refer [./web/README.md](./web/README.md).

## Test

**ZMK firmware test**

`./tests` directory contains test config for posix to confirm module functionality and config for xiao board to confirm build works.

Tests can be executed by below command:

```bash
# Run all test case and verify results
python -m unittest
```

If you want to execute west command manually, run below. (for zmk-build, the result is not verified.)

```
# Build test firmware for xiao
# `-m tests/zmk-config .` means tests/zmk-config and this repo are added as additional zephyr module
west zmk-build tests/zmk-config/config -m tests/zmk-config .

# Run zmk test cases
# -m . is required to add this module to build
west zmk-test tests -m .
```

**Web UI test**

The `./web` directory includes Jest tests. See [./web/README.md](./web/README.md#testing) for more details.

```bash
cd web
npm test
```

## Publishing Web UI

Github actions are pre-configured to publish web UI to github pages.

1. Visit Settings>Pages
1. Set source as "Github Actions"
1. Visit Actions>"Test and Build Web UI"
1. Click "Run workflow"

Then, the Web UI will be available in
`https://<your github account>.github.io/<repository name>/` like https://cormoran.github.io/zmk-module-template-with-custom-studio-rpc.

## More Info

For more info on modules, you can read through through the
[Zephyr modules page](https://docs.zephyrproject.org/3.5.0/develop/modules.html)
and [ZMK's page on using modules](https://zmk.dev/docs/features/modules).
[Zephyr's west manifest page](https://docs.zephyrproject.org/3.5.0/develop/west/manifest.html#west-manifests)
may also be of use.
