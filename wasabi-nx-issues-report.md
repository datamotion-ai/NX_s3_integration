# Wasabi / Nx Witness Storage Issues — Full Report

**Analysis date:** 2026-07-07  
**Log source:** Wasabi Storage SDK logs (`Wasabi_logs/`)  
**Log period:** 2026-06-18 through 2026-07-06  
**System:** Nx Witness v6.1.2.42921 on `HO-NX-Windows`  
**Cloud bucket:** `nx-old-june18` (Wasabi S3, `s3.us-east-1.wasabisys.com`)  
**SDK version:** `Wasabi/beta-global-2.2` (Windows/10.0.17763)

> **Note:** The 2026-06-20 index reset and associated deletions are **excluded** from root-cause analysis — that event was expected (storage migration/cutover to the `nx-old-june18` bucket). Findings below focus on ongoing behaviour from June 21 onward.

---

## Executive Summary

The logs reveal **two independent but compounding failure modes** that explain missing video footage:

| Symptom | Primary cause | Data impact |
|---------|---------------|-------------|
| Videos **missing from the Wasabi bucket** | SDK deletes `.mkv` files from cloud when the local 1 GB buffer overflows | **Permanent loss** of video objects in S3 (~16,000+ logged deletions) |
| Videos **missing from the Nx Witness timeline** | `.nxdb` index segments rotated/deleted; index chain cannot be rebuilt | Footage invisible in UI even when objects exist (or existed) in cloud |

Both are caused by defects or misconfiguration in the **Wasabi Storage SDK (`beta-global-2.2`)**, not by Wasabi infrastructure failures or camera issues.

---

## System Context

| Property | Value |
|----------|-------|
| Server name | HO-NX-Windows |
| Server GUID | `937512fb-97b5-7a6b-eaba-a5ffb61b444b` |
| Nx Witness version | 6.1.2.42921 |
| Wasabi bucket | `nx-old-june18` |
| Wasabi region | us-east-1 |
| Camera device ID | `068b9ee6f0610fe2544d9f168cf7efd1` |
| Local temp storage | `C:\Windows\TEMP\/Nx Storage/` |
| Local buffer size | 1,073,741,824 bytes (1 GB) |
| Earliest recorded footage (from logs) | 2026-06-18 |

---

## Issue Summary

