#include <Arduino.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <epd/GxEPD2_213_B74.h>

#include <sys/time.h>
#include <time.h>
#include <stdlib.h>

#include "../config.h"

#if !defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE) || \
    !defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL)
#error "PhxTraffic requires the ESP32 built-in full CA certificate bundle."
#endif

#if __has_include("../secrets.h")
#include "../secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef AZ511_API_KEY
#define AZ511_API_KEY PHXTRAFFIC_API_KEY_PLACEHOLDER
#endif

#if !defined(PHXTRAFFIC_DISPLAY_DRIVER_GXEPD2_213_B74)
#error "Select a verified display driver in config.h before building."
#endif

struct TrafficEvent
{
  String title;
  String details;
  uint8_t priority;
};

GxEPD2_213_B74 epaperDriver(PHXTRAFFIC_DISPLAY_CS_PIN,
                            PHXTRAFFIC_DISPLAY_DC_PIN,
                            PHXTRAFFIC_DISPLAY_RST_PIN,
                            PHXTRAFFIC_DISPLAY_BUSY_PIN);
GxEPD2_BW<GxEPD2_213_B74, 16> display(epaperDriver);

TrafficEvent lastGoodEvents[PHXTRAFFIC_MAX_EVENTS];
uint8_t lastGoodEventCount = 0;
bool hasGoodDisplay = false;
bool hasFetched = false;
bool timeSyncStarted = false;
bool localTestMode = false;
uint32_t lastFetchAt = 0;
uint32_t lastWifiAttemptAt = 0;
uint32_t lastPageAt = 0;
uint8_t currentEventIndex = 0;
size_t currentTitleOffset = 0;
size_t currentDetailsOffset = 0;

constexpr uint32_t PAGE_INTERVAL_MS = 15000UL;
constexpr int16_t PAGE_LINE_HEIGHT = 11;
constexpr uint8_t MAX_PAGE_LINES = 16;

JsonVariantConst field(JsonObjectConst object, const char *primary,
                       const char *alternate)
{
  JsonVariantConst value = object[primary];
  return value.isNull() ? object[alternate] : value;
}

String fieldString(JsonObjectConst object, const char *primary,
                   const char *alternate)
{
  JsonVariantConst value = field(object, primary, alternate);
  return value.isNull() ? String() : value.as<String>();
}

time_t parseIsoDate(const String &value)
{
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (sscanf(value.c_str(), "%d-%d-%dT%d:%d:%d",
             &year, &month, &day, &hour, &minute, &second) < 3)
  {
    return 0;
  }

  tm parsed = {};
  parsed.tm_year = year - 1900;
  parsed.tm_mon = month - 1;
  parsed.tm_mday = day;
  parsed.tm_hour = hour;
  parsed.tm_min = minute;
  parsed.tm_sec = second;
  time_t timestamp = mktime(&parsed);
  const char *timePart = strchr(value.c_str(), 'T');
  if (timePart != nullptr)
  {
    const char *zone = strpbrk(timePart + 1, "+-");
    if (zone != nullptr)
    {
      int offsetHours = 0;
      int offsetMinutes = 0;
      if (sscanf(zone + 1, "%d:%d", &offsetHours, &offsetMinutes) >= 1)
      {
        const time_t offset = offsetHours * 3600 + offsetMinutes * 60;
        timestamp += (*zone == '+') ? -offset : offset;
      }
    }
  }
  return timestamp;
}

time_t dateEpoch(JsonVariantConst value)
{
  if (value.isNull())
  {
    return 0;
  }
  if (value.is<const char *>())
  {
    return parseIsoDate(value.as<String>());
  }
  if (value.is<double>() || value.is<long>() || value.is<unsigned long>())
  {
    const double timestamp = value.as<double>();
    return static_cast<time_t>(timestamp > 1000000000000.0
                                   ? timestamp / 1000.0
                                   : timestamp);
  }
  return 0;
}

bool isCurrentEvent(JsonObjectConst event)
{
  JsonVariantConst activeValue = field(event, "IsActive", "isActive");
  if (!activeValue.isNull() && !activeValue.as<bool>())
  {
    return false;
  }

  if (activeValue.isNull())
  {
    String status = fieldString(event, "Status", "status");
    status.toLowerCase();
    if (status == "closed" || status == "expired" ||
        status == "complete" || status == "completed")
    {
      return false;
    }
  }

  const time_t start = dateEpoch(field(event, "StartDate", "startDate"));
  const time_t end = dateEpoch(field(event, "EndDate", "endDate"));
  const time_t now = time(nullptr);
  if (now >= 1600000000)
  {
    if (start > 0 && now < start)
    {
      return false;
    }
    if (end > 0 && now > end)
    {
      return false;
    }
  }
  return true;
}

