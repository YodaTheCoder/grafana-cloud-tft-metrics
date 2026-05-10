#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <math.h>

#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST 4
#define TFT_BL 32

#define TFT_PANEL_W 170
#define TFT_PANEL_H 320
#define HISTORY_POINTS 28
#define MAX_METRICS 8
#define MAX_RANGES 4
#define RANGE_GROUPS 2

#define CLR_BG 0x0000
#define CLR_PANEL 0x18E3
#define CLR_PANEL_2 0x1082
#define CLR_TEXT 0xFFFF
#define CLR_MUTED 0x9CF3
#define CLR_ACCENT 0x07FF
#define CLR_WARN 0xFD20
#define CLR_BAD 0xF800
#define CLR_GOOD 0x07E0
#define CLR_MAGENTA 0xF81F
#define CLR_YELLOW 0xFFE0
#define CLR_ORANGE 0xFD20
#define CLR_BLUE 0x5AEB

struct MetricQueryConfig {
    const char* label;
    const char* promql;
    const char* unit;
    float scale;
};

struct RangeWindow {
    const char* label;
    const char* duration;
};

struct RangeGroup {
    const char* label;
    RangeWindow windows[MAX_RANGES];
    uint8_t headlineRange;
};

#include "config.h"

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
WiFiClientSecure secureClient;

static const size_t configuredMetricCount = sizeof(METRIC_QUERIES) / sizeof(METRIC_QUERIES[0]);
static const size_t metricCount = configuredMetricCount > MAX_METRICS ? MAX_METRICS : configuredMetricCount;
static const RangeGroup QUERY_RANGE_GROUPS[RANGE_GROUPS] = {
    {
        "RECENT",
        {{"1m", "1m"}, {"5m", "5m"}, {"15m", "15m"}, {"1h", "1h"}},
        1
    },
    {
        "LONG",
        {{"1d", "1d"}, {"2d", "2d"}, {"7d", "7d"}, {"14d", "14d"}},
        2
    }
};

float metricValues[MAX_METRICS][RANGE_GROUPS][MAX_RANGES] = {{{0}}};
String metricErrors[MAX_METRICS][RANGE_GROUPS][MAX_RANGES];

unsigned long lastFetch = 0;
unsigned long lastPageSwitch = 0;
size_t pageIndex = 0;
bool lastFetchOk = false;
String statusText = "Booting";
float colorPhase = 0.0f;

