#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * Per-book queue of pending KOReader-style page statistics waiting to be
 * uploaded to a KoInsight server. CrossInk readers record each qualifying
 * page's dwell time into RAM; on reader exit the events are appended to
 * <cachePath>/koinsight_pending.bin (this log). The KOReader progress sync
 * flow later uploads and consumes them.
 *
 * On-disk layout (all little-endian):
 *   header  8 bytes:  magic "KIPQ" [0-3], version u16 [4-5], reserved u16 [6-7]
 *   record 16 bytes:  startTime u32 [0-3]  — unix seconds when the page dwell began
 *                     duration  u32 [4-7]  — seconds spent on the page
 *                     page      u32 [8-11] — 1-based page within the current spine
 *                     totalPages u32 [12-15] — spine page count (device pagination)
 *
 * The whole file is read-modify-written on change (HalStorage's write mode
 * truncates; there is no append mode). Sizes are tiny: 256 records = 4KB.
 */
struct KoInsightPageEvent {
  uint32_t startTime;
  uint32_t duration;
  uint32_t page;
  uint32_t totalPages;
};

class KoInsightEventLog {
 public:
  static constexpr size_t MAX_EVENTS = 256;
  static constexpr const char* FILE_NAME = "koinsight_pending.bin";

  // Byte-level codec — pure functions, unit-tested on host.
  static constexpr size_t HEADER_SIZE = 8;
  static constexpr size_t RECORD_SIZE = 16;
  static void encodeEvent(uint8_t* out, const KoInsightPageEvent& event) {
    writeLe32(out + 0, event.startTime);
    writeLe32(out + 4, event.duration);
    writeLe32(out + 8, event.page);
    writeLe32(out + 12, event.totalPages);
  }
  static KoInsightPageEvent decodeEvent(const uint8_t* in) {
    return {readLe32(in + 0), readLe32(in + 4), readLe32(in + 8), readLe32(in + 12)};
  }

 private:
  static void writeLe32(uint8_t* data, const uint32_t value) {
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    data[2] = (value >> 16) & 0xFF;
    data[3] = (value >> 24) & 0xFF;
  }
  static uint32_t readLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
  }

 public:
  /**
   * Appends events to the log, creating the file if needed. When the result
   * would exceed MAX_EVENTS, the oldest events are dropped (freshness beats
   * completeness for a stats queue that is expected to drain on every sync).
   * Returns false on storage errors.
   */
  static bool appendAll(const std::string& cachePath, const std::vector<KoInsightPageEvent>& events);

  /** Reads up to maxEvents events (oldest first). Empty when no log exists. */
  static std::vector<KoInsightPageEvent> load(const std::string& cachePath, size_t maxEvents = MAX_EVENTS);

  /** Removes the first `count` events after a successful upload. */
  static bool consumeFirst(const std::string& cachePath, size_t count);

  /** Deletes the log (used when book stats are reset). Missing file = success. */
  static bool remove(const std::string& cachePath);

  /** Number of pending events. */
  static size_t count(const std::string& cachePath);
};
