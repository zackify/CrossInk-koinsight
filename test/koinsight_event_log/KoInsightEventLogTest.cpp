#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "KoInsightEventLog.h"

// Verifies the wire format of KoInsightEventLog's pure codec. The exact byte
// layout matters: the firmware reads these records back byte-for-byte when
// draining the queue, and the documented layout is little-endian explicitly
// so host tests on any architecture assert the same bytes.

TEST(KoInsightEventLogCodec, EncodesEventAsLittleEndianRecord) {
  KoInsightPageEvent event;
  event.startTime = 0x12345678;
  event.duration = 0x9ABCDEF0;
  event.page = 0x01020304;
  event.totalPages = 0xA0B0C0D0;

  uint8_t buf[KoInsightEventLog::RECORD_SIZE] = {};
  KoInsightEventLog::encodeEvent(buf, event);

  // startTime LE
  EXPECT_EQ(buf[0], 0x78);
  EXPECT_EQ(buf[1], 0x56);
  EXPECT_EQ(buf[2], 0x34);
  EXPECT_EQ(buf[3], 0x12);
  // duration LE
  EXPECT_EQ(buf[4], 0xF0);
  EXPECT_EQ(buf[5], 0xDE);
  EXPECT_EQ(buf[6], 0xBC);
  EXPECT_EQ(buf[7], 0x9A);
  // page LE
  EXPECT_EQ(buf[8], 0x04);
  EXPECT_EQ(buf[9], 0x03);
  EXPECT_EQ(buf[10], 0x02);
  EXPECT_EQ(buf[11], 0x01);
  // totalPages LE
  EXPECT_EQ(buf[12], 0xD0);
  EXPECT_EQ(buf[13], 0xC0);
  EXPECT_EQ(buf[14], 0xB0);
  EXPECT_EQ(buf[15], 0xA0);
}

TEST(KoInsightEventLogCodec, RoundTripsEvent) {
  KoInsightPageEvent event;
  event.startTime = 1752345678;
  event.duration = 137;
  event.page = 42;
  event.totalPages = 311;

  uint8_t buf[KoInsightEventLog::RECORD_SIZE] = {};
  KoInsightEventLog::encodeEvent(buf, event);
  const KoInsightPageEvent decoded = KoInsightEventLog::decodeEvent(buf);

  EXPECT_EQ(decoded.startTime, event.startTime);
  EXPECT_EQ(decoded.duration, event.duration);
  EXPECT_EQ(decoded.page, event.page);
  EXPECT_EQ(decoded.totalPages, event.totalPages);
}

TEST(KoInsightEventLogCodec, RecordFitsDocumentedLayout) {
  EXPECT_EQ(KoInsightEventLog::RECORD_SIZE, 16u);
  EXPECT_EQ(KoInsightEventLog::HEADER_SIZE, 8u);
  // A full log stays small: it is read-modify-written on every change, so it
  // must never grow beyond a few tens of KB.
  EXPECT_LE(KoInsightEventLog::MAX_EVENTS * KoInsightEventLog::RECORD_SIZE + KoInsightEventLog::HEADER_SIZE,
            32u * 1024u);
}
