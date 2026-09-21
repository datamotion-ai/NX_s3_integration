# Nx Witness + Wasabi S3 Plugin — Crash Dump Investigation (`logs9`)

**Subject:** Media Server crash dumps collected after the `logs8` corpus (Aug 27 – Sep 1, 2026)  
**Report date:** 2026-09-01  
**Method:** `wasabi-nx-troubleshoot` workflow — ExceptionStream + ModuleList parsing, memory-string scan, plugin-log correlation where available  
**Status:** Single recurring failure mode confirmed across all five dumps; root cause matches `logs8` Finding 2 (P0 upload-dispatcher race)

---

## Verdict

All five `mediaserver.exe` minidumps in `logs9/` terminate with the **same access violation**: `0xC0000005` (**write to address `0x0`**) at **`nx_utils.dll + 0x1F0B5`**. This is the identical fault site seen at the end of the Aug 23 and Aug 27 upload stalls in `logs8`. The crashes are **downstream of the Wasabi plugin’s midnight `.nxdb` sync upload racing the `.mkv` upload dispatcher** — not a new defect class, not an S3 connectivity failure, and not the Aug 19 `s3_storage_plugin.dll` / `ClearMemoryManager` race.

**Data impact:** Each crash ends a buffer-full / write-refusal episode. Pending `.mkv` segments in `%TEMP%/Nx Storage` are typically discarded on restart (`ClearMemoryManager::freeTempStorage`), so footage for the stall window never reaches Wasabi. Bucket/timeline impact is the same class as `logs8` Finding 2.

---

## Evidence

### Environment (from dump ModuleList)

| Item | Value |
|---|---|
| Process | `mediaserver.exe` **6.1.2.42921** (`40d5d24197ba`) |
| OS | Windows **10.0.17763** x64, 12 CPUs |
| Plugin | `s3_storage_plugin.dll` loaded in every dump |
| Shared fault module | `nx_utils.dll` base **`0x7FFE64190000`**, size **`0x67C000`** |
| Modules per dump | **198** (consistent across all five) |
| Plugin version (inferred) | `Wasabi/beta-global-2.5` — same build as `logs8`; no version string embedded in these dumps |

### Dump inventory — uniform exception profile

All dumps parsed via minidump **ExceptionStream** (thread context + `ExceptionInformation`):

| Dump (PID) | Approx. crash time† | Exception | Fault site | AV detail |
|---|---|---|---|---|
| `…_22324.dmp` | **2026-08-27 ~06:22** | `0xC0000005` | `nx_utils.dll + 0x1F0B5` | write @ `0x0` |
| `…_19860.dmp` | **2026-08-28 ~08:27** | `0xC0000005` | `nx_utils.dll + 0x1F0B5` | write @ `0x0` |
| `…_19900.dmp` | **2026-08-30 ~07:15** | `0xC0000005` | `nx_utils.dll + 0x1F0B5` | write @ `0x0` |
| `…_19116.dmp` | **2026-08-31 ~05:00** | `0xC0000005` | `nx_utils.dll + 0x1F0B5` | write @ `0x0` |
| `…_18080.dmp` | **2026-09-01 ~07:31** | `0xC0000005` | `nx_utils.dll + 0x1F0B5` | write @ `0x0` |

†Times from dump **LastWriteTime** and embedded DailyLogger timestamps in process memory where present. Minidump header timestamps are invalid (epoch placeholder) on all five files.

**Not observed in `logs9`:** `0xC0000409` stack-buffer overrun, or faults inside `s3_storage_plugin.dll` (contrast Aug 19 `…_19088.dmp` in `logs8`).

### Plugin-log correlation — Aug 27 (`…_22324.dmp`)

DailyLogger files under `logs8/logs7/logs7/` cover this crash end-to-end. Sequence matches **log-signatures.md issue 1** exactly:

**1. Midnight dispatcher death (same second):**

```
[2026-08-27 00:00:26] S3Storage::renameFile   added file to uploaded  …1787803153026_73107.mkv
[2026-08-27 00:00:26] S3IODevice::flush       uploadFile nxdb, size=,429320, …--78.nxdb
[2026-08-27 00:00:26] s3Client::uploadFile    Uploading file!!          …--78.nxdb   ← sync path
[2026-08-27 00:00:26] s3Client::uploadFile    Successfully uploaded     …--78.nxdb
```

After this point, **`log_2026-08-27_06-22-38.txt` contains zero `added file in queue` / `fileUploadThread` lines** until the post-crash restart (~06:23).

**2. Buffer-full cascade (~30 min later, issue 8):**

```
[2026-08-27 00:29:56] S3Storage::open   Local Folder is full!! No space available. stop writing
```

**15,110** buffer-full lines in that file alone.

**3. Crash terminus (matches dump PID 22324):**

```
[2026-08-27 06:22:17–06:22:36] S3Storage::open   Local Folder is full!! …
                              (log ends — process AV at nx_utils.dll + 0x1F0B5)
```

**4. Post-crash recovery:**

```
[2026-08-27 06:23:54+] fileUploadThread   added file in queue   (dispatcher alive again)
```

**Code path:** sync `S3IODevice::flush` → `s3Client::uploadFile` for `.nxdb` collides with async `fileUploadThread` / `renameFile` hand-off for `.mkv` (`S3_library.cpp`, `s3Client.cpp` — see `wasabi-nx-troubleshoot` architecture.md).

### Aug 28 – Sep 1 dumps — log gap

No DailyLogger files for **2026-08-28 onward** exist in this workspace. For `…_19860`, `…_19900`, `…_19116`, and `…_18080`:

