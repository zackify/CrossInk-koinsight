#pragma once
#include <PersistableStore.h>

#include <cstdint>
#include <string>

// Build-time defaults (override in platformio.local.ini). Only used until the
// settings file exists on the device; persisted user settings always win.
#ifndef KOINSIGHT_DEFAULT_SERVER_URL
#define KOINSIGHT_DEFAULT_SERVER_URL ""
#endif
#ifndef KOINSIGHT_DEFAULT_ENABLED
#define KOINSIGHT_DEFAULT_ENABLED 0
#endif

/**
 * Settings for uploading CrossInk reading statistics to a KoInsight server
 * (https://github.com/Ko-Insight/KoInsight) via its KOReader-plugin import
 * API (POST /api/plugin/device + POST /api/plugin/import).
 *
 * The upload itself is piggybacked onto the KOReader progress sync flow
 * (KOReaderSyncActivity): whenever progress sync finishes on WiFi, pending
 * per-page stats for the synced book are pushed as KOReader-compatible
 * page_stat rows so they combine with KOReader stats server-side.
 */
class KoInsightSettings : public PersistableStore<KoInsightSettings> {
 private:
  bool enabled = KOINSIGHT_DEFAULT_ENABLED != 0;
  std::string serverUrl =
      KOINSIGHT_DEFAULT_SERVER_URL;  // Empty = fall back to the KOReader sync server URL at upload time

  KoInsightSettings() = default;
  ~KoInsightSettings() = default;

  friend class PersistableStore<KoInsightSettings>;

 public:
  static const char* getFilePath() { return "/.crosspoint/koinsight.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  void setEnabled(bool on) { enabled = on; }
  bool getEnabled() const { return enabled; }

  void setServerUrl(const std::string& url);
  const std::string& getServerUrl() const { return serverUrl; }

  /**
   * URL the uploader should POST to: the explicit serverUrl when set,
   * otherwise the provided fallback (callers pass the KOReader sync base
   * URL — KoInsight also serves the kosync API, so a combined setup needs
   * no extra configuration). Returns "" when no URL is available at all.
   */
  std::string effectiveServerUrl(const std::string& fallbackUrl) const;
};

// Helper macro to access settings
#define KOINSIGHT_STORE KoInsightSettings::getInstance()