uint8_t eventPriority(const String &type, const String &description)
{
  String category = type + " " + description;
  category.toLowerCase();
  if (category.indexOf("closure") >= 0 ||
      category.indexOf("closed") >= 0)
  {
    return 0;
  }
  if (category.indexOf("accident") >= 0 ||
      category.indexOf("crash") >= 0 ||
      category.indexOf("incident") >= 0)
  {
    return 1;
  }
  return 2;
}

bool isInPhoenixMetro(JsonObjectConst event)
{
  JsonVariantConst latitude = field(event, "Latitude", "latitude");
  JsonVariantConst longitude = field(event, "Longitude", "longitude");
  if (latitude.isNull() || longitude.isNull())
  {
    return false;
  }

  char *end = nullptr;
  const double lat = latitude.is<const char *>()
                         ? strtod(latitude.as<const char *>(), &end)
                         : latitude.as<double>();
  if (latitude.is<const char *>() &&
      (end == latitude.as<const char *>() || *end != '\0'))
  {
    return false;
  }
  end = nullptr;
  const double lon = longitude.is<const char *>()
                         ? strtod(longitude.as<const char *>(), &end)
                         : longitude.as<double>();
  if (longitude.is<const char *>() &&
      (end == longitude.as<const char *>() || *end != '\0'))
  {
    return false;
  }
  return lat >= PHXTRAFFIC_MIN_LATITUDE &&
         lat <= PHXTRAFFIC_MAX_LATITUDE &&
         lon >= PHXTRAFFIC_MIN_LONGITUDE &&
         lon <= PHXTRAFFIC_MAX_LONGITUDE;
}

void insertEvent(TrafficEvent *events, uint8_t *count,
                 const TrafficEvent &candidate)
{
  uint8_t position = 0;
  while (position < *count &&
         events[position].priority <= candidate.priority)
  {
    ++position;
  }

  if (position >= PHXTRAFFIC_MAX_EVENTS)
  {
    return;
  }

  const uint8_t last = *count < PHXTRAFFIC_MAX_EVENTS
                           ? *count
                           : PHXTRAFFIC_MAX_EVENTS - 1;
  for (int index = last; index > position; --index)
  {
    events[index] = events[index - 1];
  }
  events[position] = candidate;
  if (*count < PHXTRAFFIC_MAX_EVENTS)
  {
    ++(*count);
  }
}

bool parseEvents(JsonDocument &document, TrafficEvent *events,
                 uint8_t *count, bool filterMetro = true,
                 bool filterCurrent = true)
{
  JsonArrayConst array;
  if (document.is<JsonArray>())
  {
    array = document.as<JsonArrayConst>();
  }
  else
  {
    JsonVariantConst result = document["Events"];
    if (result.isNull())
    {
      result = document["events"];
    }
    if (!result.is<JsonArray>())
    {
      return false;
    }
    array = result.as<JsonArrayConst>();
  }

  *count = 0;
  for (JsonObjectConst event : array)
  {
    if ((filterMetro && !isInPhoenixMetro(event)) ||
        (filterCurrent && !isCurrentEvent(event)))
    {
      continue;
    }

    const String type = fieldString(event, "EventType", "eventType");
    const String description = fieldString(event, "Description",
                                          "description");
    const String roadway = fieldString(event, "RoadwayName", "roadwayName");
    const String direction = fieldString(event, "DirectionOfTravel",
                                         "directionOfTravel");
    String title = roadway;
    if (direction.length() > 0)
    {
      title += " ";
      title += direction;
    }
    if (title.length() == 0)
    {
      title = type.length() > 0 ? type : "Traffic event";
    }

    TrafficEvent candidate = {
        title,
        description.length() > 0
            ? description
            : (type.length() > 0 ? type : "No details provided."),
        eventPriority(type, description)};
    insertEvent(events, count, candidate);
  }
  return true;
}