uint16_t hsv565(float h, float s, float v) {
    h = fmod(h, 360.0f);
    if (h < 0) {
        h += 360.0f;
    }

    float c = v * s;
    float x = c * (1.0f - fabsf(fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0;
    float g = 0;
    float b = 0;

    if (h < 60) {
        r = c; g = x;
    } else if (h < 120) {
        r = x; g = c;
    } else if (h < 180) {
        g = c; b = x;
    } else if (h < 240) {
        g = x; b = c;
    } else if (h < 300) {
        r = x; b = c;
    } else {
        r = c; b = x;
    }

    return tft.color565((r + m) * 255, (g + m) * 255, (b + m) * 255);
}

String urlEncode(const String& value) {
    const char* hex = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(value.length() * 3);

    for (size_t index = 0; index < value.length(); index++) {
        char c = value.charAt(index);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            encoded += '%';
            encoded += hex[(c >> 4) & 0x0F];
            encoded += hex[c & 0x0F];
        }
    }

    return encoded;
}

String trimBaseUrl(const char* baseUrl) {
    String trimmed = String(baseUrl);
    while (trimmed.endsWith("/")) {
        trimmed.remove(trimmed.length() - 1);
    }
    return trimmed;
}

int screenW() {
    return tft.width();
}

int screenH() {
    return tft.height();
}

bool looksConfigured() {
    return String(WIFI_SSID) != "YOUR_WIFI_SSID" &&
           String(GRAFANA_PROMETHEUS_BASE_URL).indexOf("YOUR_PROMETHEUS_INSTANCE") < 0 &&
           String(GRAFANA_METRICS_USER) != "YOUR_METRICS_INSTANCE_ID" &&
           String(GRAFANA_API_TOKEN).indexOf("YOUR_METRICS_READ_TOKEN") < 0;
}

void drawCentered(const char* line1, const char* line2, uint16_t color) {
    tft.fillScreen(CLR_BG);
    tft.setTextWrap(false);
    tft.setTextColor(color);
    tft.setTextSize(2);
    int16_t x1, y1;
    uint16_t width, height;
    tft.getTextBounds(line1, 0, 0, &x1, &y1, &width, &height);
    tft.setCursor((screenW() - width) / 2, (screenH() / 2) - 20);
    tft.print(line1);

    tft.setTextSize(1);
    tft.setTextColor(CLR_MUTED);
    tft.getTextBounds(line2, 0, 0, &x1, &y1, &width, &height);
    tft.setCursor((screenW() - width) / 2, (screenH() / 2) + 12);
    tft.print(line2);
}

void setupDisplay() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init(TFT_PANEL_W, TFT_PANEL_H);
    tft.setRotation(1);
    tft.setTextWrap(false);
    tft.fillScreen(CLR_BG);
    drawCentered("Grafana", "Cloud metrics", CLR_ACCENT);
}

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    statusText = "Connecting WiFi";
    Serial.print("Connecting to WiFi SSID: ");
    Serial.println(WIFI_SSID);
    drawCentered("WiFi", WIFI_SSID, CLR_ACCENT);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        statusText = "WiFi connected";
        Serial.print("WiFi connected, IP: ");
        Serial.println(WiFi.localIP());
    } else {
        statusText = "WiFi failed";
        Serial.println("WiFi connection failed after 20 seconds");
    }
}

String queryForWindow(const char* promql, const char* duration) {
    String query = String(promql);
    int openBracket = query.lastIndexOf('[');
    int closeBracket = query.indexOf(']', openBracket);
    if (openBracket >= 0 && closeBracket > openBracket) {
        query = query.substring(0, openBracket + 1) + String(duration) + query.substring(closeBracket);
    }
    return query;
}

bool parsePrometheusValue(const String& payload, float& value, String& error) {
    JsonDocument doc;
    DeserializationError jsonError = deserializeJson(doc, payload);
    if (jsonError) {
        error = "Bad JSON";
        return false;
    }

    const char* status = doc["status"] | "";
    if (strcmp(status, "success") != 0) {
        error = doc["error"] | "Query failed";
        return false;
    }

    JsonArray result = doc["data"]["result"].as<JsonArray>();
    if (result.isNull() || result.size() == 0) {
        error = "No series";
        return false;
    }

    JsonVariant rawValue = result[0]["value"][1];
    if (rawValue.isNull()) {
        error = "No value";
        return false;
    }

    if (rawValue.is<const char*>()) {
        value = atof(rawValue.as<const char*>());
    } else {
        value = rawValue.as<float>();
    }
    error = "";
    return isfinite(value);
}

