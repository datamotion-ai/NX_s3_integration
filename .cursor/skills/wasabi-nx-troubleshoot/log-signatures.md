# Log signatures → failure modes

Grep DailyLogger files for these sequences. Match **order**, not isolated lines.
Re-measure counts on the user's corpus. Field synthesis: `samples/docs/logs8-findings-report.md` (supersedes buffer-driven-purge as the default explanation for steady `.mkv` cloud deletes).

## Issue map

| # | Mode | Severity | Bucket impact | Timeline impact |
|---|------|----------|---------------|-----------------|
| 1 | Upload dispatcher death (`.nxdb` flush race) | **Critical P0** | Segments never uploaded; restart may discard temp backlog | Gaps for outage hours |
| 2 | Capacity retention (normal) | Info / expected | Oldest **indexed** `.mkv` deleted | Rolling history shortens when full |
| 3 | Orphan archive (in S3, not in `.nxdb`) | High — often recoverable | Objects remain; retention ignores them | Empty days while bucket has footage |
| 4 | `.nxdb` bad rotation (empty/tiny catalog) | Critical* | Prior `--N.nxdb` deleted too early | Catalog resets; residual of pre-2.5 |
| 5 | Missing `_db_ref.guid` | Info† | — | Expected without `cap::DBReady` |
| 6 | `S3FileInfoIterator` path corruption | High | Possible bad follow-on deletes | Reindex unsafe until fixed |
| 7 | Upload race vs local cleanup | Medium | Chunk never uploaded | Gap |
| 8 | Buffer-full write refusal (cascade) | High symptom | Usually no direct delete | Recording to this storage stops |
| 9 | Invalid JSON on `S3Storage` ctor | Low | — | Noise if connect still works |
| 10 | `ClearMemoryManager` vs open | Medium | May lose temp; can AV in plugin | Reload / crash |

\*Normal StorageDb vacuum also deletes prior `--N.nxdb` after a durable successor; that alone is not media loss (`wasabi-nx-storagedb`). Flag Critical when the kept generation is empty/tiny (~16 B) after prior footage, or “No previous DB files found” recurs after recordings. **Fixed in `beta-global-2.5+`** (`MIN_NXDB_BYTES`, deferred remove).

†Probe + `UrlNotExists` with no following write is correct for non-database-host (backup) storage.

**Do not assume** steady `removeFile` → `removeUrl` on oldest days = buffer overflow. Correlate delete rate with buffer-full counts; flat ~1 day/day deletes with free space pinned near one day of ingest = capacity retention (issue 2).

---

## 1 — Upload dispatcher death (P0)

**Grep:** `uploadFile nxdb`, `S3IODevice::flush`, `added file to uploaded`, `added file in queue`, `fileUploadThread`, `Local Folder is full`, `Uploading file!!`

**Healthy `.mkv` flow:**

```
S3IODevice::~S3IODevice           segment closed in staging
S3Storage::renameFile             → "added file to uploaded"
s3Client::fileUploadThread        → "added file in queue"
… ThreadPool …                    → "Uploading file!!" / "Successfully uploaded file:" / "Delete File:"
```

**Failure signature (same-second collision):**

```
00:00:SS  S3IODevice::flush      uploadFile nxdb, size=,…--N.nxdb
00:00:SS  S3Storage::renameFile  …/….mkv → …_….mkv
00:00:SS  S3Storage::renameFile  added file to uploaded  …mkv
00:00:SS  s3Client::uploadFile   Uploading file!!        …--N.nxdb   # sync path
… then: more "added file to uploaded" WITHOUT any "added file in queue" / fileUploadThread
… ~30 min later (2 GB / ~63 MB): Local Folder is full!! …
… eventually mediaserver crash / restart; ClearMemoryManager may discard dozens of pending .mkv
```

**Code:** sync `S3IODevice::flush` → `s3Client::uploadFile` for `.nxdb` vs async `fileUploadThread` for `.mkv`.

**Meaning:** Midnight (or date-change) `.nxdb` flush races `.mkv` hand-off; upload dispatcher is lost and does not respawn until process restart. Buffer-full and write refusal are **downstream**. Footage for those hours never reaches Wasabi; restart often destroys the local backlog (many segments discarded vs normal “1 open segment”).

