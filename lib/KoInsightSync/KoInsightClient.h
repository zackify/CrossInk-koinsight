#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "KoInsightEventLog.h"

/**
 * Book fields KoInsight expects in POST /api/plugin/import (mirrors the
 * KOReader statistics database `book` row; md5 is the join key against
 * KOReader-synced books).
 */
struct KoInsightBookPayload {
  std::string md5;
  std::string title;
  std::string authors;
  uint32_t pages = 0;           // Device-specific pagination estimate
  uint32_t lastOpen = 0;        // Unix seconds; 0 = don't update server-side
  uint32_t totalReadTime = 0;   // Cumulative seconds on this device
  uint32_t totalReadPages = 0;  // Cumulative forward page turns on this device
};

/**
 * HTTP client for KoInsight's KOReader-plugin API:
 *   POST /api/plugin/device  {id, model, version}
 *   POST /api/plugin/import  {version, books, stats}
 *
 * The API is unauthenticated and strictly version-gated: `version` must equal
 * the server's REQUIRED_PLUGIN_VERSION (currently 0.3.0) or every request is
 * rejected with 400. Bodies are small JSON documents (stats arrive in batches
 * of at most BATCH_SIZE page events).
 */
class KoInsightClient {
 public:
  enum Error {
    OK = 0,
    NO_SERVER_URL,
    LOW_MEMORY,
    NETWORK_ERROR,
    HTTP_ERROR,
    REJECTED_BY_SERVER,  // 4xx with a server-side error message (e.g. version gate)
  };

  // Must match KoInsight's REQUIRED_PLUGIN_VERSION (sends as `version` in
  // every request; bump when KoInsight's koplugin-router.ts requires it).
  static constexpr const char* PLUGIN_VERSION = "0.3.0";

  // Max page-stat events per import request — keeps bodies in the few-KB
  // range so HTTPS handshakes stay within reachable heap on-device.
  static constexpr size_t BATCH_SIZE = 64;

  /** Stable per-device id, "crossink-" + 12 lowercase hex of the hardware MAC. */
  static std::string deviceId();

  static Error registerDevice(const std::string& baseUrl, const std::string& deviceId, const std::string& model);

  /**
   * Upload one book + a batch of pending page events. Conflict-keyed upserts
   * make retries idempotent, and books/stats rows are namespaced per device
   * server-side so they never clobber KOReader's own rows for the same md5.
   */
  static Error importStats(const std::string& baseUrl, const std::string& deviceId, const KoInsightBookPayload& book,
                           const std::vector<KoInsightPageEvent>& events);

  static std::string errorString(Error error);

  /** HTTP status of the last request (0 on transport failure). */
  static int lastHttpCode;
  /** `error` field from the last non-2xx response body, when it was JSON. */
  static std::string lastServerMessage;
};