bool fetchMetric(size_t metricIndex, size_t groupIndex, size_t rangeIndex) {
    HTTPClient http;
    String query = queryForWindow(METRIC_QUERIES[metricIndex].promql, QUERY_RANGE_GROUPS[groupIndex].windows[rangeIndex].duration);
    String url = trimBaseUrl(GRAFANA_PROMETHEUS_BASE_URL) + "/api/v1/query?query=" + urlEncode(query);

    Serial.print("Querying metric: ");
    Serial.println(METRIC_QUERIES[metricIndex].label);
    Serial.print("  View: ");
    Serial.println(QUERY_RANGE_GROUPS[groupIndex].label);
    Serial.print("  Window: ");
    Serial.println(QUERY_RANGE_GROUPS[groupIndex].windows[rangeIndex].label);

    if (!http.begin(secureClient, url)) {
        metricErrors[metricIndex][groupIndex][rangeIndex] = "HTTP begin";
        Serial.println("  HTTP begin failed");
        return false;
    }

    http.setAuthorization(GRAFANA_METRICS_USER, GRAFANA_API_TOKEN);
    http.addHeader("Accept", "application/json");
    http.setTimeout(12000);

    int status = http.GET();
    if (status != 200) {
        metricErrors[metricIndex][groupIndex][rangeIndex] = "HTTP " + String(status);
        Serial.print("  Query failed with HTTP ");
        Serial.println(status);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    float parsedValue = 0;
    String parseError;
    if (!parsePrometheusValue(payload, parsedValue, parseError)) {
        metricErrors[metricIndex][groupIndex][rangeIndex] = parseError;
        Serial.print("  Parse failed: ");
        Serial.println(parseError);
        return false;
    }

    metricValues[metricIndex][groupIndex][rangeIndex] = parsedValue * METRIC_QUERIES[metricIndex].scale;
    metricErrors[metricIndex][groupIndex][rangeIndex] = "";
    Serial.print("  Value: ");
    Serial.print(metricValues[metricIndex][groupIndex][rangeIndex], 4);
    Serial.print(" ");
    Serial.println(METRIC_QUERIES[metricIndex].unit);
    return true;
}

void fetchAllMetrics() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }

    if (WiFi.status() != WL_CONNECTED) {
        lastFetchOk = false;
        statusText = "No WiFi";
        return;
    }

    bool allOk = true;
    for (size_t index = 0; index < metricCount; index++) {
        for (size_t group = 0; group < RANGE_GROUPS; group++) {
            for (size_t range = 0; range < MAX_RANGES; range++) {
                if (!fetchMetric(index, group, range)) {
                    allOk = false;
                }
                delay(120);
            }
        }
    }

    lastFetchOk = allOk;
    lastFetch = millis();
    statusText = allOk ? "Updated" : "Partial data";
    Serial.print("Metric refresh complete: ");
    Serial.println(statusText);
}

String fitText(const char* text, uint8_t maxChars) {
    String value = String(text);
    if (value.length() <= maxChars) {
        return value;
    }
    return value.substring(0, maxChars - 1) + ".";
}

void printMetricValue(float value, bool compact) {
    float absValue = fabs(value);
    if (compact && absValue >= 1000) {
        tft.print(value / 1000.0f, 1);
        tft.print("k");
    } else if (absValue >= 100) {
        tft.print(value, 0);
    } else if (absValue >= 1) {
        tft.print(value, 2);
    } else if (absValue >= 0.01f) {
        tft.print(value, 3);
    } else if (absValue > 0.0f) {
        tft.print(value, compact ? 3 : 4);
    } else {
        tft.print("0");
    }
}

void drawHeader() {
    int w = screenW();
    tft.fillRect(0, 0, w, 26, CLR_PANEL);
    tft.drawFastHLine(0, 26, w, hsv565(colorPhase + 180, 0.9f, 1.0f));
    tft.setTextSize(1);
    tft.setTextColor(hsv565(colorPhase, 0.9f, 1.0f), CLR_PANEL);
    tft.setCursor(8, 8);
    tft.print("GRAFANA CLOUD");

    tft.setTextColor(lastFetchOk ? CLR_GOOD : CLR_WARN, CLR_PANEL);
    tft.setCursor(w - 96, 8);
    tft.print(fitText(statusText.c_str(), 15));
}

uint16_t rangeColor(size_t rangeIndex) {
    static const uint16_t colors[MAX_RANGES] = {CLR_MAGENTA, CLR_ACCENT, CLR_YELLOW, CLR_ORANGE};
    return colors[rangeIndex % MAX_RANGES];
}

