---
name: wasabi-nx-storagedb
description: >-
  Explains Nx StorageDb .nxdb generational vacuum (catalog compact/swap),
  orphan S3 archives not in the catalog, beta-global-2.5+ deferred tiny-catalog
  upload and successor-before-remove, db_ref.guid / cap::DBReady database-host
  vs backup-storage behavior, Nx archive path layout (main vs backup filenames),
  first-run init vs listing failures, and how to verify catalog persistence
  across restarts. Use when the user asks about .nxdb deletion/recreation,
  StorageDb, removeFiles exceptions naming --N.nxdb, “No previous DB files
  found”, footage in Wasabi missing on the timeline, 16-byte .nxdb uploads,
  db_ref.guid / _db_ref.guid probes without a following write, DbReady /
  isSystem / role backup vs main, whether index vacuum deletes video,
  advertising cap::DBReady on Wasabi/object storage, why Wasabi paths include
  bucket/prefix or server GUID, or why .mkv segment names differ between main
  and backup storage.
---

# Wasabi / Nx StorageDb Catalog Behavior

## When to use

Apply this skill when the user:

- Asks why `.nxdb` files are deleted and recreated
- Sees StorageDb / `removeFiles` log lines involving `--N.nxdb`
- Sees `nxdb create deferred upload` / `nxdb remove deferred` in plugin logs
- Asks about objects in Wasabi for dates the Nx timeline shows empty (orphan catalog)
- Asks about `db_ref.guid` / `_db_ref.guid` probes on startup (especially probe without a following write)
- Asks whether missing `db_ref.guid` or omitted `cap::DBReady` is a fault
- Worries that catalog vacuum deleted recorded video
- Sees “No previous DB files found. Starting with a new one” after footage was already recorded
- Asks why Wasabi object keys include a bucket/prefix or `<server GUID>` folder
- Compares main vs backup `.mkv` paths and wonders why segment filenames do not match one-to-one

For plugin-side DailyLogger diagnosis (dispatcher death, buffer cascade, iterator bugs), also use `wasabi-nx-troubleshoot`. Field synthesis: `samples/docs/logs8-findings-report.md`.

## How to answer

1. Treat generational `.nxdb` swap as **normal StorageDb vacuum**, not media deletion — when the successor is durable.
2. Treat **timeline visibility** as catalog-driven: S3 objects without `.nxdb` entries do not appear on the timeline and are invisible to retention.
3. Distinguish **first-run empty catalog**, **listing failure**, and **residual damage** from a past empty/tiny cloud catalog (pre-`2.5`).
4. Do not conflate this per-storage media catalog with the primary VMS DB. Without `cap::DBReady`, Wasabi is a non-database backup target; absence of a `db_ref.guid` write is expected.
5. Do not recommend advertising `cap::DBReady` on object storage unless the user explicitly wants database-host behavior and accepts in-place SQLite over S3.
6. Treat main vs backup path/filename differences as **expected** (bucket prefix + independent segmentation).
7. Prefer the domain sections below when explaining server intent.

---

## Database file deletion and generation

The deletion and recreation of the `.nxdb` file is the per-storage media catalog being compacted (a “vacuum”) by the server's StorageDb component. Because in-place SQLite edits cannot be safely performed on object storage, the server performs a generational swap — it writes a fresh, compacted generation of the catalog (e.g. `{guid}--2.nxdb`) and then removes the older generation (e.g. `{guid}--1.nxdb`), so that exactly one current catalog remains. In the log, “removeFiles: To remove {…}, exception: …--2.nxdb” indicates the generation being kept; the prior one is what gets removed.

### Plugin durability (`beta-global-2.5+`)

The plugin must not replace a populated cloud catalog with an empty shell:

- `MIN_NXDB_BYTES` (4096) — refuse / defer PUT of generational catalogs below this size (header-only ~16 B shells). Log: `nxdb create deferred upload, size=,…`.
- Growth / date-change flush may upload once the local catalog is large enough (`NXDB_UPLOAD_GROWTH_BYTES`, `sync_nxdb`).
- Cloud delete of `--N` is deferred until successor `--(N+1)` exists remotely at ≥ `MIN_NXDB_BYTES`. Log: `nxdb remove deferred…`, then `deleted file …--N.nxdb` after successor upload.

Healthy ordering in plugin logs:

