# Nx Witness + Wasabi S3 Plugin — Investigation Report

**Subject:** Missing footage on the Nx timeline and archive sync failures on Wasabi backup storage
**Report date:** 2026-08-30 (updated 2026-08-31 — crash-dump exception analysis)
**Status:** Root causes identified; one P0 code defect, one recoverable data-visibility issue, one behaviour confirmed normal; all five dumps parsed

---

## Environment

| Item | Value |
|---|---|
| Media Server | `mediaserver.exe` 6.1.2.42921 (`40d5d24197ba`) |
| Server GUID | `937512fb-97b5-7a6b-eaba-a5ffb61b444b` |
| Plugin | `Wasabi/beta-global-2.5` (`s3_storage_plugin.dll`) |
| Host | Windows Server, 10.0.17763 x64 |
| Storage | `s3://s3.us-east-1.wasabisys.com/nx-old-june18` |
| Storage role | Backup, non-system (`cap::DBReady` intentionally omitted) |
| Local staging | `C:\Windows\TEMP\Nx Storage` |
| `local_buffer` | 2,147,483,648 bytes (2 GB) |
| `log_level` / `log_max` | 1 (INFO) / 3 |
| Evidence | `logs8/logs7/logs7/` — 229 DailyLogger files, 2026-08-15 01:15 → 2026-08-27 07:02 |
| Crash dumps | `logs8/crash1/crash1/` — 5 × `mediaserver.exe` dumps |
| Corroborating sets | `logs4` (Jul 28 – Aug 4), `logs5` (Aug 11), `logs6` (Aug 15–17), `logs7` (Aug 17–19) |

**Workload baseline:** one active camera, hi-quality stream only. ~1,272 segments/day, average segment **63.3 MB** (range 2.3 MB – 186 MB) ≈ **~80 GB/day** ingest. Reported bucket free space is pinned at **~63.8 GB**, i.e. **less than one day of recording**.

---

## Executive summary

Three separate issues were found behind the reported symptoms. Only one is a plugin defect.

| # | Finding | Severity | Effect |
|---|---|---|---|
| **1** | **~25 days of archive (2026-06-18 → 2026-07-12) exist in Wasabi but are absent from the `.nxdb` catalog** | High — recoverable | Footage is in the bucket but cannot appear on the timeline. Also invisible to retention, so it silently consumes capacity. |
| **2** | **`.mkv` upload dispatcher dies in a race with the midnight `.nxdb` upload** | **Critical — P0 code defect** | ~3,250 segments (~206 GB, ~21% of the window) never reached Wasabi. Four outages totalling ~69 h. Stall terminations are `0xC0000005` AVs at a shared `nx_utils.dll` site; Aug 19 is a separate plugin AV. |
| **3** | Retention deletion of July 13–28 footage | Normal | Expected ~30-day rolling retention at capacity. **Not** a fault, and **not** buffer-driven. |

**Reported symptom mapping**

- *"Old footage is missing in Nx"* → Finding 1 (June/early-July never indexed) and Finding 3 (mid-July aged out legitimately).
- *"Footage available in the cloud is not shown locally"* → Finding 1.
- *"Are there sync issues?"* → Finding 2. Yes, a serious one.

---

## Finding 1 — Archive in the bucket is not in the catalog

### Symptom

Objects are visible in Wasabi for dates the Nx timeline shows as empty.

### Root cause

The Nx timeline is driven by the per-storage media catalog (`<GUID>--N.nxdb`), not by bucket contents. **The catalog's history begins 2026-07-13; the bucket's begins 2026-06-18.** Everything in between exists as real video objects with no index entry, so Nx has no way to display it.

### Evidence

A full archive walk on 2026-08-25 enumerates a contiguous day range **2026/06/18 → 2026/08/25** under `…/937512fb-…/hi_quality/068b9ee6f0610fe2544d9f168cf7efd1/`. These are genuine S3 objects — returned on the `s3:` channel with real sizes, not stale local temp folders:

