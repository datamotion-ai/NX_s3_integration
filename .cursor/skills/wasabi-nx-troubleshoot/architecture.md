# Architecture reference

Source of truth: `samples/s3_storage_plugin/src/` and repo `README.md`.
SDK interfaces: `src/storage/third_party_storage.h`, `src/plugins/plugin_api.h`.
Field-validated behavior: `samples/docs/logs8-findings-report.md` (`VERSION` `beta-global-2.5+`).

## Components

| Component | File(s) | Role |
|-----------|---------|------|
| Plugin entry | `S3_library.cpp` | `createNXPluginInstance()` — init logger, `ServerManager`, `ClearMemoryManager`, return factory |
| `S3StorageFactory` | `S3_library.*` | Creates `S3Storage`; AWS SDK init/shutdown; type `"s3"` |
| `S3Storage` | `S3_library.*` | Open, list, remove, rename, space, capabilities |
| `S3IODevice` | `S3_library.*` | Local `FILE*` I/O; flush/close queues or performs upload |
| `S3FileInfoIterator` | `S3_library.*` | Directory listing iteration for Media Server scans |
| `s3Client` | `s3Client.*` | AWS S3 ops, upload queue, upload + space threads |
| `ServerManager` | `ServerManager.*` | `env.config`, buffer GB, license gate, upload parallelism |
| `ClearMemoryManager` | `ClearMemoryManager.*` | Periodic local temp cleanup; tracks active writers |
| `ThreadPool` | `ThreadPool.*` | Parallel upload workers |
| `DailyLogger` | `daily_loger.*` | Rotating diagnostic logs |

## Data paths

### Nx archive layout (main and backup)

Identical structure under each storage root (Nx archive portion starts at `<server GUID>/`):

```text
<storage root>/<server GUID>/<hi_quality|low_quality>/<camera ID>/<year>/<month>/<day>/<hour>/<epoch_ms>_<duration_ms>.mkv
```

- Wasabi leading `bucket/subpath/` is configuration, not Nx archive structure.
- `<server GUID>/` exists on local main and Wasabi backup alike.
- Camera folder = MAC when reported, else camera ID.
- Segment filenames differ across main vs backup because each location segments independently — expected; no setting forces identical names.

Details: skill `wasabi-nx-storagedb` (Archive path layout).

### Write (record)

1. Media Server → `S3Storage::open(uri, write flags)`.
2. If local staging over `local_buffer`, open may log `Local Folder is full!!` and refuse writes (tight retries under load).
3. Local file created under `Nx Storage` (URI slashes → underscores in temp name; read/write prefixes avoid collisions).
4. `S3IODevice::write` appends locally; `ClearMemoryManager` marks file active.
5. Close/rename → `.mkv` entered for async upload (`added file to uploaded` → `fileUploadThread` / `UploadList.json`).
6. Background `fileUploadThread` + `ThreadPool` PUT to Wasabi.

**Race hazard:** generational `.nxdb` flush often calls `s3Client::uploadFile` **synchronously** from `S3IODevice::flush` while `.mkv` uses the async dispatcher. Same-second collision can kill the dispatcher (no further `added file in queue`) until process restart — see [log-signatures.md](log-signatures.md) issue 1.

### Read (playback)

1. `open` for read → download via `s3Client::downloadFile` if local miss.
2. Reads/seeks on local `FILE*`.
3. Destructor may schedule non-`.nxdb` read temps for cleanup.

### Free space

`getFreeSpace` / `getTotalSpace` must describe **bucket (archive destination) capacity only**.

- Intended: report real/configured bucket capacity; never fold in local staging/`local_buffer` state.
- Risk: folding buffer-full into `SpaceInfoNotAvailable` / collapsed free space can drive retention deletes (wrong channel). Separately, **true** low free space vs ingest causes normal rolling retention.
- Buffer full → back-pressure in `IODevice::write()`, or I/O/`isAvailable` failure — never space errors. Details: [space-and-buffer.md](space-and-buffer.md).
- `totalSpace` from matching `s3.config` entry, else default large placeholder.

### Delete

`S3Storage::removeFile` → local unlink (as needed) + `s3Client::removeUrl` (S3 DeleteObject).
Success logs: `deleted file` from `removeUrl`.

For generational `.nxdb` (`*--N.nxdb`): cloud remove may be **deferred** until successor generation exists on S3 at size ≥ `MIN_NXDB_BYTES` (`common.hpp`). Logs: `nxdb remove deferred…`, then remove after successor upload.

### Index / DB files

- `.nxdb` — per-storage media catalog (`<serverGuid>--N.nxdb`); StorageDb vacuums via generational swap (`wasabi-nx-storagedb`).
- **2.5+ durability:** do not PUT shells `< MIN_NXDB_BYTES` (typically 16-byte headers); log `nxdb create deferred upload`; re-upload on growth (`NXDB_UPLOAD_GROWTH_BYTES`) / date change when `sync_nxdb` enabled; remove prior only after successor durable.
- Timeline uses the catalog, not a full bucket walk — objects can exist in S3 without timeline visibility if never indexed.
- `_db_ref.guid` / `db_ref.guid` — database-host marker. With `cap::DBReady` omitted, server probes on startup but does not write; 404 every init is expected for backup role.
- Residual: if `.nxdb` only flushes at midnight/restart, cloud catalog can lag local temp by up to ~24 h.

## Config

| File | Location | Purpose |
|------|----------|---------|
| `env.config` | Beside mediaserver binary | `local_buffer`, `log_level`, `log_max`, `max_parallel_upload`, `sync_nxdb` |
| `s3.config` | Beside mediaserver binary | Logical size per endpoint/bucket for free-space / retention math |

Storage URL shape:

```text
s3://<access_key>:<secret_key>@<endpoint>/<bucket>
```

## Staging & logs on disk

| Path | Purpose |
|------|---------|
| `%TEMP%/Nx Storage` or `$TMP/Nx Storage` | Local staging (often **shared** across configs — capacity contention) |
| `./logs/log_YYYY-MM-DD.txt` | DailyLogger (cwd of mediaserver) |
| `UploadList.json` (as used by s3Client) | Persistent upload queue across restarts |

## Capabilities advertised

List, read, remove, write (while under buffer). `cap::DBReady` is intentionally omitted so Wasabi stays a non-database-host backup target (`isSystem: 0`, role `'backup'`); local system storage remains the DB host. Do not add `DBReady` unless database hosting over S3 is explicitly required.

## Background timers

| Mechanism | Cadence | Purpose |
|-----------|---------|---------|
| `ServerManager` | ~1–10 min | Reload config / license |
| `ClearMemoryManager` | ~1 min | Delete orphaned local temps (must not delete open/queued files) |
| Upload thread | continuous | Drain queue — must survive `.nxdb` sync uploads |
| Space thread | periodic | Refresh remote size |
