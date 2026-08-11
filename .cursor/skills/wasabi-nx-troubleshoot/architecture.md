# Architecture reference

Source of truth: `samples/s3_storage_plugin/src/` and repo `README.md`.
SDK interfaces: `src/storage/third_party_storage.h`, `src/plugins/plugin_api.h`.

## Components

| Component | File(s) | Role |
|-----------|---------|------|
| Plugin entry | `S3_library.cpp` | `createNXPluginInstance()` — init logger, `ServerManager`, `ClearMemoryManager`, return factory |
| `S3StorageFactory` | `S3_library.*` | Creates `S3Storage`; AWS SDK init/shutdown; type `"s3"` |
| `S3Storage` | `S3_library.*` | Open, list, remove, rename, space, capabilities |
| `S3IODevice` | `S3_library.*` | Local `FILE*` I/O; flush/close queues upload |
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
2. If local staging over `local_buffer`, open may log `Local Folder is full!!` and fail write.
3. Local file created under `Nx Storage` (URI slashes → underscores in temp name; read/write prefixes avoid collisions).
4. `S3IODevice::write` appends locally; `ClearMemoryManager` marks file active.
5. Close/flush → `s3Client::uploadFile` / queue (`UploadList.json` for resume).
6. Background `fileUploadThread` + `ThreadPool` PUT to Wasabi.

### Read (playback)

1. `open` for read → download via `s3Client::downloadFile` if local miss.
2. Reads/seeks on local `FILE*`.
3. Destructor may schedule non-`.nxdb` read temps for cleanup.

### Free space

`getFreeSpace` / `getTotalSpace` must describe **bucket (archive destination) capacity only**.

- Intended: report real/configured bucket capacity; never fold in local staging/`local_buffer` state.
- Current risk: if local staging ≥ `local_buffer`, code may log `local folder full:<bytes>` and return `SpaceInfoNotAvailable` / collapse free space — mediaserver then runs storage-full retention and `removeFile`/`removeUrl` deletes remote `.mkv`. That is buffer state on the wrong channel.
- Buffer full → back-pressure in `IODevice::write()`, or I/O/`isAvailable` failure — never space errors. Details: [space-and-buffer.md](space-and-buffer.md).
- `totalSpace` from matching `s3.config` entry, else default ~10 TB.

### Delete

`S3Storage::removeFile` → local unlink (as needed) + `s3Client::removeUrl` (S3 DeleteObject).
Success logs: `deleted file` from `removeUrl`.

### Index / DB files

- `.nxdb` — per-storage media catalog segments (`<serverGuid>--N.nxdb`); StorageDb vacuums via generational swap (see skill `wasabi-nx-storagedb`).
- `_db_ref.guid` / `db_ref.guid` — database-host marker. With `cap::DBReady` omitted, server probes on startup but does not write it; 404 / `UrlNotExists` every init is expected for backup role.
- `sync_nxdb` in `env.config` controls date-change flush/upload behavior for `.nxdb`.

## Config

| File | Location | Purpose |
|------|----------|---------|
| `env.config` | Beside mediaserver binary | `local_buffer`, `log_level`, `log_max`, `max_parallel_upload`, `sync_nxdb` |
| `s3.config` | Beside mediaserver binary | Logical size per endpoint/bucket for free-space math |

Storage URL shape:

```text
s3://<access_key>:<secret_key>@<endpoint>/<bucket>
```

## Staging & logs on disk

| Path | Purpose |
|------|---------|
| `%TEMP%/Nx Storage` or `$TMP/Nx Storage` | Local staging for all open files |
| `./logs/log_YYYY-MM-DD.txt` | DailyLogger (cwd of mediaserver) |
| `UploadList.json` (as used by s3Client) | Persistent upload queue across restarts |

## Capabilities advertised

List, read, remove, write (while under buffer). `cap::DBReady` is intentionally omitted so Wasabi stays a non-database-host backup target (`isSystem: 0`, role `'backup'`); local system storage remains the DB host. Do not add `DBReady` unless database hosting over S3 is explicitly required.

## Background timers

| Mechanism | Cadence | Purpose |
|-----------|---------|---------|
| `ServerManager` | ~1–10 min | Reload config / license |
| `ClearMemoryManager` | ~1 min | Delete orphaned local temps |
| Upload thread | continuous | Drain queue |
| Space thread | periodic | Refresh remote size |
