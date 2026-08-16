#pragma once
#include <string>

#include "KoInsightClient.h"

/**
 * Orchestrates one KoInsight stats upload for a book: registers the device,
 * then drains the book's pending page-event log in bounded batches. Runs
 * while the KOReader progress sync flow has WiFi up; safe to call anytime —
 * failures only keep the queue for the next attempt and are reflected in the
 * result + serial logs.
 */
struct KoInsightSyncResult {
  bool attempted = false;   // false = nothing pending, or disabled
  int eventsUploaded = 0;   // events the server accepted this run
  int eventsRemaining = 0;  // still queued after this run (partial drain)
  int errorCode = 0;        // KoInsightClient::Error of the first failure
};

class KoInsightUpload {
 public:
  // Batches per call, so a long-offline backlog drains over a few syncs
  // without stalling the progress-sync UX on the radio for minutes.
  static constexpr int MAX_BATCHES_PER_RUN = 4;

  /**
   * @param baseUrl   Effective server URL (settings-resolved; no trailing slash)
   * @param deviceId  KoInsightClient::deviceId()
   * @param model     Device model label (e.g. CROSSINK_FIRMWARE_DEVICE_TYPE)
   * @param book      Book identity + cumulative totals for this device
   * @param cachePath Book cache dir; pending events live at
   *                  <cachePath>/koinsight_pending.bin
   */
  static KoInsightSyncResult syncBookStats(const std::string& baseUrl, const std::string& deviceId,
                                           const std::string& model, const KoInsightBookPayload& book,
                                           const std::string& cachePath);
};
