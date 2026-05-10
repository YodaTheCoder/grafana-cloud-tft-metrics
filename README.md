# Grafana Cloud TFT Metrics

PlatformIO firmware for an ESP32 with built-in WiFi and a 170x320 ST7789 TFT screen. It queries Grafana Cloud Metrics through the Prometheus HTTP API and renders a colorful landscape dashboard.

![dashboard](dashboard.png)

## Hardware Defaults

The display setup matches the existing projects in this workspace:

- Board: `esp32dev`
- Display: ST7789, `170x320`
- Rotation: landscape
- SPI pins: MOSI `23`, SCLK `18`
- TFT pins: CS `15`, DC `2`, RST `4`, backlight `32`

## Configure

Edit `include/config.h`:

```cpp
const char* WIFI_SSID = "your wifi name";
const char* WIFI_PASSWORD = "your wifi password";

const char* GRAFANA_PROMETHEUS_BASE_URL = "https://prometheus-prod-XX-prod-REGION.grafana.net/api/prom";
const char* GRAFANA_METRICS_USER = "123456";
const char* GRAFANA_API_TOKEN = "glc_xxx";

const unsigned long METRIC_REFRESH_MS = 60000;
const unsigned long PAGE_ROTATE_MS = 9000;
```

Then tune `METRIC_QUERIES` for the metrics you want on screen:

```cpp
static const MetricQueryConfig METRIC_QUERIES[] = {
    {"Agent Turns", "sum(rate(copilot_chat_agent_turn_count_sum[5m]))", "/s", 1.0f},
    {"Tool Calls", "sum(rate(copilot_chat_tool_call_count_total[5m]))", "/s", 1.0f},
    {"Tokens", "sum(rate(gen_ai_client_token_usage_sum[5m]))", "/s", 1.0f},
    {"Sessions", "sum(rate(copilot_chat_session_count_total[5m]))", "/s", 1.0f}
};

```

The firmware reads instant Prometheus query results. Use PromQL that returns one vector sample. `sum(...)` is useful because it collapses labels into a single value that fits the display.

Each metric has two rotating dashboard views:

- Recent: `1m`, `5m`, `15m`, and `1h`
- Long: `1d`, `2d`, `7d`, and `14d`

The configured PromQL should include a range selector such as `[5m]`; the firmware replaces that selector for each window before querying Grafana Cloud.

The display rotates one metric and one range group at a time. Recent pages use the 5-minute value as the headline. Long pages use the 7-day value as the headline. Very small non-zero values are shown with extra decimal precision so occasional Copilot usage is less likely to round down to `0.00`.

## Grafana Cloud Settings Needed

The `OTEL_EXPORTER_OTLP_HEADERS` environment variable is for writing telemetry into Grafana Cloud via OTLP. This firmware needs read access, so configure the Prometheus query side as well:

1. In Grafana Cloud Portal, open your stack and find the Prometheus or Metrics details.
2. Copy the Prometheus query endpoint ending in `/api/prom`; do not use the OTLP endpoint.
3. Create a Grafana Cloud access policy token with `metrics:read` scope.
4. Use the Metrics instance ID as `GRAFANA_METRICS_USER`.
5. Use the access policy token as `GRAFANA_API_TOKEN`.

If your existing `OTEL_EXPORTER_OTLP_HEADERS` contains an Authorization header for ingest, that token may not have read permission. Create or update an access policy with `metrics:read` for this device.

## Build And Upload

From this folder:

```sh
pio run
pio run -t upload
pio device monitor
```

The first boot screen will remind you to edit `include/config.h` if placeholders are still present.

## Security Notes

The firmware uses HTTPS but currently calls `WiFiClientSecure::setInsecure()` to avoid storing a root certificate on the ESP32. For a more locked-down deployment, replace that with the current root CA for your Grafana Cloud endpoint and call `secureClient.setCACert(...)` in `setup()`.

Treat the metrics read token like a password. Scope it to `metrics:read` only and rotate it if you share or lose the device.