bool hasRangeValue(size_t metricIndex, size_t groupIndex, size_t rangeIndex) {
    return metricErrors[metricIndex][groupIndex][rangeIndex].length() == 0;
}

float maxRangeValue(size_t metricIndex, size_t groupIndex) {
    float maxValue = 0.0f;
    for (size_t range = 0; range < MAX_RANGES; range++) {
        if (hasRangeValue(metricIndex, groupIndex, range)) {
            maxValue = max(maxValue, metricValues[metricIndex][groupIndex][range]);
        }
    }
    return max(maxValue, 0.0001f);
}

void drawBackground() {
    int w = screenW();
    int h = screenH();
    tft.fillScreen(CLR_BG);
    for (int y = 30; y < h; y += 18) {
        uint16_t color = hsv565(colorPhase + y * 1.8f, 0.7f, 0.18f);
        tft.drawFastHLine(0, y, w, color);
    }
    for (int x = -h; x < w; x += 34) {
        tft.drawLine(x, h - 1, x + h, 30, hsv565(colorPhase + x, 0.8f, 0.12f));
    }
}

void drawRangeTrend(size_t metricIndex, size_t groupIndex, int x, int y, int width, int height) {
    tft.drawRect(x, y, width, height, CLR_PANEL_2);

    float maxValue = maxRangeValue(metricIndex, groupIndex);
    int previousX = x + 6;
    int previousY = y + height - 7;
    bool hasPrevious = false;
    for (size_t range = 0; range < MAX_RANGES; range++) {
        if (!hasRangeValue(metricIndex, groupIndex, range)) {
            continue;
        }

        float normalized = metricValues[metricIndex][groupIndex][range] / maxValue;
        int px = x + 7 + ((width - 14) * range) / (MAX_RANGES - 1);
        int py = y + height - 7 - (int)((height - 14) * normalized);
        if (hasPrevious) {
            tft.drawLine(previousX, previousY, px, py, rangeColor(range));
            tft.drawLine(previousX, previousY + 1, px, py + 1, rangeColor(range));
        }
        tft.fillCircle(px, py, 3, rangeColor(range));
        previousX = px;
        previousY = py;
        hasPrevious = true;
    }
}

void drawRangeCard(size_t metricIndex, size_t groupIndex, size_t rangeIndex, int x, int y, int width, int height) {
    uint16_t accent = rangeColor(rangeIndex);
    tft.fillRect(x, y, width, height, CLR_PANEL);
    tft.drawRect(x, y, width, height, accent);
    tft.drawFastHLine(x + 1, y + 1, width - 2, hsv565(colorPhase + rangeIndex * 70, 0.9f, 1.0f));

    tft.setTextSize(1);
    tft.setTextColor(accent, CLR_PANEL);
    tft.setCursor(x + 5, y + 6);
    tft.print(QUERY_RANGE_GROUPS[groupIndex].windows[rangeIndex].label);

    tft.setTextColor(CLR_TEXT, CLR_PANEL);
    tft.setCursor(x + 5, y + 18);
    if (!hasRangeValue(metricIndex, groupIndex, rangeIndex)) {
        tft.setTextColor(CLR_WARN, CLR_PANEL);
        tft.print(fitText(metricErrors[metricIndex][groupIndex][rangeIndex].c_str(), 9));
        return;
    }

    float value = metricValues[metricIndex][groupIndex][rangeIndex];
    printMetricValue(value, true);

    int barWidth = width - 10;
    int filled = (int)(barWidth * min(1.0f, value / maxRangeValue(metricIndex, groupIndex)));
    tft.fillRect(x + 5, y + height - 7, barWidth, 3, CLR_PANEL_2);
    tft.fillRect(x + 5, y + height - 7, filled, 3, accent);
}