| # | Issue | Severity | Impact |
|---|-------|----------|--------|
| 1 | [Cloud video purge on buffer overflow](#1-cloud-video-purge-on-local-buffer-overflow) | **Critical** | `.mkv` files permanently deleted from Wasabi |
| 2 | [Index segment rotation deletes old `.nxdb` files](#2-index-segment-rotation-deletes-old-nxdb-files) | **Critical** | Timeline metadata lost from cloud |
| 3 | [Missing `_db_ref.guid` reference file](#3-missing-_db_refguid-reference-file) | **High** | Cannot reconstruct index chain after restart |
| 4 | [`S3FileInfoIterator` path parsing corruption](#4-s3fileinfoiterator-path-parsing-corruption) | **High** | Index rebuild from S3 scan fails |
| 5 | [Upload race conditions](#5-upload-race-conditions) | **Medium** | Recordings never reach the bucket |
| 6 | [Invalid JSON on storage init](#6-invalid-json-on-storage-init) | **Low** | Config/metadata parse warning |
| 7 | [Local temp cleanup conflicts](#7-local-temp-cleanup-conflicts) | **Low** | Operational noise; contributes to upload failures |

---

## 1. Cloud Video Purge on Local Buffer Overflow

**Severity: Critical — primary cause of missing videos in the bucket**

### What happens

When the local temp buffer exceeds its 1 GB limit, the Wasabi Storage SDK **deletes `.mkv` video files from the Wasabi bucket** (not just local temp files). Deletions start with the **oldest** cloud footage and continue in batches until space is available for new recordings.

### Log sequence (every purge event)

```
getFreeSpace → local folder full:1074316517        (~1.07 GB, over 1 GB limit)
S3Storage::open → Local Folder is full!! No space available.
S3Storage::removeFile → .../2026/07/04/02/1783145500819_75419.mkv
s3Client::removeUrl → deleted file, .../1783145500819_75419.mkv
S3Storage::removeFile → file deleted
```

### Evidence: files deleted minutes after upload

On **2026-07-05**, files uploaded successfully were deleted from cloud during the same buffer-overflow cycle:

| Time | Event |
|------|-------|
| 00:06:52 | `Successfully uploaded file: .../1783145500819_75419.mkv` |
| 00:08:48 | `local folder full:1074316517` |
| 00:08:48 | `removeUrl, deleted file .../1783145500819_75419.mkv` |

The SDK treats cloud storage as a **rolling buffer** tied to local disk usage — not as long-term archive storage.

### Logged cloud `.mkv` deletions (excluding June 20 migration)

| Log file | Deletions | Notes |
|----------|-----------|-------|
| `log_2026-07-01_00-31-53.txt` | 7,988 | Purge from 2026-06-21 onward |
| `log_2026-07-02_00-00-00.txt` | 3,824 | Continued rolling purge |
| `log_2026-07-03_15-28-17.txt` | 1,400 | |
| `log_2026-06-30_00-00-00.txt` | 1,000 | |
| `log_2026-07-05_00-00-00.txt` | 675 | |
| `log_2026-07-05_04-05-45.txt` | 273 | |
| `log_2026-06-24_20-24-48.txt` | 790 | |
| `log_2026-06-24_18-13-39.txt` | 10 | |
| **Total** | **~16,000+** | |

### Why the buffer keeps overflowing

1. **Upload backlog** — recordings arrive faster than the upload queue drains; temp files accumulate under `C:\Windows\TEMP\/Nx Storage/`.
2. **1 GB buffer too small** — usage regularly reaches 1.07–1.12 GB before purges trigger (`local folder full` logged 787+ times across all logs).
3. **Plugin restarts** — reconnects stall uploads and leave orphaned queue entries, worsening backlog.

### Recommended actions

- **Increase local buffer size immediately** (e.g. 10–50 GB) in Wasabi storage plugin settings.
- **Pause recording** until buffer size is corrected to prevent further cloud deletions.
- **Check Wasabi object versioning** — deleted objects may be recoverable as non-current versions if versioning was enabled.
- **Escalate to Wasabi/Nx support** — deleting cloud archive data when local temp is full is destructive and likely a SDK defect.

---

## 2. Index Segment Rotation Deletes Old `.nxdb` Files

**Severity: Critical — primary cause of missing footage on the server timeline**

### What happens

Nx Witness uses segmented `.nxdb` index files to catalog which video chunks exist and when. When advancing from `--N.nxdb` to `--(N+1).nxdb`, the SDK **deletes the previous segment from Wasabi** and uploads a new (often empty) segment.

### Rotation sequence

| Step | Action | Log signature |
|------|--------|---------------|
| 1 | Close current index | `S3IODevice::~S3IODevice ...--N.nxdb` |
| 2 | Final upload of current segment | `uploadFile ...--N.nxdb` |
| 3 | Read/verify current segment | `S3Storage::open ...--N.nxdb,2` |
| 4 | Create next segment | `S3Storage::open ...--(N+1).nxdb,6` |
| 5 | Next segment missing in cloud | `Download failed ...--(N+1).nxdb, 404` |
| 6 | Upload new empty file (16 bytes) | `uploadFile ...--(N+1).nxdb` |
| 7 | **Delete old segment from cloud** | `removeUrl, deleted file ...--N.nxdb` |

### Rotation triggers observed

| Trigger | When | Example |
|---------|------|---------|
| Daily index maintenance | ~14:39 each day | June 27–29 at 14:39:3x–44 |
| Storage plugin restart | `createNXPluginInstance` / `S3StorageFactory::createStorage` | July 5 22:33, July 4 00:01 |
| Midnight reconnect | ~00:00–00:03 | July 3 00:01, July 2 00:03 |

### Deleted index segments (June 21 onward)

| Date | Deleted index |
|------|---------------|
| 2026-06-21 | `--3.nxdb` |
| 2026-06-22 | `--4.nxdb` |
| 2026-06-23 | `--5.nxdb` |
| 2026-06-24 | `--6.nxdb` |
| 2026-06-25 | `--7.nxdb` |
| 2026-06-26 | `--8.nxdb` |
| 2026-06-27 | `--9.nxdb` |
| 2026-06-28 | `--10.nxdb` |
| 2026-06-29 | `--11.nxdb`, `--12.nxdb` |
| 2026-06-30 | `--13.nxdb`, `--14.nxdb` |
| 2026-07-01 | `--15.nxdb` |
| 2026-07-02 | `--16.nxdb` |
| 2026-07-03 | `--17.nxdb` |
| 2026-07-04 | `--18.nxdb` |
| 2026-07-05 | `--19.nxdb` |
| **Current** | `--20.nxdb` active |

**Example (2026-06-29 at 14:39:44):**

```
--11.nxdb size: 176,760 bytes  →  rotated to  →  --12.nxdb size: 16 bytes (empty header)
removeUrl, deleted file ...--11.nxdb
```

### Why `.nxdb` files appear "overwritten"

Three distinct mechanisms:

| Mechanism | Harmful? | Description |
|-----------|----------|-------------|
| Normal re-upload | No | Current `.nxdb` re-uploaded every ~1–2 min as chunks close — incremental sync |
| Index segment rotation | **Yes** | Old segment deleted from cloud when advancing to next segment |
| Storage reconnect | **Yes** | Same rotation logic on plugin restart |

### Recommended actions

- **Do not manually delete any `.nxdb` files** from Wasabi.
- Search bucket for remaining segments: `937512fb-97b5-7a6b-eaba-a5ffb61b444b--*.nxdb`
- Check Wasabi object versioning for deleted index segments.

---

## 3. Missing `_db_ref.guid` Reference File

**Severity: High — prevents index chain recovery**

### What happens

On **every** storage initialization, the SDK attempts to download a reference file that links index segments together. It has **never existed** in the bucket (observed June 21 through July 6).

### Log pattern (every startup)

```
Error: s3Client::downloadFile, Download failed: .../937512fb-97b5-7a6b-eaba-a5ffb61b444b_db_ref.guid
       The specified key does not exist.,404
Error: nx_spl::S3Storage::fileExists, file not found: .../937512fb-97b5-7a6b-eaba-a5ffb61b444b_db_ref.guid
```

### Impact

Without `_db_ref.guid`, the server cannot reconstruct the index segment chain from cloud backups after restarts. Combined with segment rotation (Issue 2), only the current `--20.nxdb` segment's catalog entries are usable.

---

## 4. `S3FileInfoIterator` Path Parsing Corruption

**Severity: High — blocks index rebuild from S3**

### What happens

When the system scans Wasabi to rebuild the index (e.g. 2026-07-05 23:33), the file iterator produces **corrupted path entries** mixed with valid ones:

```
Info: nx_spl::S3FileInfoIterator::next, ----------------->, .../2026/06/18/13/1781805153705_68530.mkv,0,57617711
Info: nx_spl::S3FileInfoIterator::next, ----------------->, \,0,51261456
Info: nx_spl::S3FileInfoIterator::next, ----------------->, 2,0,51889887
```

### Cascading errors

```
Error: s3Client::downloadFile, Download failed: /2, The specified key does not exist.,404
Error: nx_spl::S3Storage::fileExists, file not found: C:\Windows\TEMP\/Nx Storage/2
Info:  nx_spl::S3Storage::removeFile, .../1969/12/31/20/2.mkv
```

The bogus path `1969/12/31/20/2.mkv` indicates timestamp/metadata corruption — likely conflating file size or partial filename with an epoch timestamp.

### Scale

| Metric | Count |
|--------|-------|
| Malformed `,\,` iterator entries (approx.) | ~150,000+ across all log files |
| `/2` download failures | 652+ |

### Impact

Automatic re-indexing from S3 object listings fails. Even if `.mkv` files remain in the bucket, the server cannot rebuild a reliable catalog without a fixed SDK.

---

## 5. Upload Race Conditions

**Severity: Medium — recordings never reach the bucket**

### What happens

Local temp `.mkv` files are deleted by `ClearMemoryManager` before the upload thread can read them. The upload then fails permanently.

### Log pattern

```
Error: s3Client::fileUploadThread, File do not exist to uplaod!!,.../2026/07/04/00/1783137644609_65479.mkv
```

### Occurrences

| Log file | Count |
|----------|-------|
| `log_2026-07-05_04-05-45.txt` | 102 |
| `log_2026-07-02_05-06-23.txt` | 15 |
| `log_2026-06-20_15-39-31.txt` | 3 (migration — excluded) |

### Typical trigger

Plugin restart at **2026-07-05 00:01** — upload queue contained pending files whose local copies had already been cleaned up. These chunks were **never uploaded** to Wasabi.

### Impact

Gap in bucket content for affected time ranges, independent of buffer purge (Issue 1).

---

## 6. Invalid JSON on Storage Init

**Severity: Low**

### Log pattern

```
Error: nx_spl::S3Storage::S3Storage, Invalid json
```

Observed on every storage reconnect (9 occurrences). Connection succeeds afterward, but storage configuration metadata appears malformed or missing.

---

## 7. Local Temp Cleanup Conflicts

**Severity: Low**

### Log patterns

```
Error: nx_spl::S3Storage::removeFile, Failed to remove file: ...local...mkv
Error: Unable to read local file
```

Occurs when `ClearMemoryManager` and the upload thread compete for the same local temp file. Contributes to upload failures (Issue 5) and buffer pressure (Issue 1).

---

## How the Issues Interact

```
Camera records video
        │
        ▼
  .mkv written to local temp (C:\Windows\TEMP\/Nx Storage/)
        │
        ├── Upload succeeds ──► .mkv in Wasabi bucket
        │         │
        │         ├── Buffer overflow ──► removeUrl deletes .mkv from cloud  [Issue 1]
        │         │
        │         └── Entry in .nxdb index ──► visible on Nx timeline
        │                   │
        │                   ├── Index rotation ──► old .nxdb deleted from cloud  [Issue 2]
        │                   │
        │                   └── _db_ref.guid missing ──► chain unrecoverable  [Issue 3]
        │
        └── Upload race ──► File do not exist to upload  [Issue 5]
                  │
                  └── .mkv never reaches bucket

Recovery attempt: S3 scan to rebuild index
        │
        └── S3FileInfoIterator corruption (2, \) ──► rebuild fails  [Issue 4]
```

**Net result:** Footage is missing from both the **Wasabi bucket** (Issue 1, 5) and the **Nx Witness timeline** (Issues 2, 3, 4).

---

## Major Deletion Events Timeline

| Date / time | Event | Footage affected |
|-------------|-------|------------------|
| 2026-06-24 18:11+ | Buffer overflow purge begins | June 19+ `.mkv` files |
| 2026-06-30 00:00+ | Purge event | ~1,000 `.mkv` deletions logged |
| 2026-07-01 00:16+ | Largest purge (~7,988 deletions) | From 2026-06-21 onward |
| 2026-07-02 00:00+ | Continued purge (~3,824 deletions) | Through ~2026-06-28 |
| 2026-07-03 15:28+ | Purge event (~1,400 deletions) | |
| 2026-07-05 00:01+ | Plugin restart + 102 failed uploads | July 3–4 chunks never uploaded |
| 2026-07-05 00:08+ | Buffer purge (~273 deletions) | Including freshly uploaded files |
| Daily ~14:39 | Index segment rotation | Previous `.nxdb` deleted each day |

---

## Recommended Actions (Prioritized)

### Immediate (stop data loss)

1. **Increase local buffer size** — current 1 GB is insufficient; raise to 10–50 GB minimum.
2. **Pause recording or fix buffer** before more cloud footage is purged.
3. **Check Wasabi object versioning** for recoverable deleted objects.

### Short term (investigation & recovery)

4. **Inventory remaining bucket content** — compare against expected retention window.
5. **Preserve all remaining `.nxdb` files** — do not delete manually.
6. **Search for index backups:** `937512fb-97b5-7a6b-eaba-a5ffb61b444b--*.nxdb` and `_db_ref.guid`.

### Escalation (support ticket)

7. **Escalate to Wasabi / Nx Witness support** with this report. Key defects to cite:
   - SDK deletes cloud `.mkv` files when local 1 GB buffer overflows (`removeUrl` after `local folder full`)
   - Index segment rotation deletes prior `.nxdb` from cloud on each rotation
   - Missing `_db_ref.guid` preventing index chain recovery
   - `S3FileInfoIterator` path parsing bug (entries `2`, `\`)
   - SDK version: `Wasabi/beta-global-2.2`

8. **Upgrade the Wasabi Storage SDK** — `beta-global-2.2` shows multiple critical defects; confirm fixes in a newer release before attempting index rebuild.

9. **Do not attempt index rebuild** until the parsing bug (Issue 4) is confirmed fixed — a failed rebuild may cause additional erroneous deletions.

---

## Key Log Files for Support Ticket

| File | Relevance |
|------|-----------|
| `log_2026-07-01_00-31-53.txt` | Largest cloud `.mkv` purge (~7,988 deletions from June 21) |
| `log_2026-07-05_04-05-45.txt` | Buffer overflow purge + 102 upload race failures |
| `log_2026-07-02_00-00-00.txt` | Continued cloud purge (~3,824 deletions) |
| `log_2026-07-06_00-00-00.txt` | Latest startup, index rotation, S3 scan with path corruption |
| `log_2026-06-29_15-24-29.txt` | Daily index rotation at 14:39 |
| `log_2026-06-24_20-24-48.txt` | Early buffer-overflow purge event |

---

## Conclusion

Missing video footage is caused by **multiple SDK defects and misconfiguration**, not a single root cause:

1. **Videos missing from the bucket** — the Wasabi Storage SDK permanently deletes `.mkv` files from S3 when the 1 GB local buffer overflows (~16,000+ logged deletions from June 21 onward). This is the direct cause of empty/missing objects in Wasabi.

2. **Videos missing from the server timeline** — `.nxdb` index segments are rotated out of cloud storage daily, `_db_ref.guid` is missing, and S3 re-index scans fail due to path parsing corruption. Even when bucket objects exist, the server cannot catalog or play them.

Recovery of deleted cloud video depends on **Wasabi object versioning or external backups**. Recovery of timeline visibility requires **index restoration or a successful rebuild** after SDK upgrade. Continuing operation without fixing the buffer size will cause further data loss.