String nextWrappedLine(const String &text, size_t &offset, int16_t maxWidth)
{
  while (offset < text.length() && isspace(text[offset]))
  {
    ++offset;
  }
  if (offset >= text.length())
  {
    return String();
  }

  const size_t start = offset;
  const size_t noSpace = static_cast<size_t>(-1);
  size_t lastSpace = noSpace;
  size_t end = start;
  for (; end < text.length(); ++end)
  {
    if (isspace(text[end]))
    {
      lastSpace = end;
    }

    String candidate = text.substring(start, end + 1);
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    display.getTextBounds(candidate.c_str(), 0, 0, &x1, &y1,
                          &width, &height);
    if (width > maxWidth)
    {
      if (end == start)
      {
        ++end;
      }
      else if (lastSpace != noSpace && lastSpace > start)
      {
        end = lastSpace;
      }
      break;
    }
  }

  if (end > text.length())
  {
    end = text.length();
  }
  if (end == start)
  {
    end = start + 1;
  }

  String line = text.substring(start, end);
  offset = end;
  return line;
}

bool loadTestEvents()
{
  static const char testEventJson[] = R"json(
[
   {
    "ID": 2,
    "SourceId": "347507",
    "Organization": "Tucson",
    "RoadwayName": "AJO WASH BRIDGE CLOSURE, AJO ARIZONA, TUESDAY, MARCH 26",
    "DirectionOfTravel": null,
    "Description": "AJO WASH BRIDGE CLOSURE, AJO ARIZONA, TUESDAY, MARCH 26 - Traffic control devices and signage are on-site.  Please approach the area with caution and obey all traffic control devices."
  },
  {
    "ID": 3,
    "SourceId": "359004",
    "Organization": "Tucson",
    "RoadwayName": "TWIN PEAKS ROAD, RATTLESNAKE PASS WIDENING PROJECT",
    "DirectionOfTravel": null,
    "Description": "TWIN PEAKS ROAD, RATTLESNAKE PASS WIDENING PROJECT - The speed limit will be reduced for the duration of the project."
  }
]
)json";

  JsonDocument document;
  const DeserializationError parseError =
      deserializeJson(document, testEventJson);
  if (parseError)
  {
    Serial.printf("Local test event parsing failed: %s\n", parseError.c_str());
    return false;
  }

  TrafficEvent testEvents[PHXTRAFFIC_MAX_EVENTS];
  uint8_t testEventCount = 0;
  if (!parseEvents(document, testEvents, &testEventCount, false, false) ||
      testEventCount == 0)
  {
    Serial.println("Local test event data did not contain a displayable event.");
    return false;
  }

  for (uint8_t i = 0; i < testEventCount; ++i)
  {
    lastGoodEvents[i] = testEvents[i];
  }
  lastGoodEventCount = testEventCount;
  return true;
}

void drawCar(int16_t x, int16_t y)
{
  display.drawLine(0, y + 20, display.width(), y + 20, GxEPD_BLACK);
  display.drawLine(0, y + 25, display.width(), y + 25, GxEPD_BLACK);
  display.fillRoundRect(x, y + 7, 28, 12, 3, GxEPD_BLACK);
  display.fillRect(x + 7, y + 2, 14, 8, GxEPD_BLACK);
  display.fillRect(x + 10, y + 4, 8, 4, GxEPD_WHITE);
  display.fillCircle(x + 7, y + 20, 3, GxEPD_BLACK);
  display.fillCircle(x + 22, y + 20, 3, GxEPD_BLACK);
}

void showFetchingAnimation()
{
  const int16_t stripHeight = 32;
  const int16_t top = display.height() - stripHeight;
  const int16_t positions[] = {8, 44, 80};
  for (uint8_t frame = 0; frame < 3; ++frame)
  {
    display.setPartialWindow(0, top, display.width(), stripHeight);
    display.firstPage();
    do
    {
      display.fillRect(0, top, display.width(), stripHeight, GxEPD_WHITE);
      drawCar(positions[frame], top + 1);
    }
    while (display.nextPage());
    delay(140);
  }
}

void renderStartup()
{
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(nullptr);
    display.setCursor(4, 18);
    display.print("PHOENIX TRAFFIC");
    display.setCursor(4, 40);
    display.print("Starting...");
  }
  while (display.nextPage());
}

uint16_t textPixelWidth(const String &text)
{
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  display.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &width, &height);
  return width;
}

