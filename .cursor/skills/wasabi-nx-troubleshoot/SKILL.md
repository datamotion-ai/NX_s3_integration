---
name: wasabi-nx-troubleshoot
description: >-
  Explains Wasabi/Nx S3 storage plugin code paths and diagnoses DailyLogger
  output for missing footage, upload-dispatcher death after midnight .nxdb flush,
  buffer-full cascades, index rotation, and upload failures. Use when analyzing
  plugin logs, Wasabi_logs, crash dumps, .mkv/.nxdb deletions, getFreeSpace /
  SpaceInfoNotAvailable, capacity-driven retention vs buffer overflow, orphaned
  S3 objects not on the timeline, S3FileInfoIterator errors, or when the user
  asks how s3_storage_plugin works or why recordings are missing from the bucket
  or Nx timeline.
---

# Wasabi NX S3 Plugin — Code Logic & Log Troubleshooting

## When to use

Apply this skill whenever the user:

- Asks how write/read/upload/delete/free-space flows work in `s3_storage_plugin`
- Pastes or points at DailyLogger files (`log_YYYY-MM-DD*.txt`, `Wasabi_logs/`)
- Reports missing video in the **bucket**, on the **Nx timeline**, or both
- Mentions `local folder full`, `removeUrl`, `.nxdb`, `_db_ref.guid`, midnight flush, or upload races
- Asks whether cloud deletes are buffer-driven vs normal retention at capacity
- Asks about Media Server crashes after upload stalls

Read [architecture.md](architecture.md) for component map and request flows.
Read [log-signatures.md](log-signatures.md) when matching symptoms to known failure modes.
Read [space-and-buffer.md](space-and-buffer.md) for space APIs vs buffer-full handling (and how that differs from capacity retention).
For StorageDb `.nxdb` vacuum, orphan catalogs, `db_ref.guid`, main vs backup paths: sibling skill `wasabi-nx-storagedb`.
Prefer live code under `samples/s3_storage_plugin/src/` over stale docs; version is `VERSION` in `common.hpp` (field-validated catalog fix: `beta-global-2.5+`).
Authoritative field synthesis: `samples/docs/logs8-findings-report.md`. Older `wasabi-nx-issues-report.md` / superseded investigation notes may mis-attribute retention to buffer overflow — prefer logs8.

## Quick mental model

```
Nx Media Server
  → createNXPluginInstance → S3StorageFactory → S3Storage / S3IODevice
  → local staging: %TEMP%/Nx Storage (or $TMP/Nx Storage)
  → .mkv: async s3Client upload queue + ThreadPool → Wasabi
  → .nxdb: often sync upload from S3IODevice::flush (can race the .mkv path)
  → ClearMemoryManager cleans local temps (can race open/upload)
  → DailyLogger → ./logs/log_YYYY-MM-DD.txt (relative to mediaserver cwd)
```

**Three failure surfaces (often independent):**

| Symptom | Typical cause |
|---------|---------------|
| Object missing in Wasabi | Never uploaded (dispatcher dead / race), or cloud delete via `removeFile`/`removeUrl` (retention or ops) |
| Object in bucket but missing on timeline | Catalog starts later than archive (orphaned objects) or empty/tiny `.nxdb` after bad rotation — not ordinary StorageDb vacuum; missing `_db_ref.guid` alone is expected without `cap::DBReady` |
| Recording stops + `Local Folder is full!!` flood | Usually **downstream** of a dead upload dispatcher (queue not draining), not proof that retention is buffer-driven |

## Investigation workflow

Copy and track:

```
Troubleshoot progress:
- [ ] 1. Classify symptom (bucket vs timeline vs both vs stall/crash)
- [ ] 2. Confirm plugin version / env.config (local_buffer, log_level)
- [ ] 3. Locate log files (and crash dumps if present) + time window
- [ ] 4. Grep for signature sequences (see log-signatures.md)
- [ ] 5. Separate: dispatcher death vs capacity retention vs orphan catalog
- [ ] 6. Map hits to code; recommend mitigation vs code fix
```

### 1. Classify the symptom

Ask (or infer from logs):

- Are `.mkv` objects absent from the bucket for the time range?
- Does Nx show gaps while objects still exist in S3? (→ catalog / reindex, not upload)
- Did gaps start after midnight `.nxdb` flush colliding with `renameFile` / “added file to uploaded”?
- After stalls, does restart discard many pending temps (`ClearMemoryManager::freeTempStorage`)?

### 2. Confirm runtime config

Check next to the Media Server binary:

- `env.config` — `local_buffer` (GB), `log_level` (0=DEBUG, 1=INFO, 2=ERROR), `log_max`, `max_parallel_upload`, `sync_nxdb`
- `s3.config` — reported capacity per host/bucket (drives retention depth when full; not a hard Wasabi quota)
- Plugin `VERSION` in `common.hpp` (User-Agent also logs `Wasabi/<VERSION> ...`)

