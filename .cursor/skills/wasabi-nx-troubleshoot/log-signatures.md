# Log signatures → failure modes

Grep DailyLogger files for these sequences. Match **order**, not isolated lines.
Counts and dates in `wasabi-nx-issues-report.md` are from one deployment (HO-NX-Windows, mid-2026); re-measure on the user's logs.

## Issue map

| # | Mode | Severity | Bucket impact | Timeline impact |
|---|------|----------|---------------|-----------------|
| 1 | Cloud purge on local buffer full | Critical | `.mkv` deleted from S3 | Indirect (catalog points at gone objects) |
| 2 | `.nxdb` segment rotation | Critical* | Old `--N.nxdb` deleted | Gaps / lost catalog if new gen empty or unreadable |
| 3 | Missing `_db_ref.guid` | Info† | — | Expected without `cap::DBReady`; not a timeline fault by itself |
| 4 | `S3FileInfoIterator` path corruption | High | Possible bad follow-on deletes | Reindex fails |
| 5 | Upload race vs local cleanup | Medium | Chunk never uploaded | Gap |
| 6 | Invalid JSON on `S3Storage` ctor | Low | — | Noise if connect still works |
| 7 | Local temp cleanup conflicts | Low | Contributes to 5 | Noise |

\*Normal StorageDb vacuum also deletes the prior `--N.nxdb` after writing `--(N+1)`; that alone is not media loss. See skill `wasabi-nx-storagedb`. Flag Critical when the new generation is empty/tiny after prior footage, or “No previous DB files found” recurs after recordings.

†See skill `wasabi-nx-storagedb`. Probe + `UrlNotExists` with no following write is correct for non-database-host (backup) storage. Only escalate if the user intentionally advertised `cap::DBReady` and still never gets a write.

---

## 1 — Cloud video purge (buffer overflow)

**Grep:** `local folder full`, `Local Folder is full`, `removeUrl`, `S3Storage::removeFile`, `.mkv`

**Sequence:**

```
getFreeSpace … local folder full:<bytes>          # often > 1073741824
S3Storage::open … Local Folder is full!! …
S3Storage::removeFile … /YYYY/MM/DD/HH/<ts>_<id>.mkv
s3Client::removeUrl … deleted file … .mkv
```

**Code:** `S3Storage::getFreeSpace`, `S3Storage::open`, `S3Storage::removeFile`, `s3Client::removeUrl`.

**Meaning:** Local staging exceeded `local_buffer`. The plugin folded buffer state into space APIs (`getFreeSpace` / `SpaceInfoNotAvailable` / not-enough-space signaling). Mediaserver treats that as archive destination full/unknown and runs retention cleanup — deleting oldest managed footage via `removeFile` → `removeUrl` (S3 objects, not only local temps). Buffer fullness was reported on the wrong channel. Full explanation: [space-and-buffer.md](space-and-buffer.md).

**Mitigation (ops):** Raise `local_buffer`; host staging on a fast, roomy disk (not `/tmp`/tmpfs); pause recording until backlog drains; check Wasabi versioning for deleted keys.

**Fix direction (code):** Keep `getFreeSpace`/`getTotalSpace` bucket-only (never buffer-influenced). Prefer `IODevice::write()` back-pressure/throttle while buffer drains. If write cannot proceed: `nx_spl::error::UnknownError` (short/zero bytes) or `isAvailable()` → 0 / `StorageUnavailable` — not space errors. Space errors drive retention; availability/I/O errors drive failure handling (Storage Issue / “Storage Failure” event). See [space-and-buffer.md](space-and-buffer.md).

---

## 2 — Index segment rotation

**Grep:** `--`, `.nxdb`, `removeUrl`, `S3IODevice::~S3IODevice`, `uploadFile`, `Download failed`

**Sequence:**

```
S3IODevice::~S3IODevice …--N.nxdb
uploadFile …--N.nxdb
S3Storage::open …--N.nxdb
S3Storage::open …--(N+1).nxdb
Download failed …--(N+1).nxdb … 404
uploadFile …--(N+1).nxdb          # often tiny (~16 byte header)
removeUrl, deleted file …--N.nxdb
```

**Triggers often near:** plugin restart (`createNXPluginInstance`), midnight reconnect, daily maintenance (~14:39 in field report).

**Meaning:** Advancing index segment deletes prior segment from cloud. On the server this is StorageDb generational vacuum (compact + swap so one current catalog remains). Distinct from harmless periodic re-upload of the *current* `.nxdb`. Pathological when the kept generation has no carried-forward records after footage existed, or listing never returns the prior catalog after restart.

**Mitigation:** Preserve remaining `--*.nxdb`; do not manual-delete; check versioning; avoid rebuild until issue 4 fixed. Run the restart verification in skill `wasabi-nx-storagedb` before treating vacuum as data loss.