void appendWrappedSection(const String &label, const String &text,
                          size_t &offset, String *lines, uint8_t &lineCount,
                          uint8_t maxLines, int16_t maxWidth)
{
  bool firstLine = true;
  while (lineCount < maxLines && offset < text.length())
  {
    String prefix;
    int16_t availableWidth = maxWidth;
    if (firstLine)
    {
      prefix = label + (offset == 0 ? ": " : " (cont.): ");
      availableWidth -= textPixelWidth(prefix);
      if (availableWidth < 1)
      {
        prefix = String();
        availableWidth = maxWidth;
      }
    }

    String line = nextWrappedLine(text, offset, availableWidth);
    if (line.length() == 0)
    {
      break;
    }
    lines[lineCount++] = prefix + line;
    firstLine = false;
  }
}

void renderCurrentEventPage()
{
  if (lastGoodEventCount == 0)
  {
    display.setFullWindow();
    display.firstPage();
    do
    {
      display.fillScreen(GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);
      display.setFont(nullptr);
      display.setCursor(4, 10);
      display.print("PHOENIX TRAFFIC");
      display.drawLine(0, 14, display.width(), 14, GxEPD_BLACK);
      display.setCursor(4, 31);
      display.print("No current metro events");
      display.setCursor(4, 44);
      display.print("Next check in 5 minutes");
    }
    while (display.nextPage());
    hasGoodDisplay = true;
    lastPageAt = millis();
    return;
  }

  if (currentEventIndex >= lastGoodEventCount)
  {
    currentEventIndex = 0;
    currentTitleOffset = 0;
    currentDetailsOffset = 0;
  }

  const TrafficEvent &event = lastGoodEvents[currentEventIndex];
  const String heading = localTestMode
                             ? "AZ 511 TEST DATA"
                             : "PHOENIX EVENT";
  const int16_t maxWidth = display.width() - 8;
  const uint8_t maxLines = min(
      static_cast<uint8_t>(MAX_PAGE_LINES),
      static_cast<uint8_t>((display.height() - 40) / PAGE_LINE_HEIGHT));
  String pageLines[MAX_PAGE_LINES];
  uint8_t pageLineCount = 0;

  display.setFont(nullptr);
  appendWrappedSection("Roadway", event.title, currentTitleOffset,
                       pageLines, pageLineCount, maxLines, maxWidth);
  if (currentTitleOffset >= event.title.length())
  {
    appendWrappedSection("Details", event.details, currentDetailsOffset,
                         pageLines, pageLineCount, maxLines, maxWidth);
  }

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(nullptr);
    display.setCursor(4, 10);
    display.print(heading);
    display.setCursor(display.width() - 62, 10);
    display.print(String(currentEventIndex + 1) + "/" +
                  String(lastGoodEventCount));
    display.drawLine(0, 14, display.width(), 14, GxEPD_BLACK);

    for (uint8_t i = 0; i < pageLineCount; ++i)
    {
      display.setCursor(4, 27 + static_cast<int16_t>(i) * PAGE_LINE_HEIGHT);
      display.print(pageLines[i]);
    }
    display.setCursor(4, display.height() - 3);
    display.print("Auto-advance 15s");
  }
  while (display.nextPage());

  hasGoodDisplay = true;
  lastPageAt = millis();
}

void renderEvents(const TrafficEvent *events, uint8_t count, bool testMode = false)
{
  for (uint8_t i = 0; i < count; ++i)
  {
    lastGoodEvents[i] = events[i];
  }
  lastGoodEventCount = count;
  localTestMode = testMode;
  currentEventIndex = 0;
  currentTitleOffset = 0;
  currentDetailsOffset = 0;
  renderCurrentEventPage();
}