**Mitigation (ops):** Watchdog — no `Successfully uploaded file` …`.mkv` for >10 min → alert / restart Media Server. Raise `local_buffer` (20 GB+), dedicated staging disk, per-config staging root. Check main storage for outage windows (backup role).

**Fix direction:** Queue `.nxdb` through the same path as `.mkv` or one mutex; self-heal dead `fileUploadThread`; re-queue temps on restart instead of deleting; harden `ClearMemoryManager`.

---

## 2 — Capacity retention (normal)

**Grep:** `S3Storage::removeFile`, `s3Client::removeUrl`, `deleted file` … `.mkv` on oldest indexed days

**Sequence (steady, ~1/min when trimming a day):**

```
S3Storage::removeFile  …/YYYY/MM/DD/HH/<ts>_<dur>.mkv
s3Client::removeUrl    deleted file … same .mkv
```

**Meaning:** Free space pinned near ~one day of ingest → Nx deletes oldest **catalogued** day as new days arrive. Lag often ~configured retention (e.g. ~30 d). After an outage, retention may catch up by deleting several footage-days at once.

**How to tell from buffer-driven wrong-channel deletes:**

| Signal | Capacity retention | Buffer wrong-channel (historical / if space APIs fold buffer) |
|--------|--------------------|----------------------------------------------------------------|
| Delete rate | Flat ~1 day of segments/day | Spikes with buffer crises |
| Buffer-full lines | Can be 0 on heavy-delete days | Correlated with deletes |
| Free space | Pinned low vs daily ingest | May show `local folder full` folded into space APIs |

**Mitigation:** Confirm intended max days / `s3.config` capacity; check versioning if recovery needed. Do **not** “fix” by treating as plugin purge.

---

## 3 — Orphan archive (bucket ahead of catalog)

**Grep:** `getFileIterator`, `getobjectKeys` … `s3:` with real sizes for days **before** first retention target; retention only ever deletes from catalog start day onward.

**Meaning:** Timeline is driven by `<GUID>--N.nxdb`, not by walking the bucket for playback. Objects can exist for weeks with no index entry — invisible to timeline **and** retention (they still consume capacity).

**Common origin:** Pre-`2.5` empty/tiny `.nxdb` upload + delete of prior generation (issue 4), then resume indexing forward only.

**Mitigation:** Raise `s3.config` capacity **first**, then Rebuild archive index only if issue 6 is clean. See `wasabi-nx-storagedb`.

---

## 4 — Index segment rotation (pathological vs healthy)

**Grep:** `--`, `.nxdb`, `nxdb create deferred`, `nxdb remove deferred`, `uploadFile nxdb`, `removeUrl` … `.nxdb`

**Healthy (`beta-global-2.5+`):**

```
S3IODevice::flush    nxdb create deferred upload, size=,16,  …--(N+1).nxdb   # not sent
S3IODevice::flush    uploadFile nxdb, size=,<≥MIN_NXDB_BYTES>, …--(N+1).nxdb
s3Client::uploadFile Successfully uploaded …--(N+1).nxdb
s3Client::removeUrl  deleted file …--N.nxdb   # only after successor durable
# or: nxdb remove deferred … waiting for successor size
```

**Pathological (pre-2.5 / if regressions):**

```
uploadFile …--(N+1).nxdb          # tiny (~16 byte header) reaches S3
removeUrl, deleted file …--N.nxdb # prior populated catalog gone from cloud
```

**Triggers:** plugin restart, midnight reconnect / date-change flush, daily maintenance.

**Mitigation:** Preserve remaining `--*.nxdb`; versioning; restart verification in `wasabi-nx-storagedb`. Residual: cloud catalog can be up to ~24 h stale if uploads are midnight-only.

---

## 5 — Missing `_db_ref.guid`

**Grep:** `_db_ref.guid`, `fileExists`, `Download failed`, server `main.log` `DbReady`

```
s3Client::downloadFile, Download failed: …/<serverGuid>_db_ref.guid … 404
S3Storage::fileExists, file not found: …_db_ref.guid
```

**Meaning:** Expected for backup / no `cap::DBReady`. Not a timeline fault. Escalate only if `DBReady` was intentionally advertised and write never follows.

---

## 6 — Iterator path corruption

**Grep:** `S3FileInfoIterator::next`, `,\,`, `Download failed: /2`, `1969/12/31`

