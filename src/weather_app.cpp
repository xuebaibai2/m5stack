#include "weather_app.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "generated_config.h"
#include "status_bar.h"

namespace {

constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr uint32_t kConnectPollMs = 250;
constexpr uint32_t kHttpTimeoutMs = 8000;
constexpr uint32_t kAnimationFrameMs = 80;
constexpr int kIconCenterX = 52;
constexpr int kIconCenterY = 76;
constexpr int kIconBoxX = 0;
constexpr int kIconBoxY = 36;
constexpr int kIconBoxW = 116;
constexpr int kIconBoxH = 92;
constexpr int kWeatherValueX = 120;
constexpr size_t kWeatherLocationNameMaxLen = 32;
constexpr size_t kWeatherCoordinateMaxLen = 18;
constexpr size_t kWeatherTimezoneMaxLen = 40;

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

struct WeatherConfig {
  char locationName[kWeatherLocationNameMaxLen] = "";
  char latitude[kWeatherCoordinateMaxLen] = "";
  char longitude[kWeatherCoordinateMaxLen] = "";
  char timezone[kWeatherTimezoneMaxLen] = "";
};

WeatherState weatherState = WeatherState::Idle;
WeatherData weather;
WeatherConfig weatherConfig;
M5Canvas weatherIconCanvas(&M5.Display);
bool wifiStarted = false;
bool appVisible = false;
bool weatherIconCanvasReady = false;
uint32_t stateStartedAt = 0;
uint32_t lastConnectCheckAt = 0;
uint32_t lastAnimationAt = 0;
uint8_t animationFrame = 0;
char weatherError[64] = "";

void copyConfigValue(char* destination, size_t destinationSize,
                     const char* source) {
  if (destinationSize == 0) {
    return;
  }
  strlcpy(destination, source == nullptr ? "" : source, destinationSize);
}

void loadDefaultWeatherConfig() {
  copyConfigValue(weatherConfig.locationName, sizeof(weatherConfig.locationName),
                  kWeatherLocationName);
  copyConfigValue(weatherConfig.latitude, sizeof(weatherConfig.latitude),
                  kWeatherLatitude);
  copyConfigValue(weatherConfig.longitude, sizeof(weatherConfig.longitude),
                  kWeatherLongitude);
  copyConfigValue(weatherConfig.timezone, sizeof(weatherConfig.timezone),
                  kWeatherTimezone);
}

void loadWeatherPreferences() {
  loadDefaultWeatherConfig();

  Preferences preferences;
  if (!preferences.begin("weather", true)) {
    Serial.println("Weather config: using generated defaults");
    return;
  }

  String value = preferences.getString("name", weatherConfig.locationName);
  copyConfigValue(weatherConfig.locationName, sizeof(weatherConfig.locationName),
                  value.c_str());
  value = preferences.getString("lat", weatherConfig.latitude);
  copyConfigValue(weatherConfig.latitude, sizeof(weatherConfig.latitude),
                  value.c_str());
  value = preferences.getString("lon", weatherConfig.longitude);
  copyConfigValue(weatherConfig.longitude, sizeof(weatherConfig.longitude),
                  value.c_str());
  value = preferences.getString("tz", weatherConfig.timezone);
  copyConfigValue(weatherConfig.timezone, sizeof(weatherConfig.timezone),
                  value.c_str());
  preferences.end();

  Serial.printf("Weather config loaded: %s %s,%s %s\n",
                weatherConfig.locationName, weatherConfig.latitude,
                weatherConfig.longitude, weatherConfig.timezone);
}

bool saveWeatherPreferences() {
  Preferences preferences;
  if (!preferences.begin("weather", false)) {
    return false;
  }

  const bool ok = preferences.putString("name", weatherConfig.locationName) > 0 &&
                  preferences.putString("lat", weatherConfig.latitude) > 0 &&
                  preferences.putString("lon", weatherConfig.longitude) > 0 &&
                  preferences.putString("tz", weatherConfig.timezone) > 0;
  preferences.end();
  return ok;
}

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

void drawWeatherShell() {
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.print(weatherConfig.locationName);
  statusBarReset();
  statusBarDraw();
}

void ensureWeatherIconCanvas() {
  if (weatherIconCanvasReady) {
    return;
  }

  weatherIconCanvas.setColorDepth(16);
  weatherIconCanvas.createSprite(kIconBoxW, kIconBoxH);
  weatherIconCanvasReady = true;
}

template <typename Gfx>
void drawSunIcon(Gfx& gfx, int cx, int cy, uint8_t frame) {
  const int pulse = (frame / 6) % 2;
  gfx.fillCircle(cx, cy, 18 + pulse, TFT_YELLOW);
  const int inner = 24 + pulse;
  const int outer = 32 + pulse;
  gfx.drawLine(cx, cy - outer, cx, cy - inner, TFT_YELLOW);
  gfx.drawLine(cx, cy + inner, cx, cy + outer, TFT_YELLOW);
  gfx.drawLine(cx - outer, cy, cx - inner, cy, TFT_YELLOW);
  gfx.drawLine(cx + inner, cy, cx + outer, cy, TFT_YELLOW);
  gfx.drawLine(cx - 23, cy - 23, cx - 17, cy - 17, TFT_ORANGE);
  gfx.drawLine(cx + 17, cy + 17, cx + 23, cy + 23, TFT_ORANGE);
  gfx.drawLine(cx + 17, cy - 17, cx + 23, cy - 23, TFT_ORANGE);
  gfx.drawLine(cx - 23, cy + 23, cx - 17, cy + 17, TFT_ORANGE);
}

template <typename Gfx>
void drawCloudIcon(Gfx& gfx, int cx, int cy, uint8_t frame,
                   uint16_t color = TFT_LIGHTGREY) {
  const uint8_t phase = frame % 24;
  const int drift = phase < 12 ? (static_cast<int>(phase) / 3)
                               : (static_cast<int>(23 - phase) / 3);
  cx += drift;
  gfx.fillCircle(cx - 17, cy + 6, 14, color);
  gfx.fillCircle(cx, cy - 2, 19, color);
  gfx.fillCircle(cx + 20, cy + 7, 13, color);
  gfx.fillRoundRect(cx - 34, cy + 5, 68, 20, 10, color);
}

template <typename Gfx>
void drawRainIcon(Gfx& gfx, int cx, int cy, uint8_t frame) {
  drawCloudIcon(gfx, cx, cy - 8, frame, TFT_LIGHTGREY);
  const int dropOffset = frame % 12;
  for (int i = -24; i <= 24; i += 16) {
    const int y = cy + 22 + dropOffset;
    gfx.drawLine(cx + i, y, cx + i - 4, y + 12, TFT_CYAN);
    gfx.drawLine(cx + i + 1, y, cx + i - 3, y + 12, TFT_CYAN);
  }
}

template <typename Gfx>
void drawWindIcon(Gfx& gfx, int cx, int cy, uint8_t frame) {
  const int shift = frame % 18;
  for (int row = 0; row < 3; ++row) {
    const int y = cy - 18 + row * 22;
    const int x = cx - 44 + ((shift + row * 7) % 18);
    const int width = row == 1 ? 82 : 64;
    gfx.drawFastHLine(x, y, width, TFT_SKYBLUE);
    gfx.drawFastHLine(x + 8, y + 6, width - 26, TFT_SKYBLUE);
    gfx.drawCircle(x + width, y + 3, 6, TFT_SKYBLUE);
  }
}

template <typename Gfx>
void drawWeatherIcon(Gfx& gfx, WeatherIcon icon, int cx, int cy,
                     uint8_t frame) {
  switch (icon) {
    case WeatherIcon::Sunny:
      drawSunIcon(gfx, cx, cy, frame);
      break;
    case WeatherIcon::Cloudy:
      drawCloudIcon(gfx, cx, cy, frame);
      break;
    case WeatherIcon::Rainy:
      drawRainIcon(gfx, cx, cy, frame);
      break;
    case WeatherIcon::Windy:
      drawWindIcon(gfx, cx, cy, frame);
      break;
  }
}

void drawWeatherIconFrame() {
  ensureWeatherIconCanvas();
  weatherIconCanvas.fillSprite(TFT_BLACK);
  drawWeatherIcon(weatherIconCanvas, weather.icon, kIconCenterX - kIconBoxX,
                  kIconCenterY - kIconBoxY, animationFrame);
  weatherIconCanvas.pushSprite(kIconBoxX, kIconBoxY);
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
  M5.Display.setCursor(kWeatherValueX, 42);
  M5.Display.printf("%.0fC", weather.temperatureC);

  M5.Display.setTextSize(2);
  M5.Display.setCursor(kWeatherValueX, 74);
  M5.Display.print(weather.condition);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  M5.Display.setCursor(kWeatherValueX, 104);
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
  String timezone = weatherConfig.timezone;
  timezone.replace("/", "%2F");

  String url = "https://api.open-meteo.com/v1/forecast?";
  url += "latitude=";
  url += weatherConfig.latitude;
  url += "&longitude=";
  url += weatherConfig.longitude;
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
    drawWeatherMessage("Updating", "Fetching weather");
    return;
  }