---

## 3 — Missing `_db_ref.guid`

**Grep:** `_db_ref.guid`, `fileExists`, `Download failed`, server `main.log` `DbReady`

**Pattern (often every init):**

```
s3Client::downloadFile, Download failed: …/<serverGuid>_db_ref.guid … 404
S3Storage::fileExists, file not found: …_db_ref.guid
```

**Server-side context (main.log):** `DbReady is true` typically appears only for local system storage (`isSystem: 1`, role `'main'`). Wasabi with `cap::DBReady` omitted is `isSystem: 0`, role `'backup'` — no DbReady line, no `db_ref.guid` write.

**Meaning:** For the intended non-database-host capability set, absence of `_db_ref.guid` is expected and does not impair recording/catalog as a backup target. Plugin `fileExists` returning not-found/`UrlNotExists` is correct; the server is not attempting a write. Do not recommend adding `cap::DBReady` on object storage unless database-host behavior is explicitly required (reintroduces in-place SQLite over S3).

**When it would matter:** Only if `cap::DBReady` were advertised and the server still never wrote the ref, or a downstream requirement specifically depends on database-host semantics.

---

## 4 — Iterator path corruption

**Grep:** `S3FileInfoIterator::next`, `,\,`, `Download failed: /2`, `1969/12/31`

**Pattern:**

```
S3FileInfoIterator::next … /2026/…/….mkv,<type>,<size>   # valid
S3FileInfoIterator::next … \,0,<size>                    # corrupt
S3FileInfoIterator::next … 2,0,<size>                    # corrupt
downloadFile … /2 … 404
removeFile … /1969/12/31/20/2.mkv                        # epoch/garbage path
```

**Code:** `S3FileInfoIterator::next`, `s3Client::getobjectKeys` listing parse.

**Meaning:** S3 listing → iterator produces garbage names; rebuild/scan fails and may attempt nonsense downloads/deletes.

**Mitigation:** Do not run aggressive reindex until fixed; inspect `getobjectKeys` / iterator for how name/type/size are split.

---

## 5 — Upload race (file gone before upload)

**Grep:** `File do not exist to uplaod` (typo is intentional in code), `fileUploadThread`

**Pattern:**

```
Error: … fileUploadThread … File do not exist to uplaod!!,…/*.mkv
```

**Code:** `s3Client` upload thread (~`ERRORLOG("File do not exist to uplaod!!"`); `ClearMemoryManager` may have removed local file; queue entry can survive restart.

**Meaning:** Chunk never reached Wasabi. Independent of purge (issue 1).

**Common trigger:** Plugin restart with pending `UploadList.json` entries whose local copies are gone.

---

## 6 — Invalid JSON on storage init

**Grep:** `Invalid json`, `S3Storage::S3Storage`

```
Error: … S3Storage::S3Storage … Invalid json
```

Often on reconnect; connection may still succeed. Low priority unless storage stays unavailable.

---

## 7 — Local cleanup conflicts

**Grep:** `Failed to remove file`, `Unable to read local file`

Competing `ClearMemoryManager` / upload / `removeFile` on the same temp path. Noise that feeds issues 1 and 5.

---

## Startup / reconnect anchors

Use these to bracket incidents:

| Signature | Meaning |
|-----------|---------|
| `create  NXPlugin Instance` | Plugin load / factory create |
| `SuccessFully initialise s3 connection` / `establish s3 connection` | AWS client OK |
| `userAgent` / `Wasabi/` | Confirms plugin VERSION string |
| `Storage not available!!` | License/config/client unavailable path |

---

## Useful ripgrep recipes

```bash
rg -n "local folder full|Local Folder is full" log_*.txt
rg -n "deleted file.*\.mkv|removeFile.*\.mkv" log_*.txt
rg -n "deleted file.*\.nxdb|removeUrl.*\.nxdb" log_*.txt
rg -n "_db_ref\.guid" log_*.txt
rg -n "File do not exist to uplaod" log_*.txt
rg -n "S3FileInfoIterator::next" log_*.txt | rg ",\\\\,|,2,0,"
rg -n "create  NXPlugin Instance|Invalid json" log_*.txt
```

On Windows PowerShell, prefer `rg` the same way from the log folder.

---

## Interaction diagram

```
record → local .mkv
  ├─ upload OK → S3 .mkv
  │    ├─ buffer full → removeUrl deletes S3 .mkv     [1]
  │    └─ indexed in .nxdb → timeline
  │         ├─ rotation deletes old --N.nxdb           [2]
  │         └─ no _db_ref.guid → expected w/o DBReady  [3]
  └─ cleanup before upload → never on S3              [5]

reindex scan → iterator garbage → rebuild fails       [4]
```
