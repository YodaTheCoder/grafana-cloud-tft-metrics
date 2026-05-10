#pragma once

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Grafana Cloud Prometheus query endpoint, not the OTLP ingest endpoint.
// Find it in Grafana Cloud Portal > your stack > Prometheus > Details.
// Example: https://prometheus-prod-24-prod-eu-west-2.grafana.net/api/prom
const char* GRAFANA_PROMETHEUS_BASE_URL = "https://YOUR_PROMETHEUS_INSTANCE.grafana.net/api/prom";

// Use your Grafana Cloud Metrics instance ID as the username and an access policy
// token with metrics:read scope as the password.
const char* GRAFANA_METRICS_USER = "YOUR_METRICS_INSTANCE_ID";
const char* GRAFANA_API_TOKEN = "glc_YOUR_METRICS_READ_TOKEN";

const unsigned long METRIC_REFRESH_MS = 60000;
const unsigned long PAGE_ROTATE_MS = 9000;

static const MetricQueryConfig METRIC_QUERIES[] = {
    {"Agent Turns", "sum(rate(copilot_chat_agent_turn_count_sum[5m]))", "/s", 1.0f},
    {"Tool Calls", "sum(rate(copilot_chat_tool_call_count_total[5m]))", "/s", 1.0f},
    {"Tokens", "sum(rate(gen_ai_client_token_usage_sum[5m]))", "/s", 1.0f},
    {"Sessions", "sum(rate(copilot_chat_session_count_total[5m]))", "/s", 1.0f}
};