void drawMetricPage(size_t metricIndex, size_t groupIndex) {
    int w = screenW();
    int h = screenH();
    uint16_t metricAccent = hsv565(colorPhase + metricIndex * 62, 0.85f, 1.0f);

    tft.setTextSize(1);
    tft.setTextColor(metricAccent, CLR_BG);
    tft.setCursor(10, 35);
    tft.print(QUERY_RANGE_GROUPS[groupIndex].label);
    tft.print(" METRIC");

    tft.setTextSize(2);
    tft.setTextColor(CLR_TEXT, CLR_BG);
    tft.setCursor(10, 50);
    tft.print(fitText(METRIC_QUERIES[metricIndex].label, 18));

    tft.setTextSize(3);
    tft.setTextColor(metricAccent, CLR_BG);
    tft.setCursor(10, 78);
    uint8_t headlineRange = QUERY_RANGE_GROUPS[groupIndex].headlineRange;
    if (hasRangeValue(metricIndex, groupIndex, headlineRange)) {
        float value = metricValues[metricIndex][groupIndex][headlineRange];
        printMetricValue(value, false);
        tft.setTextSize(1);
        tft.print(" ");
        tft.print(METRIC_QUERIES[metricIndex].unit);
        tft.print(" @");
        tft.print(QUERY_RANGE_GROUPS[groupIndex].windows[headlineRange].label);
    } else {
        tft.setTextSize(2);
        tft.print("waiting...");
    }

    drawRangeTrend(metricIndex, groupIndex, w - 126, 38, 116, 56);

    int gap = 6;
    int cardHeight = 34;
    int cardTop = h - cardHeight - 11;
    int cardWidth = (w - 20 - (gap * (MAX_RANGES - 1))) / MAX_RANGES;
    for (size_t range = 0; range < MAX_RANGES; range++) {
        int x = 10 + range * (cardWidth + gap);
        int y = cardTop;
        drawRangeCard(metricIndex, groupIndex, range, x, y, cardWidth, cardHeight);
    }

    int progressWidth = map(millis() - lastPageSwitch, 0, PAGE_ROTATE_MS, 0, w);
    progressWidth = constrain(progressWidth, 0, w);
    tft.fillRect(0, h - 5, w, 5, CLR_PANEL_2);
    tft.fillRect(0, h - 5, progressWidth, 5, metricAccent);
}

void drawDashboard() {
    colorPhase += 17.0f;
    drawBackground();
    drawHeader();

    if (!looksConfigured()) {
        tft.setTextColor(CLR_WARN, CLR_BG);
        tft.setTextSize(1);
        tft.setCursor(10, 42);
        tft.print("Edit include/config.h");
        tft.setCursor(10, 60);
        tft.print("Add WiFi + Grafana");
        tft.setCursor(10, 78);
        tft.print("metrics:read token");
        return;
    }

    if (metricCount == 0) {
        tft.setTextColor(CLR_WARN, CLR_BG);
        tft.setCursor(10, 62);
        tft.print("No metric queries set");
        return;
    }

    size_t metricIndex = (pageIndex / RANGE_GROUPS) % metricCount;
    size_t groupIndex = pageIndex % RANGE_GROUPS;
    drawMetricPage(metricIndex, groupIndex);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("Grafana Cloud TFT Metrics booting");
    setupDisplay();
    secureClient.setInsecure();

    if (looksConfigured()) {
        connectWiFi();
        fetchAllMetrics();
    } else {
        statusText = "Needs config";
    }
    drawDashboard();
}

void loop() {
    unsigned long now = millis();

    if (looksConfigured() && (lastFetch == 0 || now - lastFetch >= METRIC_REFRESH_MS)) {
        fetchAllMetrics();
        drawDashboard();
    }

    size_t pageCount = metricCount * RANGE_GROUPS;
    if (pageCount > 1 && now - lastPageSwitch >= PAGE_ROTATE_MS) {
        pageIndex = (pageIndex + 1) % pageCount;
        lastPageSwitch = now;
        drawDashboard();
    }

    delay(100);
}