```
S3FileInfoIterator::next … /2026/…/….mkv,<type>,<size>   # valid
S3FileInfoIterator::next … \,0,<size>                    # corrupt
S3FileInfoIterator::next … 2,0,<size>                    # corrupt
```

**Meaning:** Listing parse garbage → reindex unsafe. **Gate:** do not rebuild archive index until zero corruption hits (path depth / malformed token checks).

---

## 7 — Upload race (file gone before upload)

**Grep:** `File do not exist to uplaod` (typo intentional in code), `fileUploadThread`

```
Error: … fileUploadThread … File do not exist to uplaod!!,…/*.mkv
```

**Meaning:** Chunk never reached Wasabi — local file removed while queue entry remains. Distinct from dispatcher death (issue 1), where queue pickup never happens.

---

## 8 — Buffer-full write refusal (usually cascade)

**Grep:** `local folder full`, `Local Folder is full`, `S3Storage::open`

```
getFreeSpace … local folder full:<bytes>          # if present
S3Storage::open … Local Folder is full!! No space available. stop writing
```

**Meaning:** Staging ≥ `local_buffer`. Often follows issue 1 (nothing uploading/reclaiming). Tight retry loops (tens of thousands of lines) can precede Media Server AV (`0xC0000005`, often `nx_utils.dll`).

**Code guidance:** Do not report buffer state via space APIs in a way that drives retention; prefer write back-pressure / `isAvailable` ([space-and-buffer.md](space-and-buffer.md)). Still: **observed steady cloud deletes in logs8 were capacity retention, not this path.**

---

## 9 — Invalid JSON on storage init

**Grep:** `Invalid json`, `S3Storage::S3Storage`

Often on reconnect; low priority if storage still connects.

---

## 10 — ClearMemoryManager vs open

**Grep:** `Delete File:`, `Failed to open local file`, `INVALID_HANDLE_VALUE`, plugin reload

```
Delete File: …<seg>.mkv
S3Storage::open … same file
Failed to get file size … INVALID_HANDLE_VALUE
Failed to open local file!!
```

**Meaning:** Cleanup deleted a file still being opened. Can escalate to AV inside `s3_storage_plugin.dll`. Secondary under load / after issue 1, but a real crash path.

---

## Startup / reconnect anchors

| Signature | Meaning |
|-----------|---------|
| `create  NXPlugin Instance` | Plugin load / factory create |
| `SuccessFully initialise s3 connection` / `establish s3 connection` | AWS client OK |
| `userAgent` / `Wasabi/` | Confirms plugin VERSION string |
| `Storage not available!!` | License/config/client unavailable path |
| `nxdb create deferred upload` | 2.5+ refusing tiny catalog PUT |
| `nxdb remove deferred` | 2.5+ waiting for successor before cloud delete |

---

## Useful ripgrep recipes

```bash
rg -n "uploadFile nxdb|added file to uploaded|added file in queue|fileUploadThread" log_*.txt
rg -n "local folder full|Local Folder is full" log_*.txt
rg -n "deleted file.*\.mkv|removeFile.*\.mkv" log_*.txt
rg -n "nxdb create deferred|nxdb remove deferred|deleted file.*\.nxdb" log_*.txt
rg -n "_db_ref\.guid" log_*.txt
rg -n "File do not exist to uplaod" log_*.txt
rg -n "S3FileInfoIterator::next" log_*.txt | rg ",\\\\,|,2,0,"
rg -n "create  NXPlugin Instance|Invalid json|Failed to open local file" log_*.txt
```

---

## Interaction diagram

```
record → local .mkv
  ├─ renameFile "added file to uploaded"
  │    ├─ fileUploadThread "added file in queue" → S3 OK
  │    └─ no queue pickup after sync .nxdb flush → dispatcher dead [1]
  │         → staging fills → Local Folder is full [8] → crash / restart discards backlog
  ├─ indexed in .nxdb → timeline
  │    ├─ healthy vacuum: defer tiny upload; delete prior after successor [4]
  │    ├─ bad tiny upload (pre-2.5) → orphan older S3 objects [3]
  │    └─ no _db_ref.guid → expected w/o DBReady [5]
  ├─ capacity full → removeUrl oldest indexed .mkv [2]
  └─ cleanup before upload → never on S3 [7]

reindex scan → iterator garbage → rebuild unsafe [6]
```
