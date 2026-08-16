#include "KoInsightUpload.h"

#include <Logging.h>

#include "KoInsightEventLog.h"

KoInsightSyncResult KoInsightUpload::syncBookStats(const std::string& baseUrl, const std::string& deviceId,
                                                   const std::string& model, const KoInsightBookPayload& book,
                                                   const std::string& cachePath) {
  KoInsightSyncResult result;

  const size_t pending = KoInsightEventLog::count(cachePath);
  if (pending == 0) {
    LOG_DBG("KNS", "No pending stats for %.8s...; nothing to upload", book.md5.c_str());
    return result;
  }
  result.attempted = true;

  LOG_INF("KNS", "Uploading stats for \"%s\" (%u pending events) to %s", book.title.c_str(),
          static_cast<unsigned>(pending), baseUrl.c_str());

  if (baseUrl.empty()) {
    LOG_ERR("KNS", "KoInsight upload skipped: no server URL (set one or point KOReader sync at KoInsight)");
    result.errorCode = KoInsightClient::NO_SERVER_URL;
    return result;
  }

  // Register up-front so the device shows up in KoInsight even on a first sync.
  const auto deviceError = KoInsightClient::registerDevice(baseUrl, deviceId, model);
  if (deviceError != KoInsightClient::OK) {
    LOG_ERR("KNS", "Device registration failed: %s (http %d)", KoInsightClient::errorString(deviceError).c_str(),
            KoInsightClient::lastHttpCode);
    result.errorCode = deviceError;
    return result;
  }

  for (int batch = 0; batch < MAX_BATCHES_PER_RUN; ++batch) {
    auto events = KoInsightEventLog::load(cachePath, KoInsightClient::BATCH_SIZE);
    if (events.empty()) {
      break;
    }

    // The book's `pages` hint follows the newest event's spine pagination so
    // the server-side per-device row reflects the device's own layout.
    KoInsightBookPayload bookForBatch = book;
    bookForBatch.pages = events.back().totalPages;

    const auto error = KoInsightClient::importStats(baseUrl, deviceId, bookForBatch, events);
    if (error != KoInsightClient::OK) {
      LOG_ERR("KNS", "Stats import failed (batch %d, %u events): %s (http %d)", batch + 1,
              static_cast<unsigned>(events.size()), KoInsightClient::errorString(error).c_str(),
              KoInsightClient::lastHttpCode);
      result.errorCode = error;
      break;
    }

    if (!KoInsightEventLog::consumeFirst(cachePath, events.size())) {
      // The server already accepted these events; if the local consume fails
      // we will re-send them next time (server upserts make that idempotent).
      LOG_ERR("KNS", "Accepted events could not be consumed locally; they will be re-sent harmlessly");
      break;
    }

    result.eventsUploaded += static_cast<int>(events.size());
    LOG_DBG("KNS", "Batch %d uploaded %u events", batch + 1, static_cast<unsigned>(events.size()));
  }

  result.eventsRemaining = static_cast<int>(KoInsightEventLog::count(cachePath));
  LOG_INF("KNS", "Stats sync done: %d uploaded, %d remaining, error=%d (%s)", result.eventsUploaded,
          result.eventsRemaining, result.errorCode,
          result.errorCode ? KoInsightClient::errorString(static_cast<KoInsightClient::Error>(result.errorCode)).c_str()
                           : "ok");
  return result;
}