```
nxdb create deferred upload, size=,16,  …--(N+1).nxdb
uploadFile nxdb, size=,<hundreds of KB>, …--(N+1).nxdb
Successfully uploaded …--(N+1).nxdb
deleted file …--N.nxdb
```

**Residual:** if cloud upload of `.nxdb` is mostly midnight/restart-only, the bucket copy can lag local `%TEMP%` by up to ~24 h. A temp wipe then resumes from a stale cloud index — same class of exposure that produced orphan archives under older builds.

## Storage impact

This process is strictly scoped to this specific Wasabi storage location. A StorageDb instance is per-storage, so it does not affect the catalogs of other storage volumes, and it is separate from the primary VMS system database (which is what `cap::DBReady` governs).

## Data retention vs vacuum

Vacuum deletion does **not** remove recorded archive `.mkv` files. The `.nxdb` is solely an index; video objects are stored separately. During a normal vacuum, existing records are carried forward into the new generation before the old one is removed.

If the log shows “No previous DB files found. Starting with a new one,” followed by “Nothing to write,” the server found no usable catalog and started empty — media already on S3 is not deleted by that message, but also not visible until re-indexed.

## Orphan archive (in bucket, not in catalog)

**Symptom:** Wasabi lists `.mkv` for dates the Nx timeline shows empty; retention never deletes those days (it always starts from the catalog’s earliest record).

**Meaning:** The timeline is driven by `<GUID>--N.nxdb`, not by bucket contents. Enumeration via `getFileIterator` / `getobjectKeys` can be healthy while indexing history starts later (e.g. catalog begins mid-July while objects exist from mid-June).

**Typical cause:** Historical bad rotation (pre-`2.5` uploaded ~16 B `--(N+1).nxdb` then deleted populated `--N`), then only forward indexing — residual damage, not necessarily an ongoing fault if current generations are large and ordered correctly.

**Recovery order (critical):**

1. **Raise declared capacity in `s3.config` first.** Reindexing older days without headroom makes retention see the storage as far over limit and purge the recovered oldest footage immediately.
2. Confirm `S3FileInfoIterator` has **zero** path-corruption hits (`wasabi-nx-troubleshoot` / log-signatures issue 6).
3. Run **Rebuild archive index** on that storage.
4. Verify older days appear on the timeline and retention settles at the intended depth.
5. Orphaned objects also consume capacity Nx does not account for — reclaiming or indexing them changes how much visible history fits.

## Purpose of db_ref.guid

This is a small marker/reference file the server uses to associate a storage location with a server database instance, allowing the server to recognize the storage and its catalog across restarts. Its detailed internal semantics are not part of the public Storage SDK surface, so from the plugin's perspective the key requirements are that it returns a clean “does not exist” result when the file is absent, and durably persists it once the server writes it.