```
getFileIterator  ,----------------->,~G/hi_quality/CAM/2026/06/18/13
getobjectKeys    ,-------------->s3:,1781803166553_71025.mkv,0,63637130
getobjectKeys    ,-------------->s3:,1781803311423_74852.mkv,0,67180983
getobjectKeys    ,-------------->s3:,1781803386275_71878.mkv,0,62044685
```

June 18–20 alone still listed 383 / 905 / 407 objects in the final scan pass, and every day from July 1–12 listed objects. The bucket name `nx-old-june18` matches the archive start date.

**Retention behaviour proves where the catalog starts.** Nx retention always deletes the oldest chunk *it has indexed*, so tracing its first-ever run pinpoints the catalog's earliest record:

| Corpus | Window | Cloud `.mkv` deletes |
|---|---|---|
| `logs4` | Jul 28 – Aug 4 | none — archive still growing |
| `logs5` | Aug 11 | none |
| `logs6` | Aug 15 – 17 | **first ever: `2026/07/13`, 36 segments**, then 07/14, 07/15, 07/16 |
| `logs7` | Aug 17 – 19 | 07/18, 07/19, 07/20 |
| `logs8` | Aug 15 – 27 | 07/13 → 07/28, never earlier |

Retention began around Aug 15 when the storage reached capacity, and the oldest record it could find was **July 13** — and only a partial day of it (36 segments), which is the catalog's first entry. Had June 18 – July 12 been indexed, those days would have been purged first. They were never touched.

The range is therefore orphaned in both directions: **invisible to the timeline, and immune to retention.**

### Why the catalog lost its early history

No log corpus covers mid-July (the earliest is `logs4`, starting Jul 28), so the reset is not directly observable. The known `beta-global-2.4` defect documented in `nxdb-catalog-timeline-fix.md` is precisely this failure mode: at rotation the plugin uploaded a **16-byte empty `.nxdb`** to Wasabi and deleted the populated prior generation from the cloud, leaving the only complete index in `%TEMP%`. Any temp wipe, host move, or restart that fell back to the S3 copy would resume from an empty catalog and index only forward from that point.

