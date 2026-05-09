#include "weather_app.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "generated_config.h"

namespace {

constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr uint32_t kConnectPollMs = 250;
constexpr uint32_t kHttpTimeoutMs = 8000;
constexpr uint32_t kAnimationFrameMs = 220;
constexpr int kIconCenterX = 52;
constexpr int kIconCenterY = 76;
constexpr int kIconBoxX = 0;
constexpr int kIconBoxY = 36;
constexpr int kIconBoxW = 100;
constexpr int kIconBoxH = 92;

enum class WeatherState {
  Idle,
  Connecting,
  Fetching,
  Ready,
  Offline,
  Error,
};

enum class WeatherIcon {
  Sunny,
  Cloudy,
  Rainy,
  Windy,
};

struct WeatherData {
  float temperatureC = 0.0f;
  float windKmh = 0.0f;
  int weatherCode = -1;
  const char* condition = "Unknown";
  WeatherIcon icon = WeatherIcon::Cloudy;
};

WeatherState weatherState = WeatherState::Idle;
WeatherData weather;
bool wifiStarted = false;
uint32_t stateStartedAt = 0;
uint32_t lastConnectCheckAt = 0;
uint32_t lastAnimationAt = 0;
uint8_t animationFrame = 0;
char weatherError[64] = "";

bool wifiCredentialsConfigured() {
  return strcmp(kWifiSsid, "YOUR_WIFI_SSID") != 0 &&
         strcmp(kWifiPassword, "YOUR_WIFI_PASSWORD") != 0 &&
         strlen(kWifiSsid) > 0;
}

void ensureWifiStarted() {
  if (wifiStarted || !wifiCredentialsConfigured()) {
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(kWifiSsid, kWifiPassword);
  wifiStarted = true;
  Serial.println("WiFi connection started");
}

void drawWifiIndicator() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  const int x = M5.Display.width() - 48;
  M5.Display.fillCircle(x, 12, 4, connected ? TFT_GREEN : TFT_DARKGREY);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(x + 8, 8);
  M5.Display.print("WiFi");
}

void drawWeatherShell() {
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.print("Sydney");
  drawWifiIndicator();
}

void drawSunIcon(int cx, int cy, uint8_t frame) {
  const int pulse = frame % 2;
  M5.Display.fillCircle(cx, cy, 18 + pulse, TFT_YELLOW);
  const int inner = 24 + pulse;
  const int outer = 32 + pulse;
  M5.Display.drawLine(cx, cy - outer, cx, cy - inner, TFT_YELLOW);
  M5.Display.drawLine(cx, cy + inner, cx, cy + outer, TFT_YELLOW);
  M5.Display.drawLine(cx - outer, cy, cx - inner, cy, TFT_YELLOW);
  M5.Display.drawLine(cx + inner, cy, cx + outer, cy, TFT_YELLOW);
  M5.Display.drawLine(cx - 23, cy - 23, cx - 17, cy - 17, TFT_ORANGE);
  M5.Display.drawLine(cx + 17, cy + 17, cx + 23, cy + 23, TFT_ORANGE);
  M5.Display.drawLine(cx + 17, cy - 17, cx + 23, cy - 23, TFT_ORANGE);
  M5.Display.drawLine(cx - 23, cy + 23, cx - 17, cy + 17, TFT_ORANGE);
}

void drawCloudIcon(int cx, int cy, uint8_t frame,
                   uint16_t color = TFT_LIGHTGREY) {
  const int drift = static_cast<int>(frame % 4) - 1;
  cx += drift;
  M5.Display.fillCircle(cx - 17, cy + 6, 14, color);
  M5.Display.fillCircle(cx, cy - 2, 19, color);
  M5.Display.fillCircle(cx + 20, cy + 7, 13, color);
  M5.Display.fillRoundRect(cx - 34, cy + 5, 68, 20, 10, color);
}

void drawRainIcon(int cx, int cy, uint8_t frame) {
  drawCloudIcon(cx, cy - 8, frame, TFT_LIGHTGREY);
  const int dropOffset = (frame % 3) * 4;
  for (int i = -24; i <= 24; i += 16) {
    const int y = cy + 22 + dropOffset;
    M5.Display.drawLine(cx + i, y, cx + i - 4, y + 12, TFT_CYAN);
    M5.Display.drawLine(cx + i + 1, y, cx + i - 3, y + 12, TFT_CYAN);
  }
}

void drawWindIcon(int cx, int cy, uint8_t frame) {
  const int shift = (frame % 4) * 4;
  for (int row = 0; row < 3; ++row) {
    const int y = cy - 18 + row * 22;
    const int x = cx - 44 + ((shift + row * 7) % 18);
    const int width = row == 1 ? 82 : 64;
    M5.Display.drawFastHLine(x, y, width, TFT_SKYBLUE);
    M5.Display.drawFastHLine(x + 8, y + 6, width - 26, TFT_SKYBLUE);
    M5.Display.drawCircle(x + width, y + 3, 6, TFT_SKYBLUE);
  }
}

void drawWeatherIcon(WeatherIcon icon, int cx, int cy, uint8_t frame) {
  switch (icon) {
    case WeatherIcon::Sunny:
      drawSunIcon(cx, cy, frame);
      break;
    case WeatherIcon::Cloudy:
      drawCloudIcon(cx, cy, frame);
      break;
    case WeatherIcon::Rainy:
      drawRainIcon(cx, cy, frame);
      break;
    case WeatherIcon::Windy:
      drawWindIcon(cx, cy, frame);
      break;
  }
}

void drawWeatherIconFrame() {
  M5.Display.fillRect(kIconBoxX, kIconBoxY, kIconBoxW, kIconBoxH, TFT_BLACK);
  drawWeatherIcon(weather.icon, kIconCenterX, kIconCenterY, animationFrame);
}

WeatherIcon iconForWeather(int code, float windKmh) {
  if (windKmh >= 35.0f && code <= 3) {
    return WeatherIcon::Windy;
  }

  if (code == 0 || code == 1) {
    return WeatherIcon::Sunny;
  }

  if ((code >= 51 && code <= 67) || (code >= 71 && code <= 77) ||
      (code >= 80 && code <= 82) || code >= 95) {
    return WeatherIcon::Rainy;
  }

  return WeatherIcon::Cloudy;
}

const char* conditionForWeather(int code, float windKmh) {
  if (windKmh >= 35.0f && code <= 3) {
    return "Windy";
  }

  switch (code) {
    case 0:
      return "Sunny";
    case 1:
      return "Mostly sunny";
    case 2:
      return "Partly cloudy";
    case 3:
      return "Cloudy";
    case 45:
    case 48:
      return "Fog";
    case 51:
    case 53:
    case 55:
      return "Drizzle";
    case 61:
    case 63:
    case 65:
    case 80:
    case 81:
    case 82:
      return "Rain";
    case 71:
    case 73:
    case 75:
    case 77:
      return "Snow";
    case 95:
    case 96:
    case 99:
      return "Storm";
    default:
      return "Mixed";
  }
}

void drawWeatherReady() {
  drawWeatherShell();
  drawWeatherIconFrame();

  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(102, 42);
  M5.Display.printf("%.0fC", weather.temperatureC);

  M5.Display.setTextSize(2);
  M5.Display.setCursor(102, 74);
  M5.Display.print(weather.condition);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  M5.Display.setCursor(102, 104);
  M5.Display.printf("%.0f km/h", weather.windKmh);
}

void drawWeatherMessage(const char* title, const char* detail) {
  drawWeatherShell();
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(16, 48);
  M5.Display.print(title);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setCursor(16, 78);
  M5.Display.print(detail);
}

String weatherUrl() {
  String timezone = kWeatherTimezone;
  timezone.replace("/", "%2F");

  String url = "https://api.open-meteo.com/v1/forecast?";
  url += "latitude=";
  url += kWeatherLatitude;
  url += "&longitude=";
  url += kWeatherLongitude;
  url += "&current=temperature_2m,wind_speed_10m,weather_code";
  url += "&timezone=";
  url += timezone;
  return url;
}

bool fetchWeather() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);

  const String url = weatherUrl();
  if (!http.begin(client, url)) {
    snprintf(weatherError, sizeof(weatherError), "HTTP begin failed");
    Serial.println(weatherError);
    return false;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    snprintf(weatherError, sizeof(weatherError), "HTTP status %d", httpCode);
    Serial.println(weatherError);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  Serial.printf("Weather payload length: %u\n", payload.length());
  Serial.print("Weather payload prefix: ");
  Serial.println(payload.substring(0, 80));

  if (payload.length() == 0) {
    snprintf(weatherError, sizeof(weatherError), "Empty response");
    Serial.println(weatherError);
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    snprintf(weatherError, sizeof(weatherError), "JSON %s", error.c_str());
    Serial.println(weatherError);
    if (payload[0] != '{') {
      snprintf(weatherError, sizeof(weatherError), "Bad JSON: %.20s",
               payload.c_str());
    }
    return false;
  }

  JsonObject current = doc["current"];
  if (current.isNull()) {
    snprintf(weatherError, sizeof(weatherError), "Missing current");
    Serial.println(weatherError);
    return false;
  }

  weather.temperatureC = current["temperature_2m"] | 0.0f;
  weather.windKmh = current["wind_speed_10m"] | 0.0f;
  weather.weatherCode = current["weather_code"] | -1;
  if (weather.weatherCode < 0) {
    snprintf(weatherError, sizeof(weatherError), "Missing weather_code");
    Serial.println(weatherError);
    return false;
  }

  weather.condition = conditionForWeather(weather.weatherCode, weather.windKmh);
  weather.icon = iconForWeather(weather.weatherCode, weather.windKmh);
  snprintf(weatherError, sizeof(weatherError), "");
  Serial.printf("Weather OK: %.1f C, %.1f km/h, code %d\n",
                weather.temperatureC, weather.windKmh, weather.weatherCode);
  return true;
}

