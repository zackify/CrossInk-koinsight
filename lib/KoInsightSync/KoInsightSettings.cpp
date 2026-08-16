#include "KoInsightSettings.h"

void KoInsightSettings::toJson(JsonDocument& doc) const {
  doc["enabled"] = getEnabled();
  doc["serverUrl"] = getServerUrl();
}

bool KoInsightSettings::fromJson(JsonVariantConst doc) {
  setEnabled(doc["enabled"] | false);
  setServerUrl(doc["serverUrl"] | "");
  return true;
}

void KoInsightSettings::setServerUrl(const std::string& url) {
  serverUrl = url;
  while (!serverUrl.empty() && serverUrl.back() == '/') {
    serverUrl.pop_back();
  }
}

std::string KoInsightSettings::effectiveServerUrl(const std::string& fallbackUrl) const {
  if (!serverUrl.empty()) {
    return serverUrl;
  }
  std::string url = fallbackUrl;
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }
  return url;
}