  weatherState = WeatherState::Connecting;
  drawWeatherMessage("Connecting WiFi", "Please wait");
}

}  // namespace

void weatherAppBegin() {
  loadWeatherPreferences();
  ensureWifiStarted();
}

void weatherAppStart() {
  appVisible = true;
  beginFetch();
}

void weatherAppUpdate() {
  const uint32_t now = millis();
  statusBarUpdate();

  switch (weatherState) {
    case WeatherState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        weatherState = WeatherState::Fetching;
        drawWeatherMessage("Updating", "Fetching weather");
      } else if (now - stateStartedAt >= kConnectTimeoutMs) {
        WiFi.disconnect(false);
        weatherState = WeatherState::Offline;
        drawWeatherMessage("Offline", "WiFi unavailable");
      } else if (now - lastConnectCheckAt >= kConnectPollMs) {
        lastConnectCheckAt = now;
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
  appVisible = false;
}

bool weatherAppWifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool weatherAppApplyRemoteConfig(const char* locationName, const char* latitude,
                                 const char* longitude, const char* timezone) {
  if (locationName == nullptr || latitude == nullptr || longitude == nullptr ||
      timezone == nullptr || strlen(locationName) == 0 ||
      strlen(latitude) == 0 || strlen(longitude) == 0 || strlen(timezone) == 0) {
    snprintf(weatherError, sizeof(weatherError), "Bad weather config");
    Serial.println(weatherError);
    return false;
  }

  copyConfigValue(weatherConfig.locationName, sizeof(weatherConfig.locationName),
                  locationName);
  copyConfigValue(weatherConfig.latitude, sizeof(weatherConfig.latitude),
                  latitude);
  copyConfigValue(weatherConfig.longitude, sizeof(weatherConfig.longitude),
                  longitude);
  copyConfigValue(weatherConfig.timezone, sizeof(weatherConfig.timezone),
                  timezone);

  if (!saveWeatherPreferences()) {
    snprintf(weatherError, sizeof(weatherError), "Save config failed");
    Serial.println(weatherError);
    return false;
  }

  Serial.printf("Weather config saved: %s %s,%s %s\n",
                weatherConfig.locationName, weatherConfig.latitude,
                weatherConfig.longitude, weatherConfig.timezone);
  if (appVisible) {
    beginFetch();
  }
  return true;
}
