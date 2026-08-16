#include "KoInsightEventLog.h"

#include <HalStorage.h>
#include <Logging.h>

#include <vector>

namespace {
constexpr uint8_t VERSION = 1;
const char MAGIC[4] = {'K', 'I', 'P', 'Q'};

void writeLe16(uint8_t* data, uint16_t value) {
  data[0] = value & 0xFF;
  data[1] = (value >> 8) & 0xFF;
}

uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

std::string logPath(const std::string& cachePath) { return cachePath + "/" + KoInsightEventLog::FILE_NAME; }

// Reads the whole log file (minus header) into `events` (newest at the back).
// A missing file yields an empty vector; a corrupt one is treated as a fresh log.
std::vector<KoInsightPageEvent> readAll(const std::string& cachePath) {
  std::vector<KoInsightPageEvent> events;
  const std::string path = logPath(cachePath);
  if (!Storage.exists(path.c_str())) {
    return events;
  }
  FsFile f;
  if (!Storage.openFileForRead("KNS", path, f)) {
    LOG_ERR("KNS", "Could not open %s for reading", KoInsightEventLog::FILE_NAME);
    return events;
  }
  const size_t size = f.fileSize();
  uint8_t header[KoInsightEventLog::HEADER_SIZE];
  bool headerOk = size >= KoInsightEventLog::HEADER_SIZE &&
                  f.read(header, KoInsightEventLog::HEADER_SIZE) == static_cast<int>(KoInsightEventLog::HEADER_SIZE) &&
                  header[0] == MAGIC[0] && header[1] == MAGIC[1] && header[2] == MAGIC[2] && header[3] == MAGIC[3] &&
                  readLe16(header + 4) == VERSION;
  if (!headerOk) {
    f.close();
    LOG_ERR("KNS", "%s corrupt (size=%u); starting fresh", KoInsightEventLog::FILE_NAME, static_cast<unsigned>(size));
    return events;
  }
  const size_t payloadBytes = size - KoInsightEventLog::HEADER_SIZE;
  // Cap the allocation: a wayward file must not trigger a huge read.
  if (payloadBytes > KoInsightEventLog::MAX_EVENTS * KoInsightEventLog::RECORD_SIZE * 4) {
    f.close();
    LOG_ERR("KNS", "%s unexpectedly large (%u bytes); starting fresh", KoInsightEventLog::FILE_NAME,
            static_cast<unsigned>(payloadBytes));
    return events;
  }
  const size_t recordCount = payloadBytes / KoInsightEventLog::RECORD_SIZE;
  events.reserve(recordCount);
  std::vector<uint8_t> buf(payloadBytes);
  const int read = f.read(buf.data(), payloadBytes);
  f.close();
  if (read < 0) {
    LOG_ERR("KNS", "Short read on %s", KoInsightEventLog::FILE_NAME);
    events.clear();
    return events;
  }
  for (size_t i = 0; i < static_cast<size_t>(read) / KoInsightEventLog::RECORD_SIZE; ++i) {
    const KoInsightPageEvent e = KoInsightEventLog::decodeEvent(buf.data() + i * KoInsightEventLog::RECORD_SIZE);
    if (e.duration == 0 || e.startTime == 0 || e.page == 0) {
      LOG_DBG("KNS", "Dropping malformed event in %s", KoInsightEventLog::FILE_NAME);
      continue;
    }
    events.push_back(e);
  }
  return events;
}

bool writeAll(const std::string& cachePath, const std::vector<KoInsightPageEvent>& events) {
  if (events.empty()) {
    return KoInsightEventLog::remove(cachePath);
  }
  FsFile f;
  if (!Storage.openFileForWrite("KNS", logPath(cachePath), f)) {
    LOG_ERR("KNS", "Could not write %s", KoInsightEventLog::FILE_NAME);
    return false;
  }
  uint8_t header[KoInsightEventLog::HEADER_SIZE];
  header[0] = MAGIC[0];
  header[1] = MAGIC[1];
  header[2] = MAGIC[2];
  header[3] = MAGIC[3];
  writeLe16(header + 4, VERSION);
  writeLe16(header + 6, 0);
  bool ok = f.write(header, sizeof(header)) == sizeof(header);
  uint8_t record[KoInsightEventLog::RECORD_SIZE];
  if (ok) {
    for (const auto& e : events) {
      KoInsightEventLog::encodeEvent(record, e);
      if (f.write(record, sizeof(record)) != sizeof(record)) {
        ok = false;
        break;
      }
    }
  }
  f.close();
  if (!ok) {
    LOG_ERR("KNS", "Truncated write to %s", KoInsightEventLog::FILE_NAME);
  }
  return ok;
}
}  // namespace

bool KoInsightEventLog::appendAll(const std::string& cachePath, const std::vector<KoInsightPageEvent>& newEvents) {
  if (newEvents.empty()) {
    return true;
  }
  std::vector<KoInsightPageEvent> events = readAll(cachePath);
  events.insert(events.end(), newEvents.begin(), newEvents.end());
  if (events.size() > MAX_EVENTS) {
    const size_t dropped = events.size() - MAX_EVENTS;
    LOG_INF("KNS", "%s over capacity, dropping %u oldest events", FILE_NAME, static_cast<unsigned>(dropped));
    events.erase(events.begin(), events.begin() + dropped);
  }
  LOG_DBG("KNS", "Queued %u event(s), %u pending in %s", static_cast<unsigned>(newEvents.size()),
          static_cast<unsigned>(events.size()), FILE_NAME);
  return writeAll(cachePath, events);
}

std::vector<KoInsightPageEvent> KoInsightEventLog::load(const std::string& cachePath, const size_t maxEvents) {
  std::vector<KoInsightPageEvent> events = readAll(cachePath);
  if (events.size() > maxEvents) {
    events.resize(maxEvents);
  }
  return events;
}

bool KoInsightEventLog::consumeFirst(const std::string& cachePath, const size_t count) {
  if (count == 0) {
    return true;
  }
  std::vector<KoInsightPageEvent> events = readAll(cachePath);
  if (count >= events.size()) {
    return remove(cachePath);
  }
  events.erase(events.begin(), events.begin() + count);
  return writeAll(cachePath, events);
}

bool KoInsightEventLog::remove(const std::string& cachePath) {
  const std::string path = logPath(cachePath);
  if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
    LOG_ERR("KNS", "Could not delete %s", FILE_NAME);
    return false;
  }
  return true;
}

size_t KoInsightEventLog::count(const std::string& cachePath) {
  const std::string path = logPath(cachePath);
  if (!Storage.exists(path.c_str())) {
    return 0;
  }
  FsFile f;
  if (!Storage.openFileForRead("KNS", path, f)) {
    return 0;
  }
  const size_t size = f.fileSize();
  f.close();
  if (size < HEADER_SIZE) {
    return 0;
  }
  return (size - HEADER_SIZE) / RECORD_SIZE;
}
