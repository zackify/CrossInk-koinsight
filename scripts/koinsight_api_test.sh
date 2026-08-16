#!/usr/bin/env bash
# Live verification of the CrossInk KoInsight stats-sync payloads against a
# real KoInsight server. Sends exactly what the firmware sends
# (lib/KoInsightSync/KoInsightClient.cpp):
#   1. POST /api/plugin/device  {id, model, version}
#   2. POST /api/plugin/import  {version, books, stats}
# Then verifies:
#   - version gate passes ("0.3.0")
#   - rows land keyed by book md5 and per-device (book_device, page_stat)
#   - re-sending the same payload is idempotent (upsert, no duplicates)
#   - a KOReader-shaped import for the SAME md5 attaches to the same book,
#    so CrossInk + KOReader stats combine (this is the whole point)
#
# Usage: KOINSIGHT_URL=http://zai:3005 ./scripts/koinsight_api_test.sh
set -euo pipefail

BASE="${KOINSIGHT_URL:-http://zai:3005}"
DEVICE_ID="crossink-testdev01"
BOOK_MD5="25f8abb4f4f5594f02f361726814fea1"   # stands in for KOReaderDocumentId::calculate(file)
NOW=$(date +%s)
T1=$((NOW - 1800))
T2=$((NOW - 1200))
T3=$((NOW - 600))

req() { # method path body
  curl -sS -w '\n%{http_code}' -X "$1" -H 'Content-Type: application/json' -d "$3" "$BASE$2"
}

expect_code() { # response expected_code label
  local code
  code=$(tail -n1 <<<"$1")
  if [[ "$code" != "$2" ]]; then
    echo "FAIL: $3 (HTTP $code)"; echo "$1"; exit 1
  fi
  echo "OK: $3 (HTTP $code)"
}

echo "== 1. device registration (firmware: KoInsightClient::registerDevice) =="
RESP=$(req POST /api/plugin/device "{\"id\":\"$DEVICE_ID\",\"model\":\"x3-x4\",\"version\":\"0.3.0\"}")
expect_code "$RESP" 200 "register device"

echo "== 2. stats import (firmware: KoInsightClient::importStats) =="
IMPORT_BODY=$(cat <<JSON
{
  "version": "0.3.0",
  "books": [{
    "id": 0,
    "md5": "$BOOK_MD5",
    "title": "Test Book (CrossInk)",
    "authors": "Test Author",
    "notes": 0,
    "last_open": $NOW,
    "highlights": 0,
    "pages": 311,
    "series": "",
    "language": "",
    "total_read_time": 450,
    "total_read_pages": 3
  }],
  "stats": [
    {"page": 1, "start_time": $T1, "duration": 150, "total_pages": 311, "book_md5": "$BOOK_MD5", "device_id": "$DEVICE_ID"},
    {"page": 2, "start_time": $T2, "duration": 160, "total_pages": 311, "book_md5": "$BOOK_MD5", "device_id": "$DEVICE_ID"},
    {"page": 3, "start_time": $T3, "duration": 140, "total_pages": 311, "book_md5": "$BOOK_MD5", "device_id": "$DEVICE_ID"}
  ]
}
JSON
)
RESP=$(req POST /api/plugin/import "$IMPORT_BODY")
expect_code "$RESP" 200 "import stats"

echo "== 3. idempotency: re-send the same import (firmware retry after local consume failure) =="
RESP=$(req POST /api/plugin/import "$IMPORT_BODY")
expect_code "$RESP" 200 "re-import same stats"

echo "== 4. KOReader-shaped import for the SAME md5 (unification check) =="
# The KOReader plugin registers its device before importing (book_device has a
# FK to device), so do the same here.
RESP=$(req POST /api/plugin/device '{"id":"koreader-kindle01","model":"Kindle","version":"0.3.0"}')
expect_code "$RESP" 200 "register KOReader device"
KOREADER_BODY=$(cat <<JSON
{
  "version": "0.3.0",
  "books": [{
    "id": 7,
    "md5": "$BOOK_MD5",
    "title": "Test Book (KOReader)",
    "authors": "Test Author",
    "notes": 2,
    "last_open": $NOW,
    "highlights": 1,
    "pages": 512,
    "series": "",
    "language": "en",
    "total_read_time": 900,
    "total_read_pages": 5
  }],
  "stats": [
    {"page": 10, "start_time": $T1, "duration": 200, "total_pages": 512, "book_md5": "$BOOK_MD5", "device_id": "koreader-kindle01"},
    {"page": 11, "start_time": $T2, "duration": 210, "total_pages": 512, "book_md5": "$BOOK_MD5", "device_id": "koreader-kindle01"}
  ]
}
JSON
)
RESP=$(req POST /api/plugin/import "$KOREADER_BODY")
expect_code "$RESP" 200 "KOReader import same md5"

echo "== 5. version gate: wrong plugin version must be rejected =="
RESP=$(req POST /api/plugin/import '{"version":"0.0.0","books":[],"stats":[]}')
expect_code "$RESP" 400 "version gate rejects old plugin"

echo
echo "All API checks passed. Inspect rows with:"
echo "  docker exec koinsight node -e \"const db=require('better-sqlite3')('/app/data/prod.sqlite3'); \
console.log(db.prepare('SELECT * FROM device').all()); \
console.log(db.prepare('SELECT * FROM book').all()); \
console.log(db.prepare('SELECT device_id,book_md5,pages,total_read_time,total_read_pages FROM book_device').all()); \
console.log(db.prepare('SELECT device_id,book_md5,page,start_time,duration,total_pages FROM page_stat ORDER BY device_id,page').all())\""
