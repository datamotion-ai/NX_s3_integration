---
name: wasabi-nx-troubleshoot
description: >-
  Explains Wasabi/Nx S3 storage plugin code paths and diagnoses DailyLogger
  output for missing footage, buffer purge, index rotation, and upload failures.
  Use when analyzing plugin logs, Wasabi_logs, .mkv/.nxdb deletions, free-space
  / getFreeSpace / SpaceInfoNotAvailable issues, local buffer overflow driving
  archive retention deletes, S3FileInfoIterator errors, or when the user asks how
  the s3_storage_plugin works or why recordings are missing from the bucket or
  Nx timeline.
---

# Wasabi NX S3 Plugin — Code Logic & Log Troubleshooting

## When to use

Apply this skill whenever the user:

- Asks how write/read/upload/delete/free-space flows work in `s3_storage_plugin`
- Pastes or points at DailyLogger files (`log_YYYY-MM-DD*.txt`, `Wasabi_logs/`)
- Reports missing video in the **bucket**, on the **Nx timeline**, or both
- Mentions `local folder full`, `removeUrl`, `.nxdb`, `_db_ref.guid`, or upload races
- Asks why the server deletes oldest archive when the local buffer is full, or how `getFreeSpace` / `SpaceInfoNotAvailable` should behave

Read [architecture.md](architecture.md) for component map and request flows.
Read [log-signatures.md](log-signatures.md) when matching symptoms to known failure modes.
Read [space-and-buffer.md](space-and-buffer.md) when diagnosing buffer-driven retention deletes or designing space/write error handling (`getFreeSpace` must stay bucket-only; buffer full → back-pressure or I/O/`isAvailable` failure, never space errors).
For normal StorageDb `.nxdb` generational vacuum, `db_ref.guid` probe-without-write on non-`DBReady` backup storage, main vs backup archive path/filename differences, vs pathological empty-catalog restarts, read the sibling skill `wasabi-nx-storagedb`.
Prefer live code under `samples/s3_storage_plugin/src/` over stale docs; version is `VERSION` in `common.hpp`.
Known field findings are summarized in `wasabi-nx-issues-report.md` (use as evidence patterns, not as current code truth).

## Quick mental model

```
Nx Media Server
  → createNXPluginInstance → S3StorageFactory → S3Storage / S3IODevice
  → local staging: %TEMP%/Nx Storage (or $TMP/Nx Storage)
  → s3Client upload queue + ThreadPool → Wasabi S3
  → ClearMemoryManager cleans local temps
  → DailyLogger → ./logs/log_YYYY-MM-DD.txt (relative to mediaserver cwd)
```

**Two failure surfaces (often independent):**

| Symptom | Typical cause |
|---------|---------------|
| Object missing in Wasabi | Cloud delete via `removeFile`/`removeUrl`, or upload never ran |
| Object in bucket but missing on timeline | Empty/unreadable `.nxdb` after restart (listing/persistence), bad `S3FileInfoIterator` scan — not ordinary StorageDb generational vacuum; missing `_db_ref.guid` alone is expected without `cap::DBReady` |

## Investigation workflow

Copy and track:

```
Troubleshoot progress:
- [ ] 1. Classify symptom (bucket vs timeline vs both)
- [ ] 2. Confirm plugin version / env.config (local_buffer, log_level)
- [ ] 3. Locate log files and time window
- [ ] 4. Grep for signature sequences (see log-signatures.md)
- [ ] 5. Map hits to code functions and decide root cause
- [ ] 6. Recommend immediate mitigation vs code fix
```

### 1. Classify the symptom

Ask (or infer from logs):

- Are `.mkv` objects absent from the bucket for the time range?
- Does Nx show gaps on the timeline while objects still exist in S3?
- Did the gap start after a plugin restart, midnight, or ~daily maintenance?

### 2. Confirm runtime config

Check next to the Media Server binary:

- `env.config` — `local_buffer` (GB), `log_level` (0=DEBUG, 1=INFO, 2=ERROR), `log_max`, `max_parallel_upload`, `sync_nxdb`
- `s3.config` — reported capacity per host/bucket (not a hard Wasabi quota)
- Plugin `VERSION` in `common.hpp` (User-Agent also logs `Wasabi/<VERSION> ...`)

A 1 GB `local_buffer` plus upload backlog is the classic trigger for free-space=0 / `SpaceInfoNotAvailable` and Media Server–driven cloud deletes — that is buffer state reported on the wrong channel (see [space-and-buffer.md](space-and-buffer.md)).

### 3. Locate logs

- Default directory: `./logs` relative to Media Server working directory (`DailyLogger::m_logDirectory`)
- Names: `log_YYYY-MM-DD.txt`; rotated: `log_YYYY-MM-DD_HH-MM-SS.txt` when size ≥ 5 MB
- Retention: `log_max` (default 3) — old files deleted aggressively; ask user for full `Wasabi_logs/` archives when available

### 4. Parse log line format

```
<Level>:\t[<YYYY-MM-DD HH:MM:SS>] <line> : <functionName>\t,<arg1>,<arg2>,...
```

Levels: `Debug`, `Info`, `Error`. Args are comma-joined (leading comma before first arg).
`functionName` is `__FUNCTION__` — map directly to C++ symbols in `S3_library.cpp`, `s3Client.cpp`, etc.

### 5. Map to code (start here)

| Concern | Primary files | Key symbols |
|---------|---------------|-------------|
| Plugin load | `S3_library.cpp` | `createNXPluginInstance` |
| Open / write / full buffer | `S3_library.cpp` | `S3Storage::open`, `S3IODevice::*` |
| Free space | `S3_library.cpp` | `S3Storage::getFreeSpace` |
| Delete local+cloud | `S3_library.cpp`, `s3Client.cpp` | `S3Storage::removeFile`, `s3Client::removeUrl` |
| Upload queue | `s3Client.cpp` | `uploadFile`, `fileUploadThread`, `addFileToUploadInQueue` |
| Listing / reindex | `S3_library.cpp`, `s3Client.cpp` | `S3FileInfoIterator::next`, `getobjectKeys` |
| Local cleanup | `ClearMemoryManager.*` | timer cleanup vs upload race |
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
Plugin behavior / config / race — be specific.

## Impact
What was deleted, never uploaded, or made unplayable.

## Next actions
1. Immediate mitigation (config / ops)
2. Code or support follow-up if needed
```

## Safety rules while troubleshooting

- Do **not** recommend deleting remaining `.nxdb` objects from the bucket.
- Do **not** recommend a full S3 reindex/rebuild until `S3FileInfoIterator` path corruption is ruled out or fixed (bogus keys like `2`, `\` can cascade into bad deletes).
- Prefer checking Wasabi **object versioning** before declaring data unrecoverable.
- Prefer increasing `local_buffer` and draining upload backlog before proposing risky cleanup of `Nx Storage`.

## Code-change guidance

When the user asks for a fix after diagnosis:

1. Reproduce the log→code path first.
2. Prefer smallest change that stops destructive behavior. For buffer-full → archive deletes: decouple space APIs from local buffer (bucket-only `getFreeSpace`/`getTotalSpace`); use write back-pressure or `UnknownError`/`isAvailable` failure — never `NotEnoughSpace`/`SpaceInfoNotAvailable` for buffer overflow ([space-and-buffer.md](space-and-buffer.md)).
3. Keep logging: preserve or improve signatures that make the next diagnosis easier.
4. Match existing style in `samples/s3_storage_plugin/src/`.