void beginFetch() {
  stateStartedAt = millis();
  lastConnectCheckAt = 0;
  lastAnimationAt = 0;
  animationFrame = 0;

  if (!wifiCredentialsConfigured()) {
    weatherState = WeatherState::Offline;
    drawWeatherMessage("WiFi setup needed", "Set credentials");
    return;
  }

  ensureWifiStarted();

  if (WiFi.status() == WL_CONNECTED) {
    weatherState = WeatherState::Fetching;
    drawWeatherMessage("Updating", "Fetching Sydney weather");
    return;
  }

  weatherState = WeatherState::Connecting;
  drawWeatherMessage("Connecting WiFi", "Please wait");
}

}  // namespace

void weatherAppBegin() {
  ensureWifiStarted();
}

void weatherAppStart() {
  beginFetch();
}

void weatherAppUpdate() {
  const uint32_t now = millis();

  switch (weatherState) {
    case WeatherState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        weatherState = WeatherState::Fetching;
        drawWeatherMessage("Updating", "Fetching Sydney weather");
      } else if (now - stateStartedAt >= kConnectTimeoutMs) {
        WiFi.disconnect(false);
        weatherState = WeatherState::Offline;
        drawWeatherMessage("Offline", "WiFi unavailable");
      } else if (now - lastConnectCheckAt >= kConnectPollMs) {
        lastConnectCheckAt = now;
        drawWeatherMessage("Connecting WiFi", "Please wait");
      }
      break;

    case WeatherState::Fetching:
      weatherState = fetchWeather() ? WeatherState::Ready : WeatherState::Error;
      if (weatherState == WeatherState::Ready) {
        drawWeatherReady();
      } else {
        drawWeatherMessage("Weather error", weatherError);
      }
      break;

    case WeatherState::Ready:
      if (now - lastAnimationAt >= kAnimationFrameMs) {
        lastAnimationAt = now;
        ++animationFrame;
        drawWeatherIconFrame();
      }
      break;

    case WeatherState::Idle:
    case WeatherState::Offline:
    case WeatherState::Error:
      break;
  }
}

void weatherAppRefresh() {
  beginFetch();
}

void weatherAppStop() {
  weatherState = WeatherState::Idle;
}

bool weatherAppWifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}
