# Local buffer vs archive space APIs

Authoritative guidance for how the plugin should report space vs write failures when local staging overflows — and how that differs from **normal capacity retention**. Prefer this over treating every cloud delete as “buffer full.”

Field note (`samples/docs/logs8-findings-report.md`): steady ~1-day-per-day `.mkv` deletes with free space pinned near one day of ingest were **Nx retention at capacity**, not buffer-driven purge. Buffer-full floods there were a **downstream symptom** of a dead upload dispatcher (see [log-signatures.md](log-signatures.md) issue 1).

## Capacity retention vs buffer overflow (diagnose first)

| | Capacity retention (normal) | Buffer overflow cascade |
|--|----------------------------|-------------------------|
| Trigger | Declared/usable free space low vs ingest | Staging ≥ `local_buffer`; often because uploads stopped |
| Log shape | Steady `removeFile`/`removeUrl` on oldest **indexed** days | `Local Folder is full!!` / open refusals; may lack matching delete spike |
| Correlation | Delete rate flat even on days with **zero** buffer-full lines | Buffer-full precedes recording stop; deletes may be unrelated |
| Data fate | Oldest catalogued footage removed by design | New footage never uploaded; restart may discard temp backlog |

Do not attribute retention deletes to buffer overflow unless delete volume tracks buffer crises and space APIs are folding buffer state into “archive full.”

## Why wrong-channel space errors can cause deletion

`getFreeSpace()`, `getTotalSpace()`, and the space error codes (`NotEnoughSpace`, `SpaceInfoNotAvailable`) describe the archive destination's capacity. The mediaserver uses those values to drive archive retention: when it believes the destination is low on or out of space, it deletes the oldest footage it manages to make room. So when the plugin returns `SpaceInfoNotAvailable` because the local buffer overflowed, the server can read that as archive capacity trouble and run storage-full cleanup — `removeFile` → `removeUrl` on managed objects. That is buffer state on the wrong channel (a real design hazard), distinct from healthy retention when the bucket really is at the configured ceiling.

## What to do when the local buffer is full

Three principles:

1. Keep the space APIs tied to the bucket only. `getFreeSpace()` and `getTotalSpace()` should always report the bucket's real (or intended) capacity and must never be influenced by the local buffer. As long as they return healthy bucket space, the server has no reason to run retention solely because staging is full. Do not return `NotEnoughSpace` or `SpaceInfoNotAvailable` from these methods because the buffer is full — reserve `SpaceInfoNotAvailable` for a genuine inability to query the bucket.

2. Prefer back-pressure at the write path. A full buffer is a throughput condition — media is arriving faster than it can be uploaded and drained (or the upload dispatcher is dead). The cleanest handling is for `IODevice::write()` to block/throttle until the buffer drains enough to accept the data. Sizing the buffer adequately and hosting it on a fast, roomy dedicated disk (not `%TEMP%` / `/tmp` / tmpfs), and not sharing one staging root across many server/bucket configs, is part of this.

3. If a write genuinely cannot proceed, signal it as a storage failure, not a space shortage. Return `nx_spl::error::UnknownError` from `IODevice::write()` (short/zero byte count) or take the storage temporarily offline via `isAvailable()` → 0 / `StorageUnavailable`. Do **not** spin `S3Storage::open` tens of thousands of times while silently dropping recording — that pattern precedes Media Server access violations under load. The server treats unavailable storage / write I/O errors as throughput faults (hold/drop frames, Storage Issue event) without using retention to reclaim space.

Rule of thumb: space errors drive retention; availability and I/O errors drive failure handling. A buffer overflow belongs in the second category, never the first.

## Ops mitigations while code still refuses writes on full buffer

- Raise `local_buffer` substantially (field: 2 GB ≈ ~30 min at ~60 MB/segment; prefer 20 GB+).
- Move staging off the Windows TEMP tree to a dedicated data volume.
- Give each Media Server / bucket config its own staging root (`UploadList.json` + temps).
- Watchdog: no successful `.mkv` upload for >10 minutes → alert and restart (dispatcher death is restart-recoverable).

## Notifying the user when recording stops

### Option A — built-in Storage Issue event (passive)

When the plugin reports storage unavailable or write I/O errors, the mediaserver raises Storage Issue (historically “Storage Failure” in some rules). Default rules notify users / email admin. Generic alert — not specifically “buffer overflow.”