String urlEncode(const String &value)
{
  const char hex[] = "0123456789ABCDEF";
  String encoded;
  for (size_t i = 0; i < value.length(); ++i)
  {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~')
    {
      encoded += static_cast<char>(c);
    }
    else
    {
      encoded += '%';
      encoded += hex[c >> 4];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

bool hasConfiguredApiKey()
{
  return strlen(AZ511_API_KEY) > 0 &&
         strcmp(AZ511_API_KEY, PHXTRAFFIC_API_KEY_PLACEHOLDER) != 0;
}

bool fetchEvents()
{
  showFetchingAnimation();

  WiFiClientSecure tlsClient;
  tlsClient.useBuiltinCACertBundle();

  HTTPClient http;
  const String url = String(PHXTRAFFIC_AZ511_ENDPOINT) +
                     "?key=" + urlEncode(AZ511_API_KEY);
  if (!http.begin(tlsClient, url))
  {
    Serial.println("Unable to start AZ 511 request.");
    return false;
  }
  http.useHTTP10(true);
  http.addHeader("Accept", "application/json");

  const int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK)
  {
    Serial.printf("AZ 511 request failed with HTTP status %d.\n", statusCode);
    http.end();
    return false;
  }
  JsonDocument document;
  const DeserializationError parseError =
      deserializeJson(document, http.getStream(),
                      DeserializationOption::NestingLimit(12));
  http.end();
  if (parseError)
  {
    Serial.printf("AZ 511 JSON parsing failed: %s\n", parseError.c_str());
    return false;
  }

  TrafficEvent parsedEvents[PHXTRAFFIC_MAX_EVENTS];
  uint8_t parsedCount = 0;
  if (!parseEvents(document, parsedEvents, &parsedCount))
  {
    Serial.println("AZ 511 response did not contain an event array.");
    return false;
  }

  for (uint8_t i = 0; i < parsedCount; ++i)
  {
    lastGoodEvents[i] = parsedEvents[i];
  }
  lastGoodEventCount = parsedCount;
  renderEvents(lastGoodEvents, lastGoodEventCount);
  Serial.printf("Displayed %u Phoenix metro event(s).\n", lastGoodEventCount);
  return true;
}

void setup()
{
  Serial.begin(115200);
  setenv("TZ", "UTC0", 1);
  tzset();

  SPI.begin(PHXTRAFFIC_SPI_SCK_PIN, PHXTRAFFIC_SPI_MISO_PIN,
            PHXTRAFFIC_SPI_MOSI_PIN, PHXTRAFFIC_DISPLAY_CS_PIN);
  display.init(115200);
  display.setRotation(PHXTRAFFIC_DISPLAY_ROTATION);
  renderStartup();

  if (!hasConfiguredApiKey())
  {
    showFetchingAnimation();
    if (loadTestEvents())
    {
      renderEvents(lastGoodEvents, lastGoodEventCount, true);
      Serial.println("AZ 511 key unset/placeholder; displaying local sample data.");
    }
    return;
  }

  if (strlen(WIFI_SSID) == 0)
  {
    Serial.println("Set Wi-Fi credentials in the ignored secrets.h file.");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttemptAt = millis();
}

void loop()
{
  if (!hasConfiguredApiKey())
  {
    if (lastGoodEventCount == 0)
    {
      delay(1000);
      return;
    }
    if (hasGoodDisplay && millis() - lastPageAt >= PAGE_INTERVAL_MS)
    {
      if (currentTitleOffset >= lastGoodEvents[currentEventIndex].title.length() &&
          currentDetailsOffset >= lastGoodEvents[currentEventIndex].details.length())
      {
        currentEventIndex = (currentEventIndex + 1) % lastGoodEventCount;
        currentTitleOffset = 0;
        currentDetailsOffset = 0;
      }
      renderCurrentEventPage();
    }
    delay(100);
    return;
  }

  const uint32_t now = millis();
  if (strlen(WIFI_SSID) > 0 && WiFi.status() != WL_CONNECTED &&
      now - lastWifiAttemptAt >= 30000UL)
  {
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWifiAttemptAt = now;
  }

  if (WiFi.status() == WL_CONNECTED && !timeSyncStarted)
  {
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    timeSyncStarted = true;
    Serial.println("Waiting for UTC clock sync before verified TLS.");
  }

  if (WiFi.status() == WL_CONNECTED && time(nullptr) >= 1600000000 &&
      (!hasFetched || now - lastFetchAt >= PHXTRAFFIC_REFRESH_INTERVAL_MS))
  {
    hasFetched = true;
    lastFetchAt = now;
    if (!fetchEvents() && hasGoodDisplay)
    {
      renderCurrentEventPage();
    }
  }
  if (lastGoodEventCount > 0 && millis() - lastPageAt >= PAGE_INTERVAL_MS)
  {
    if (currentTitleOffset >= lastGoodEvents[currentEventIndex].title.length() &&
        currentDetailsOffset >= lastGoodEvents[currentEventIndex].details.length())
    {
      currentEventIndex = (currentEventIndex + 1) % lastGoodEventCount;
      currentTitleOffset = 0;
      currentDetailsOffset = 0;
    }
    renderCurrentEventPage();
  }
  delay(100);
}