**That defect is fixed in the running 2.5 build** (see [Verified healthy](#verified-healthy)). This is residual damage from the older build, not an ongoing catalog fault.

### The plugin is not withholding the data

The server walks the full tree roughly every hour and the plugin returns the June objects correctly every time. Enumeration is healthy — **3,002,518 `S3FileInfoIterator::next` calls** across the corpus, reaching individual files:

| Path depth | Entries | Level |
|---|---|---|
| 10 | 2,868,986 | `.mkv` files |
| 9 | 120,031 | hour folders |
| 8 | 6,672 | day folders |
| 3–7 | ~6,800 | bucket / quality / camera / year / month |

Minimum depth is 3 with no stray single-token or malformed paths — independently confirming zero `S3FileInfoIterator` corruption. Only indexing is missing.

### Secondary impact on retention depth

The orphaned ~25 days occupy capacity Nx does not account for. With free space pinned at ~63.8 GB against ~80 GB/day of ingest, retention is trimming the *visible* archive to ~30 days while ~25 days of unmanaged data sits on the same storage. Reclaiming or indexing it directly affects how much history stays visible.

### Two related observations

- **No low-quality stream exists.** `…/low_quality/…` returns only `info.txt` (4,953 bytes) — zero `.mkv` objects anywhere in the corpus, for any camera.
- **Only 1 of 16 cameras has footage.** Sixteen camera folders exist under both quality trees, but only `068b9ee6f0610fe2544d9f168cf7efd1` contains video. The other fifteen (`132de07d-…`, `2fbe8965…`, `7ebb0a2e…`, `832c5b27…`, `8fd9dc89…`, `92-61-00-00-00-01` … `-07`, `aad2db23-…`) return only `info.txt`. If footage is expected from those cameras, none was ever written.

---

## Finding 2 — Upload dispatcher race (P0 code defect)

### Symptom

Recording stops reaching Wasabi entirely, `Local Folder is full!!` floods the log, and the Media Server eventually crashes. Recovers only on restart.

### Root cause

The `.nxdb` catalog is uploaded **synchronously** from `S3IODevice::flush` via `s3Client::uploadFile`, while `.mkv` segments go through the queued asynchronous `s3Client::fileUploadThread`. When the midnight `.nxdb` flush collides with a `.mkv` hand-off, **the upload dispatcher is lost and never respawns.**

### Healthy flow, for reference

```
S3IODevice::~S3IODevice           segment closed in %TEMP%
S3Storage::renameFile             → "added file to uploaded"
s3Client::fileUploadThread        → "added file in queue"        ← dispatcher picks it up
ThreadPool::adjustWorkerThreads   → detaching/detached worker
s3Client::fileUploadThread        → "Uploading file!!", <bytes>
s3Client::fileUploadThread        → "Successfully uploaded file:"
s3Client::fileUploadThread        → "Delete File:" (temp reclaimed)
ThreadPool::addWorker             → "Thread closed!!"
```

### Evidence — 2026-08-27 00:00:26

The synchronous `.nxdb` upload and the `.mkv` hand-off land in the same second:

```
00:00:26  S3IODevice::flush      uploadFile nxdb, size=,429320,  …--78.nxdb
00:00:26  S3Storage::renameFile  …/2026/08/26/23/1787803153026.mkv → …_73107.mkv
00:00:26  S3Storage::renameFile  added file to uploaded  …1787803153026_73107.mkv
00:00:26  s3Client::uploadFile   Uploading file!!        …--78.nxdb
00:00:26  s3Client::uploadFile   Successfully uploaded   …--78.nxdb
00:01:32  S3Storage::renameFile  added file to uploaded  …1787803226133_66442.mkv
00:02:37  S3Storage::renameFile  added file to uploaded  …1787803292575_64262.mkv
```

`1787803153026_73107.mkv` never receives `added file in queue`, and neither does any segment after it. **Zero `fileUploadThread` lines exist between 00:02 and the 06:22 crash.** Identical signature on 2026-08-23 at 00:00:27 (`--72.nxdb` colliding with `1787457558159_69030.mkv`), followed by 16 hours of no upload activity.

### Confirmed as a race, not deterministic

On nights that survived, the two operations do not overlap in the same instant:

| Night | `.nxdb` flush | `.mkv` hand-off | Outcome |
|---|---|---|---|
| Aug 24 | 00:00:45 | renameFile 00:00:**44** (before flush) | survived — queued 00:00:47 |
| Aug 26 | 00:00:23 | 00:00:**50** (27 s later) | survived — queued 00:00:51 |
| **Aug 23** | 00:00:27 | 00:00:**27** (same second) | **dispatcher died** |
| **Aug 27** | 00:00:26 | 00:00:**26** (same second) | **dispatcher died** |

Roughly one night in three loses the race.

### Cascade

```
dispatcher dies at midnight
  → segments accumulate in C:\Windows\TEMP\Nx Storage (nothing uploaded, nothing reclaimed)
  → 2 GB / 63 MB ≈ 30 segments ≈ 30 min later: "Local Folder is full!! No space available. stop writing"
  → S3Storage::open refuses every write; Nx records nothing to this storage
  → tight retry loop (45,958 log lines on Aug 23) until mediaserver crashes
  → restart: ClearMemoryManager::freeTempStorage DISCARDS the pending backlog
```

Timing confirms the mechanism precisely:

| Night | Dispatcher died | First `Local Folder is full` | Delay | Crash / restart | Backlog discarded |
|---|---|---|---|---|---|
| Aug 23 | 00:00:27 | 00:33:03 | 32 min | 16:26:18 (crash) | **30 segments** |
| Aug 27 | 00:00:26 | 00:29:56 | 29 min | 06:22:48 (crash) | **28 segments** |
| Aug 15 | before corpus | 01:15:32 | — | 12:53:52 | **39 segments** |

Normal restarts discard exactly 1 segment (the open one). These three discarded 28–39 — recorded video destroyed. 114 segments were lost this way across the corpus.

### Crash dumps corroborate

All five minidumps were parsed (ExceptionStream + ModuleList). Every dump is `0xC0000005` (`STATUS_ACCESS_VIOLATION`). Two distinct fault sites appear:

| Dump | Timestamp (local) | Correlates with | Exception | Fault module / offset | Detail |
|---|---|---|---|---|---|
| `sent_…_9568.dmp` | 2026-08-23 16:26:18 | **End of the Aug 23 stall** | `0xC0000005` | `nx_utils.dll + 0x1F0B5` | Write to NULL (`ExceptionInformation` = write, address `0x0`) |
| `sent_…_22324.dmp` | 2026-08-27 06:22:48 | **End of the Aug 27 stall** | `0xC0000005` | `nx_utils.dll + 0x1F0B5` | Same NULL-write site |
| `sent_…_23820.dmp` | 2026-08-23 22:54:42 | Restart after Aug 23 stall | `0xC0000005` | `nx_utils.dll + 0x1F0B5` | Same NULL-write site |
| `…_19560.dmp` | 2026-08-21 00:00:02 | Midnight restart | `0xC0000005` | `nx_utils.dll + 0x1F0B5` | Same NULL-write site |
| `…_19088.dmp` | 2026-08-19 10:40:48 | Restart after `ClearMemoryManager` race | `0xC0000005` | **`s3_storage_plugin.dll + 0xCD84`** | Read of `0xFFFFFFFFFFFFFFFF` |

**Interpretation**

- **Stall-terminating crashes (Aug 23 16:26, Aug 27 06:22)** are confirmed access violations, not clean service stops. Both fault at the **same** instruction in `nx_utils.dll` (`+ 0x1F0B5`) with a NULL write — consistent with Media Server collapsing under the plugin’s buffer-full retry storm after the upload dispatcher died.
- **Two additional dumps** (Aug 21 00:00, Aug 23 22:54) hit that **identical** `nx_utils.dll` site, so the NULL-write path is a recurring Media Server failure mode in this corpus, not a one-off.
- **Aug 19 10:40** is a **separate** failure: AV inside `s3_storage_plugin.dll` itself, matching the log sequence where `ClearMemoryManager` deleted a temp `.mkv` while `S3Storage::open` / `S3IODevice::intialise` still held it (`INVALID_HANDLE_VALUE` → `Failed to open local file!!` → plugin reload three seconds later).
- Dump headers report OS **10.0.17763**, 12 CPUs, and loaded module lists of 198 entries including `s3_storage_plugin.dll` and the AWS CRT/S3 plugin dependencies — environment matches the plugin logs.
- No dump exists for the Aug 18 00:01 event (longest outage, ~33 h of plugin silence). The two unprefixed dumps (`…_19088`, `…_19560`) were never sent upstream.

### Impact — footage that never reached Wasabi

Expected ingest ~1,272 segments/day:

| Day | Uploaded | Hours with zero uploads |
|---|---|---|
| 2026-08-15 | 626 | 00:00 – 12:53 |
| 2026-08-16 | 1,275 | — |
| 2026-08-17 | 1,273 | — |
| 2026-08-18 | **0** | **entire day** |
| 2026-08-19 | 774 | 00:00 – 09:20 |
| 2026-08-20 | 1,272 | — |
| 2026-08-21 | 1,276 | — |
| 2026-08-22 | 1,274 | — |
| 2026-08-23 | 429 | **00:00 – 16:27** |
| 2026-08-24 | 1,271 | — |
| 2026-08-25 | 1,268 | — |
| 2026-08-26 | 1,278 | — |
| 2026-08-27 | 62 | 00:00 – 06:24, log ends 07:02 |

Four outage windows, **~69 hours total**:

- **Aug 15 00:00 → 12:53** (~12.9 h; began before the corpus starts, likely the Aug 14 midnight event)
- **Aug 18 00:01 → Aug 19 09:20** (~33.3 h; plugin entirely silent, no dump captured)
- **Aug 23 00:00 → 16:27** (~16.4 h)
- **Aug 27 00:00 → 06:24** (~6.4 h)

**Aug 15–26 shortfall: 12,016 uploaded vs ~15,264 expected — ~3,250 segments ≈ 206 GB ≈ 21% of the window never reached Wasabi.** Those hours are permanently absent from this storage. Because this is a backup-role storage, check whether the main local storage still holds them.

---

## Finding 3 — Retention deletes are normal (corrects the earlier report)

12,706 `.mkv` objects were deleted from Wasabi during the corpus. All were July footage, and the deletion date tracks the footage date with a consistent ~30-day lag:

| Delete date | Footage deleted | Segments | Lag |
|---|---|---|---|
| 2026-08-15 | 2026/07/13 – 07/16 | 3,789 | 30–33 d (catch-up) |
| 2026-08-17 | 2026/07/18 | 1,271 | 30 d |
| 2026-08-19 | 2026/07/19 – 07/20 | 2,541 | 30–31 d (catch-up) |
| 2026-08-20 | 2026/07/21 | 894 | 30 d |
| 2026-08-21 | 2026/07/22 | 1,250 | 30 d |
| 2026-08-23 | 2026/07/24 | 1,266 | 30 d |
| 2026-08-25 | 2026/07/26 | 1,272 | 30 d |
| 2026-08-27 | 2026/07/28 | 333 | 30 d |

The storage sits at its capacity ceiling — free space pinned at ~63.8 GB against ~80 GB/day of ingest — so Nx trims one oldest day for each new day. Thirty days is simply the depth that fits.

### Correction: this is not buffer-driven

`logs8-investigation-report.md` attributed these deletes to local buffer overflow. The correlation does not hold — delete rate is flat at ~1,270/day regardless of buffer state, and the worst buffer-crisis days show *fewer* deletes than quiet days:

| Day | Buffer-full events | `.mkv` deleted from S3 |
|---|---|---|
| 2026-08-17 | **0** | 1,271 |
| 2026-08-21 | **0** | 1,250 |
| 2026-08-25 | **0** | 1,272 |
| 2026-08-23 | **45,958** | 1,266 |
| 2026-08-27 | **15,129** | 333 |

Buffer overflow is a *downstream symptom* of Finding 2, not a cause of deletion. The deletes are routine server-issued retention calls arriving at ~1/minute:

```
[2026-08-23 00:20:13] S3Storage::removeFile  …/2026/07/24/00/1784866730945_72222.mkv
[2026-08-23 00:20:13] s3Client::removeUrl    deleted file …/2026/07/24/00/1784866730945_72222.mkv
[2026-08-23 00:21:26] S3Storage::removeFile  …/2026/07/24/00/1784866803167_72844.mkv
[2026-08-23 00:21:26] s3Client::removeUrl    deleted file …/2026/07/24/00/1784866803167_72844.mkv
```

One real side effect: after an outage, retention **catches up by deleting several days at once** (Aug 15 removed 4 footage-days, Aug 19 removed 2). Across the corpus 12,706 objects were deleted but only 12,078 uploaded, so the visible archive is net shrinking — losing footage from both ends.

---

## Verified healthy

The catalog rotation defect from `nxdb-catalog-timeline-fix.md` **is fixed in `beta-global-2.5`**. The premature 16-byte upload no longer occurs:

```
00:01:04  S3IODevice::flush    nxdb create deferred upload, size=,16,  …--73.nxdb   ← deferred, not sent
00:01:04  S3IODevice::flush    uploadFile nxdb, size=,402712,          …--73.nxdb   ← full catalog sent
00:01:04  s3Client::uploadFile Successfully uploaded                   …--73.nxdb
00:01:04  s3Client::removeUrl  deleted file                            …--72.nxdb   ← only after successor durable
```

| Check | Result |
|---|---|
| `.nxdb` generations | 19 rotations, `--59` → `--78`, all uploads 376–443 KB, never 16 B on S3 |
| Delete ordering | Prior generation removed only **after** successor uploads successfully |
| `No previous DB files found` | 0 occurrences |
| `S3FileInfoIterator` path corruption | 0 hits (3M+ traversal entries, no malformed paths) |
| Upload race `File do not exist to uplaod` | 0 hits |
| S3 / network upload failures | **0** — Wasabi connectivity is not a factor |
| Missing `_db_ref.guid` (404 each init) | **Expected** — backup-role storage without `cap::DBReady`; probe with no following write is correct |

Complete error inventory across 229 files: 19 `.nxdb` 404s and 18 `_db_ref.guid` 404s (both expected probes), 18 `Invalid json` on `S3Storage` ctor (harmless noise), and 5 one-off local file/lock errors.

**Residual weakness:** `.nxdb` only reaches S3 at midnight and at restarts, so the cloud copy can be up to ~24 h stale. Harmless while the local temp copy survives, but a `%TEMP%` wipe would leave the bucket index a day behind — the same exposure that produced Finding 1 under the older build.

---

## Other contributing issues

**Shared staging directory.** Every init loads `UploadList.json` for **10 different server/bucket configs** (`nx-21`, `nx-22`, `nx-22-june11`, `nx-old-june18`, `perf-may04-*`, `perf-may08-back-*`, `wsc-apl24-*`, `wsc-apl28-03`, `wsc-apl28-04`, `wsc-apl30-*`) from one `C:\Windows\TEMP\Nx Storage` root. At 63 MB/segment the 2 GB buffer holds only ~30 segments before any other config consumes space.

**`ClearMemoryManager` races** (5 occurrences) — cleanup deleting a file Nx is opening. Example, Aug 19 10:40:45: `Delete File: …71355.mkv` → `S3Storage::open` same file → `Failed to get file size INVALID_HANDLE_VALUE` → `Failed to open local file!!` → plugin reload. Dump `…_19088.dmp` at 10:40:48 confirms this as an access violation inside `s3_storage_plugin.dll + 0xCD84` (read of `0xFFFFFFFFFFFFFFFF`), not only a soft reload. Secondary to Finding 2, but a real crash path under load.

---

## Remediation plan

### Phase 1 — Recover the orphaned archive (Finding 1)

Order matters here.

1. **Raise the declared capacity in `s3.config` first.** Once the catalog knows about June 18 – July 12, retention will see the storage far over its limit and immediately purge the oldest footage to get back under — which is exactly the data being recovered. Reindexing without headroom will delete it for real.
2. **Run "Rebuild archive index"** on the Wasabi storage (Nx Desktop → Server Settings → Storage Management → Reindex archive). The safety gate from `log-signatures.md` — do not reindex until `S3FileInfoIterator` corruption is ruled out — is **satisfied**: zero corruption hits.
3. Verify June 18 – July 12 appears on the timeline and confirm retention settles at the intended depth.
4. Confirm the intended retention depth server-side; plugin logs cannot show Nx's configured max-days.

### Phase 2 — Code fixes for the sync defect (Finding 2, P0)

1. **Serialize the `.nxdb` and `.mkv` upload paths.** Route the `S3IODevice::flush` `.nxdb` upload through the same queue as `.mkv`, or guard both with a single `s3Client` mutex. This is the direct fix for the race.
2. **Make the dispatcher self-healing.** `addFileToUploadInQueue` / `fileUploadThread` must detect a dead worker and respawn it. A watchdog that alarms when `added file to uploaded` occurs without a matching `added file in queue` within N seconds would have caught all four outages within a minute.
3. **Never discard a pending backlog on restart.** `ClearMemoryManager::freeTempStorage` should re-queue unuploaded `.mkv` files rather than delete them.
4. **Decouple buffer state from write refusal.** Per `space-and-buffer.md`, buffer-full should apply `IODevice::write()` back-pressure or fail `isAvailable()` — not spin `S3Storage::open` 45,958 times while silently dropping recording.
5. **Harden `ClearMemoryManager`** so it never deletes files that are open or queued for upload. Dump `…_19088.dmp` shows the race ends as an AV in `s3_storage_plugin.dll + 0xCD84`.
6. **~~Analyse the stall-terminating dumps~~ (done 2026-08-31).** `sent_…_9568.dmp` and `sent_…_22324.dmp` are both `0xC0000005` NULL writes at `nx_utils.dll + 0x1F0B5`, taken while `S3Storage::open` was spinning on `Local Folder is full!!`. Follow-up with symbols: resolve `nx_utils.dll + 0x1F0B5` and `s3_storage_plugin.dll + 0xCD84` to source functions; check whether the shared Media Server site is a null callback / destroyed object left by the dead upload dispatcher.

### Phase 3 — Operational mitigations (immediate, no code change)

1. **Raise `local_buffer`** to 20 GB+ and move staging off `C:\Windows\TEMP` to a dedicated data disk. At 63 MB/segment, 2 GB is ~30 minutes of headroom — far too little to ride out a stall.
2. **Give each server config its own staging root** instead of sharing one folder across 10 configs.
3. **Interim watchdog:** alert if no `Successfully uploaded file …mkv` appears for >10 minutes, and restart the Media Server on trigger. Every outage here was restart-recoverable; they lasted 6–33 h only because nothing detected them.
4. **Check Wasabi object versioning** before treating the deleted July footage or the missing August hours as unrecoverable.
5. **Verify main storage** for the four outage windows — this is a backup-role target, so the primary may still hold that footage.

---

## Data impact summary

| Surface | Impact |
|---|---|
| **Recoverable** | ~25 days (2026-06-18 → 2026-07-12) in the bucket, unindexed. Recoverable by reindex, provided capacity is raised first. |
| **Permanently lost** | ~3,250 segments (~206 GB, ~21% of Aug 15–26) never uploaded. Includes 114 segments discarded from temp on restart. Check main storage. |
| **Deleted by design** | 12,706 segments, July 13–28, ~30-day retention at capacity. Check Wasabi versioning if recovery is desired. |
| **Availability** | ~69 h of no archive writes across 4 windows; 5 Media Server crashes (all `0xC0000005`; 4× `nx_utils.dll + 0x1F0B5`, 1× `s3_storage_plugin.dll + 0xCD84`); longest outage ~33 h (Aug 18 → 19). |
| **Catalog** | Healthy in the current build. Generations `--59` → `--78`, correct sizes and delete ordering. |

---

## Appendix — method and key evidence

**Signature analysis** followed the `wasabi-nx-troubleshoot` skill workflow; catalog and `db_ref.guid` semantics per `wasabi-nx-storagedb`. All counts were re-measured on this corpus rather than carried over from prior reports.

| File | Why it matters |
|---|---|
| `log_2026-08-23_05-52-25.txt` | Dispatcher death at 00:00:27; buffer-full onset 00:33 |
| `log_2026-08-27.txt` | Dispatcher death at 00:00:26; crash 06:22; clean recovery after |
| `log_2026-08-23_16-26-09.txt` | Aug 23 crash tail inside buffer-full loop |
| `log_2026-08-15_23-54-10.txt` | June 18 footage listed from S3 with real object sizes |
| `log_2026-08-17_01-07-42.txt` | Full scan pass — 16 cameras, month/day tree, `low_quality` empty |
| `log_2026-08-19_10-27-49.txt` | Post-outage retention catch-up (2 footage-days) |
| `crash1/crash1/*.dmp` | 5 dumps parsed: 4× AV NULL-write at `nx_utils.dll + 0x1F0B5` (incl. Aug 23/27 stall ends); 1× AV in `s3_storage_plugin.dll + 0xCD84` (Aug 19 ClearMemoryManager race) |

**Superseded documents**

- `logs8-investigation-report.md` — earlier analysis. Its primary verdict (buffer-driven cloud purge) is corrected by Finding 3; it also states no crash dumps were included, but five are present.