`db_ref.guid` is the database-reference marker the server associates with a **database-hosting** storage. On startup the server probes for it (in part to detect whether a storage previously belonged to a different server's database), which is the existence check observed in logs. However, because a storage without `cap::DBReady` is not DbReady, the server does not promote it to a database host and therefore does not proceed to write `db_ref.guid` to it. A correct `fileExists` / `UrlNotExists` response is not being rejected — no write follows because the server is not attempting one.

## db_ref.guid when storage is not a database host

Regarding `db_ref.guid` behavior: based on the server log you provided (`main.log`), this is expected behavior rather than a fault, and it follows directly from the storage not being a database host.

In the server log, the database-ready determination appears only for the local storage:

  “QnFileStorageResource(…): DbReady is true for c:\HD Witness Media”

There is no equivalent line for the Wasabi storage. The storage manager also classifies the two differently — the local drive is the system storage (`isSystem: 1`, role `'main'`), while the Wasabi storage is a non-system backup storage (`isSystem: 0`, role `'backup'`, archive mode `'isolated'`). The server's own databases are opened locally on the system drive. In other words, the local storage is the database host; the Wasabi storage is not, because `cap::DBReady` is intentionally omitted from your `getCapabilities()` implementation.

Importantly, this does not impair the storage. The same server log shows the Wasabi storage being selected for recording (“Optimal storage selection: final result: s3://…/testinge”), running its cleanup routine, and holding a populated media catalog. It is operating correctly as a non-database backup target, which is the configuration this capability set is intended to produce.

So the key question is what behavior you are aiming for:

  • If the goal remains for this storage not to act as a database host (consistent with your original requirement), then the absence of the `db_ref.guid` write is the expected and correct result, and no change is needed. The storage is fully functional as-is.

  • If you specifically need the server to write and maintain `db_ref.guid` on this storage, that occurs for database-hosting storages, which requires advertising `cap::DBReady`. We would not recommend that for an object-storage target, as it re-enables database hosting over S3/Wasabi and reintroduces the in-place SQLite read/write behavior that object storage does not handle well — the behavior you set out to avoid.

If you can let us know what prompted the need for `db_ref.guid` specifically — for example, whether a downstream behavior appears to depend on it — we can advise further. Based on the logs provided, however, the storage is initializing and recording correctly without it.

## Initialization sequence

On startup the server probes for `db_ref.guid` and existing catalog files. Observing an existence request before any write is therefore expected. The critical requirement for the plugin is to return a clean “not found” / `UrlNotExists` result for that case rather than a blocking call or error.

For a **database-hosting** storage (`cap::DBReady`), a missing ref can be followed by the server writing `db_ref.guid`. For this Wasabi plugin configuration (no `cap::DBReady`), the probe alone is expected and **no write follows** — see the section above. Do not treat “probe without write” as a plugin persistence failure.

## Archive path layout (main vs backup)

Following up on the filename/path question with a clarification. Both your main and backup storage use the same documented Nx archive structure, so the naming scheme itself is identical in both locations:

  `<storage root>/<server GUID>/<hi_quality|low_quality>/<camera ID>/<year>/<month>/<day>/<hour>/<epoch_ms>_<duration_ms>.mkv`

The `<server GUID>` sub-directory is a standard part of this layout and is present on every storage location, including your local main storage — so it is not something that differs between main and backup. (The camera-level folder is the device's MAC address where reported, otherwise the camera ID, which is why it matches across both of your examples.)

With that in mind, the two differences you're seeing come down to:

1. Bucket prefix. The leading portion of the Wasabi path (`nx-22-june11/nx-22-june11/`) is simply your bucket name plus the subpath the storage was configured to point at. It is not part of the Nx archive structure — everything from `<server GUID>/` onward is.

2. Independent segmentation. The individual segment filenames do not match one-to-one between main and backup — for example, main `1781618464445_63230.mkv` versus backup `1781618461824_62342.mkv`. This is expected: the backup is written as its own set of recording segments, cut independently of the main storage, so the start timestamps and durations do not line up exactly. The filename format is the same; only the specific values differ, because each location segments its recording independently.

Neither of these affects functionality. The server writes, indexes, and plays back both layouts automatically, and the backup archive remains directly accessible and playable through the Client. There is no user-facing setting to change the on-disk filename layout or to force the backup to mirror the main storage's exact filenames — the per-segment naming follows from independent recording. No change is needed.

## Recommended verification

Because the log shows “No previous DB files found. Starting with a new one,” we'd recommend confirming this only occurs on a genuine first run:

1. Record a few minutes of footage directly to the Wasabi storage.
2. Restart the Nx Server.
3. Review the StorageDb log lines on startup.

If the restart shows the existing `.nxdb` being found and loaded — not “starting with a new one” — and the recordings remain visible in the timeline, the integration is behaving correctly. If “No previous DB files found” recurs after footage has been recorded, it indicates the plugin's `getFileIterator` / object-listing layer is not listing the previously written `.nxdb` back to the server (a listing or read-after-write consistency issue on the object-storage side) rather than a server problem. In that case the recorded media stays safely on the storage but remains unindexed until an archive re-index, and that listing logic would be the area to investigate.

Also verify in plugin DailyLogger after vacuum:

- No tiny (e.g. 16 B) generational `.nxdb` successfully uploaded to S3
- Prior generation removed only after successor upload success
- Zero recurring “No previous DB files found” after known footage

## Related plugin investigation

If verification fails (empty catalog after recorded footage) or orphans exist:

- Inspect `S3FileInfoIterator` / `getobjectKeys` listing of `--*.nxdb` (media catalog persistence/listing)
- Confirm durable upload and read-after-write of `.nxdb` generations (`MIN_NXDB_BYTES` path)
- Do **not** treat missing `*_db_ref.guid` as the root cause when `cap::DBReady` is omitted — that write is not expected
- Before reindex: raise `s3.config` capacity; rule out iterator corruption
- See `wasabi-nx-troubleshoot` for dispatcher death, buffer cascade, and other plugin-side failure modes
