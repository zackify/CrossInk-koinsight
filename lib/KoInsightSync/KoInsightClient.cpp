#include "KoInsightClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <esp_mac.h>

#include <cstdio>

namespace {
constexpr const char* DEVICE_ENDPOINT = "/api/plugin/device";
constexpr const char* IMPORT_ENDPOINT = "/api/plugin/import";

// Same floors as the KOSync client: a wolfSSL TLS 1.3 handshake needs
// contiguous heap; plain-HTTP servers don't, but gating keeps behavior
// uniform and failures diagnosable instead of flaky.
constexpr uint32_t MIN_FREE_HEAP_FOR_TLS = 35000;
constexpr uint32_t MIN_MAX_ALLOC_HEAP_FOR_TLS = 20000;

bool isHttps(const std::string& url) { return url.rfind("https://", 0) == 0; }

bool insufficientHeapFor(const std::string& url) {
  if (!isHttps(url)) {
    return false;
  }
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_TLS || maxAllocHeap < MIN_MAX_ALLOC_HEAP_FOR_TLS) {
    LOG_ERR("KNS", "Insufficient heap for TLS: %u free (need %u), %u max alloc (need %u)", freeHeap,
            MIN_FREE_HEAP_FOR_TLS, maxAllocHeap, MIN_MAX_ALLOC_HEAP_FOR_TLS);
    return true;
  }
  return false;
}

// Extracts {"error": ...} / {"message": ...} from a response body, if any.
void captureServerMessage(const std::string& body) {
  KoInsightClient::lastServerMessage.clear();
  if (body.empty() || body.front() != '{') {
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return;
  }
  const char* message = doc["error"];
  if (!message) {
    message = doc["message"];
  }
  if (message) {
    KoInsightClient::lastServerMessage = message;
  }
}

KoInsightClient::Error postJson(const char* tag, const std::string& url, const std::string& body) {
  KoInsightClient::lastHttpCode = 0;
  KoInsightClient::lastServerMessage.clear();

  LOG_DBG("KNS", "%s POST %s (%u bytes, heap %u)", tag, url.c_str(), static_cast<unsigned>(body.size()),
          static_cast<unsigned>(ESP.getFreeHeap()));

  if (insufficientHeapFor(url)) {
    return KoInsightClient::LOW_MEMORY;
  }

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("KNS", "%s bad URL: %s", tag, url.c_str());
    return KoInsightClient::NO_SERVER_URL;
  }
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(body);
  KoInsightClient::lastHttpCode = code;
  const std::string& response = http.getString();
  http.end();

  LOG_DBG("KNS", "%s response: HTTP %d (%u bytes)", tag, code, static_cast<unsigned>(response.size()));

  if (code <= 0) {
    return KoInsightClient::NETWORK_ERROR;
  }
  if (code == 200) {
    captureServerMessage(response);
    if (!KoInsightClient::lastServerMessage.empty()) {
      LOG_DBG("KNS", "%s server message: %s", tag, KoInsightClient::lastServerMessage.c_str());
    }
    return KoInsightClient::OK;
  }
  captureServerMessage(response);
  LOG_ERR(
      "KNS", "%s HTTP %d: %s", tag, code,
      KoInsightClient::lastServerMessage.empty() ? "(no server message)" : KoInsightClient::lastServerMessage.c_str());
  return code < 500 ? KoInsightClient::REJECTED_BY_SERVER : KoInsightClient::HTTP_ERROR;
}
}  // namespace

int KoInsightClient::lastHttpCode = 0;
std::string KoInsightClient::lastServerMessage;

std::string KoInsightClient::deviceId() {
  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) {
    LOG_ERR("KNS", "Could not read MAC for device id; using generic id");
    return "crossink-device";
  }
  char id[24];
  snprintf(id, sizeof(id), "crossink-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return std::string(id);
}

KoInsightClient::Error KoInsightClient::registerDevice(const std::string& baseUrl, const std::string& deviceId,
                                                       const std::string& model) {
  if (baseUrl.empty()) {
    return NO_SERVER_URL;
  }
  JsonDocument doc;
  doc["id"] = deviceId;
  doc["model"] = model;
  doc["version"] = PLUGIN_VERSION;
  std::string body;
  serializeJson(doc, body);
  return postJson("RegisterDevice", baseUrl + DEVICE_ENDPOINT, body);
}

KoInsightClient::Error KoInsightClient::importStats(const std::string& baseUrl, const std::string& deviceId,
                                                    const KoInsightBookPayload& book,
                                                    const std::vector<KoInsightPageEvent>& events) {
  if (baseUrl.empty()) {
    return NO_SERVER_URL;
  }

  JsonDocument doc;
  doc["version"] = PLUGIN_VERSION;

  auto bookJson = doc["books"].to<JsonArray>().add<JsonObject>();
  bookJson["id"] = 0;  // Server keys books by md5; the numeric id is ignored.
  bookJson["md5"] = book.md5;
  bookJson["title"] = book.title;
  bookJson["authors"] = book.authors;
  bookJson["notes"] = 0;
  bookJson["last_open"] = book.lastOpen;
  bookJson["highlights"] = 0;
  bookJson["pages"] = book.pages;
  bookJson["series"] = "";
  bookJson["language"] = "";
  bookJson["total_read_time"] = book.totalReadTime;
  bookJson["total_read_pages"] = book.totalReadPages;

  auto statsJson = doc["stats"].to<JsonArray>();
  for (const auto& e : events) {
    auto stat = statsJson.add<JsonObject>();
    stat["page"] = e.page;
    stat["start_time"] = e.startTime;
    stat["duration"] = e.duration;
    stat["total_pages"] = e.totalPages;
    stat["book_md5"] = book.md5;
    stat["device_id"] = deviceId;
  }

  std::string body;
  serializeJson(doc, body);
  if (body.empty()) {
    LOG_ERR("KNS", "ImportStats: empty JSON body (heap %u)", static_cast<unsigned>(ESP.getFreeHeap()));
    return LOW_MEMORY;
  }

  LOG_DBG("KNS", "ImportStats: %u events for book %.8s...", static_cast<unsigned>(events.size()), book.md5.c_str());
  return postJson("ImportStats", baseUrl + IMPORT_ENDPOINT, body);
}

std::string KoInsightClient::errorString(const Error error) {
  switch (error) {
    case OK:
      return "OK";
    case NO_SERVER_URL:
      return "KoInsight server URL is not configured";
    case LOW_MEMORY:
      return "Insufficient memory for stats sync";
    case NETWORK_ERROR:
      return "Network error talking to KoInsight";
    case HTTP_ERROR:
      return "KoInsight server error";
    case REJECTED_BY_SERVER:
      return lastServerMessage.empty() ? "KoInsight rejected the upload" : lastServerMessage;
  }
  return "Unknown KoInsight error";
}
