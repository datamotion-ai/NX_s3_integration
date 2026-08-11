# Local buffer vs archive space APIs

Authoritative guidance for why Media Server deletes oldest footage when the plugin’s local staging buffer overflows, and how the plugin should report space vs write failures. Prefer this over treating buffer-full as “bucket full.”

## Why the deletion happens

`getFreeSpace()`, `getTotalSpace()`, and the space error codes (`NotEnoughSpace`, `SpaceInfoNotAvailable`) describe the archive destination's capacity. The mediaserver uses those values to drive archive retention: when it believes the destination is low on or out of space, it deletes the oldest footage it manages to make room. So when the plugin returns `SpaceInfoNotAvailable` because the local buffer overflowed, the server reads that as "I cannot determine the archive's free space while it appears full" and falls back to its storage-full cleanup routine. The buffer state is being reported on the wrong channel.

## What to do when the local buffer is full

Three principles:

1. Keep the space APIs tied to the bucket only. `getFreeSpace()` and `getTotalSpace()` should always report the bucket's real (or intended) capacity and must never be influenced by the local buffer. As long as they return healthy bucket space, the server has no reason to run the retention/cleanup routine. Do not return `NotEnoughSpace` or `SpaceInfoNotAvailable` from these methods because the buffer is full — reserve `SpaceInfoNotAvailable` strictly for a genuine inability to query the bucket's capacity.

2. Prefer back-pressure at the write path. A full buffer is a throughput condition — media is arriving faster than it can be uploaded and drained. The cleanest handling is for `IODevice::write()` to block/throttle until the buffer drains enough to accept the data, which slows the recorder rather than losing footage or signaling a space problem. Sizing the buffer adequately and hosting it on a fast, roomy disk (not `/tmp` or a tmpfs mount) is part of this.

3. If a write genuinely cannot proceed, signal it as a storage failure, not a space shortage. Return a generic I/O write error from `IODevice::write()` — in this SDK that is `nx_spl::error::UnknownError` (note the storage SDK's error enum does not have an "ioError" value; that belongs to the separate analytics SDK), with a short/zero byte count — or take the storage temporarily offline by having `isAvailable()` return 0 (or `StorageUnavailable`). The server treats an unavailable storage or a write I/O error as a storage/throughput fault: it holds frames in its own internal buffers, drops them if the condition persists, logs the I/O error, and stops recording to or fails over that storage — but it does not delete archive to reclaim space. Back-pressure (point 2) is preferable to repeatedly flapping availability, so use this path when a write truly cannot be accepted rather than as the routine response to transient fullness.

The rule of thumb: space errors drive retention; availability and I/O errors drive failure handling. A buffer overflow belongs in the second category, never the first.

## Notifying the user when recording stops

There are two ways to surface this, and they complement each other.

### Option A — built-in Storage Issue event (passive)

With the change in point 3, when the plugin reports the storage as unavailable or returns write I/O errors, the mediaserver raises its built-in Storage Issue event (this was called "Storage Failure" in earlier versions and still appears under that name in some preconfigured rules). Nx ships a default rule that shows a notification to all users and emails the admin when that event fires, and an admin can extend it in the Event Rules. This requires no extra work in the plugin, but the alert is generic — it tells the operator the storage has a problem, not specifically that your buffer overflowed.
