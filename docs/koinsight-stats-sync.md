---
title: KoInsight Stats Sync
nav_order: 7.6
---

# KoInsight Stats Sync

CrossInk can upload your reading statistics to a self-hosted [KoInsight](https://github.com/Ko-Insight/KoInsight) server, so reading time on your Xteink shows up in the same dashboard as your KOReader stats. It uses the same import API as KoInsight's own KOReader plugin (`/api/plugin/device` + `/api/plugin/import`), so no extra server-side software is needed.

## How It Works

1. While you read, CrossInk records each page's dwell time in memory (the same dwell data that powers the on-device reading stats).
2. When you leave the reader, those page events are appended to a small queue file next to the book's stats (`koinsight_pending.bin` in the book's cache folder, capped at 256 events).
3. The next time you run **KOReader Sync** for that book (progress sync), the queued events are uploaded to KoInsight while WiFi is on, then removed from the queue. Uploads are idempotent server-side, so retries are harmless.

Books are identified by the binary partial-MD5 of the file content — the same hash KOReader uses in its statistics database — so the same ebook read on both KOReader and CrossInk unifies into a single book in KoInsight, with per-device stats. This works regardless of the **Document Matching** setting used for progress sync.

Each reader registers itself as its own KoInsight device (`crossink-<mac>`), so multiple CrossInk devices don't overwrite each other.

## Setup

1. Open **Settings > KOReader Sync**.
2. Turn on **KoInsight Stats Sync**.
3. Set **KoInsight Server URL** to your KoInsight instance (e.g. `http://192.168.1.10:3005`). If left empty, the KOReader sync server URL is used — handy because KoInsight also serves the kosync API, so a single URL can handle progress and stats.
4. Read, then run KOReader Sync on the book as usual.

You can also pre-provision the settings on the SD card before first boot:

```json
// /.crosspoint/koinsight.json
{
  "enabled": true,
  "serverUrl": "http://your-koinsight-host:3005"
}
```

## Notes and Limitations

- **Page numbers are device-specific.** CrossInk paginates differently than KOReader, so page numbers in stats won't match KOReader's — KoInsight tracks them per device, and reading time (the part that matters) is exact.
- **Timestamps need a clock.** Page events are only recorded when the device clock is set (it syncs over NTP during any WiFi session).
- **Only EPUB reading is captured** for now (the formats KOReader progress sync supports).
- **Stats reset clears the queue.** Resetting a book's stats also discards its not-yet-uploaded events so deleted history isn't resurrected server-side.
- **Version gate.** KoInsight rejects uploads whose plugin version doesn't match the server's expected version (currently `0.3.0`). If your KoInsight server upgrades its required plugin version, stats sync will log a rejection until the firmware is updated.
- Unlike [Reading Stats Sync](./reading-stats-sync.md) (reader-to-reader totals), this syncs per-page reading history to a server and combines with KOReader data.