Small `local_buffer` (e.g. 2 GB at ~60 MB/segment ≈ ~30 min) makes dispatcher death escalate quickly to buffer-full and write refusal. That is headroom, not proof that cloud deletes are buffer-caused ([space-and-buffer.md](space-and-buffer.md)).

### 3. Locate logs

- Default directory: `./logs` relative to Media Server working directory (`DailyLogger::m_logDirectory`)
- Names: `log_YYYY-MM-DD.txt`; rotated: `log_YYYY-MM-DD_HH-MM-SS.txt` when size ≥ 5 MB
- Retention: `log_max` (default 3) — ask for full `Wasabi_logs/` archives when available
- Crash dumps: parse ExceptionStream + ModuleList; correlate timestamps with stall ends

### 4. Parse log line format

```
<Level>:\t[<YYYY-MM-DD HH:MM:SS>] <line> : <functionName>\t,<arg1>,<arg2>,...
```

Levels: `Debug`, `Info`, `Error`. Args are comma-joined (leading comma before first arg).
`functionName` is `__FUNCTION__` — map to symbols in `S3_library.cpp`, `s3Client.cpp`, etc.

### 5. Map to code (start here)

| Concern | Primary files | Key symbols |
|---------|---------------|-------------|
| Plugin load | `S3_library.cpp` | `createNXPluginInstance` |
| Open / write / full buffer | `S3_library.cpp` | `S3Storage::open`, `S3IODevice::*` |
| Free space | `S3_library.cpp` | `S3Storage::getFreeSpace` |
| Delete local+cloud | `S3_library.cpp`, `s3Client.cpp` | `S3Storage::removeFile`, `s3Client::removeUrl` |
| Upload queue / dispatcher | `s3Client.cpp` | `uploadFile`, `fileUploadThread`, `addFileToUploadInQueue` |
| `.nxdb` flush / defer | `S3_library.cpp`, `common.*` | `S3IODevice::flush`, `isGenerationalNxdb`, deferred remove helpers |
| Listing / reindex | `S3_library.cpp`, `s3Client.cpp` | `S3FileInfoIterator::next`, `getobjectKeys` |
| Local cleanup | `ClearMemoryManager.*` | timer cleanup vs open/upload race |
| Config / license | `ServerManager.*` | `env.config` load |

When explaining logic, cite the actual function and the log tags it emits. Do not invent Nx Media Server internals — only the plugin side is in this repo.

### 6. Output format for log diagnoses

Use this structure:

```markdown
## Verdict
One sentence: primary failure mode + data impact (bucket / timeline / both).

## Evidence
- Log sequence (quoted signatures + timestamps)
- Matching code path (file + function)

## Root cause
Plugin behavior / config / race — be specific. Say if retention is normal capacity trim.

## Impact
What was deleted, never uploaded, orphaned (in S3 unindexed), or made unplayable.

## Next actions
1. Immediate mitigation (config / ops / watchdog)
2. Code or support follow-up if needed
```

## Safety rules while troubleshooting

- Do **not** recommend deleting remaining `.nxdb` objects from the bucket.
- Do **not** run “Rebuild archive index” until `S3FileInfoIterator` path corruption is ruled out (bogus keys like `2`, `\` can cascade into bad deletes). If zero corruption hits, reindex is allowed — but **raise declared `s3.config` capacity first** so newly indexed older days are not immediately retention-purged.
- Prefer checking Wasabi **object versioning** before declaring data unrecoverable.
- Prefer increasing `local_buffer`, dedicating a staging disk, and a “no `.mkv` upload success for >10 min → alert/restart” watchdog before risky `Nx Storage` cleanup.
- For backup-role storage, check **main** storage for outage windows — primary may still hold footage that never reached Wasabi.

## Code-change guidance

When the user asks for a fix after diagnosis:

1. Reproduce the log→code path first.
2. Prefer smallest change that stops destructive or availability loss:
   - **Dispatcher death (P0):** serialize `.nxdb` and `.mkv` upload paths (queue `.nxdb` like `.mkv` or one `s3Client` mutex); make `fileUploadThread` self-healing; do not discard pending backlog on restart.
   - **Buffer-full → write refusal storm:** back-pressure / `isAvailable` failure — never spin `S3Storage::open` while dropping recording; keep space APIs bucket-only ([space-and-buffer.md](space-and-buffer.md)).
   - **Catalog durability (2.5+):** keep `MIN_NXDB_BYTES` deferral and successor-before-remove ordering.
3. Keep logging: preserve signatures that make the next diagnosis easier.
4. Match existing style in `samples/s3_storage_plugin/src/`.