- Fault site is **identical** to the proven Aug 27 stall termination.
- Embedded memory strings show **`S3Storage::open`** in three dumps (active open/retry path at crash).
- No evidence of a different exception class or plugin-module fault.

Given the **~1 crash per 1–2 days** cadence, same binary build, and same NULL-write site, these are **high-confidence continuations of issue 1 → issue 8**, most likely triggered at or shortly after midnight `.nxdb` flush on successive nights (the same ~1-in-3 midnight race rate measured in `logs8`).

---

## Root cause

| Layer | Finding |
|---|---|
| **Primary (plugin P0)** | Midnight (or date-change) **`.nxdb` sync upload races `.mkv` dispatcher hand-off** → `fileUploadThread` stops dequeuing → segments accumulate in 2 GB `local_buffer` staging |
| **Secondary (symptom)** | Staging fills in **~30 min** → `Local Folder is full!!` → `S3Storage::open` tight retry loop (**issue 8**) |
| **Terminal (process)** | Media Server **access violation** at shared **`nx_utils.dll + 0x1F0B5`** (NULL pointer write) while under load — not the plugin DLL itself in these five dumps |
| **Ruled out** | S3 upload failures, capacity-retention deletes as crash trigger, `ClearMemoryManager` plugin AV (issue 10 — seen once Aug 19, not in `logs9`) |

This is the **same root cause** documented as `logs8` Finding 2. The `logs9` corpus shows the defect **continued after Aug 27** with at least **four additional crashes in five days**.

---

## Impact

| Surface | Effect |
|---|---|
| **Wasabi bucket** | Stall windows produce **no new `.mkv` uploads**; restart may **discard dozens of pending temp segments** (28–39 per event in `logs8` measured restarts) |
| **Nx timeline (backup storage)** | **Gaps** for each outage; check **main/local storage** for the same hours |
| **Service availability** | **5 crashes / ~6 days** in this dump set alone; Aug 27 outage ~**6.4 h** before crash; prior `logs8` windows reached **6–33 h** |
| **Retention** | Cloud `.mkv` deletes during buffer-full episodes (e.g. Jul 28 footage deleted at 06:22:18 on Aug 27) are **normal capacity retention** (`logs8` Finding 3) — **not** caused by buffer overflow |

---

## Next actions

### Immediate (ops — no code change)

1. **Watchdog:** Alert when `added file to uploaded` occurs without `added file in queue` within 60 s, or when no `Successfully uploaded file …mkv` for **>10 min** → restart Media Server (every stall in `logs8`/`logs9` recovered only after restart).
2. **Raise `local_buffer`** to **20 GB+** and move staging off `C:\Windows\TEMP` to a dedicated disk (2 GB ≈ 30 min at ~63 MB/segment).
3. **Per-config staging roots** — ten bucket configs share one `Nx Storage` folder in `logs8`.
4. **Collect matching DailyLogger** for Aug 28 – Sep 1 to confirm midnight race signatures on the four newer dumps.

### Code fix (P0 — same as `logs8`)

1. **Serialize `.nxdb` and `.mkv` upload paths** — queue `.nxdb` through `fileUploadThread` or one `s3Client` mutex.
2. **Self-healing dispatcher** — detect dead `fileUploadThread` and respawn; never leave `added file to uploaded` without queue pickup.
3. **Do not discard upload backlog on restart** — re-queue temps instead of `ClearMemoryManager` delete.
4. **Back-pressure instead of open-spin** — fail `isAvailable()` / `write()` under buffer-full; do not loop `S3Storage::open` tens of thousands of times (reduces `nx_utils.dll` AV risk).
5. **Symbol follow-up:** resolve `nx_utils.dll + 0x1F0B5` with PDBs to identify the null write (likely destroyed callback/object after plugin stall).

---

## Appendix — method

**Workflow checklist (`wasabi-nx-troubleshoot`):**

- [x] Classify symptom → **process crash** after upload stall / buffer-full (not bucket-only or timeline-only)
- [x] Confirm plugin version / env → **6.1.2.42921 + beta-global-2.5** (inferred from `logs8` + module list)
- [x] Locate evidence → **5 × `.dmp` in `logs9/`**; plugin logs for Aug 27 in `logs8/logs7/logs7/`
- [x] Grep signature sequences → **issue 1 + issue 8** confirmed on Aug 27
- [x] Separate failure modes → **dispatcher death**, not retention/orphan catalog/iterator corruption
- [x] Map to code → `S3IODevice::flush` / `s3Client::uploadFile` vs `fileUploadThread`

**Key log files (Aug 27):**

| File | Role |
|---|---|
| `logs8/logs7/logs7/log_2026-08-27_06-22-38.txt` | Midnight race, zero queue pickup, 15k buffer-full lines, crash tail |
| `logs8/logs7/logs7/log_2026-08-27.txt` | Post-crash recovery — `added file in queue` from 06:23 |

**Relationship to prior reports:**

| Report | Relationship |
|---|---|
| `logs8/logs8-findings-report.md` | Primary field synthesis; `…_22324.dmp` analysed there — **`logs9` adds four new post-corpus crashes with the same fault site** |
| `logs8-investigation-report.md` | Superseded on retention attribution; crash dumps now confirmed |

**Evidence gap:** Provide `Wasabi_logs/` or `./logs/log_2026-08-*.txt` for **Aug 28 – Sep 1** to pin exact midnight race timestamps on `…_19860`, `…_19900`, `…_19116`, and `…_18080`.
