# Read Pico USB TF-card export

This component exports only the removable TF card as a USB mass-storage device.
It never exports internal flash or writes PMU/eFuse configuration.

The ESP32-S3 internal USB PHY is shared with the normal USB Serial/JTAG console.
Entering file transfer changes USB enumeration; the console returns after a
successful stop. No additional CDC interface is enabled. USB host configuration
is reported as `connected`; without a wired VBUS monitor it is not a guaranteed
physical cable-presence signal.

## Ownership and caller responsibilities

1. Initialize the component once with `read_pico_usb_init()`.
2. Stop/drain wireless transfers and every local TF consumer, including open
   books and font files. Keep them stopped while status `active` is true.
3. Call blocking `read_pico_usb_start()` on the maintenance worker. It obtains
   the BSP raw-card lease, records the observed media generation and starts a
   dedicated USB callback task. The normal filesystem remains unavailable.
4. Ask the user to eject the disk from the computer before leaving transfer.
   Host eject marks media unavailable but does not itself remount the card.
   Pulling the cable or stopping without host eject cannot flush host caches.
5. Call blocking `read_pico_usb_stop()`. It first stops admission and cooperatively
   joins the USB callback task, then disables OTG, restores Serial/JTAG, releases
   the raw lease, and requests a nonformatting remount/read/write verification.
6. Resume local consumers only after `ESP_OK`. A failed stop preserves remaining
   ownership and can be retried with the same stop API. Never force-delete the
   task, free the scratch buffer or release the raw lease on a timeout.

An observed removal/reinsertion invalidates the original raw handle for the
entire export session. It cannot recognize a swap with no observed CD transition.
Explicitly stop and restart export to use a replacement card. A failed remount
never triggers formatting.

## Storage and USB implementation

The dependency is pinned to official `esp_tinyusb` 2.0.1~1, with its resolved
TinyUSB core. Only the core is used. The wrapper filesystem helper must remain
unlinked because it remounts on USB callbacks and can acknowledge buffered
writes before SD completion. All writes here finish synchronously, including
read/modify/write for partial sectors. Bounds use 64-bit byte arithmetic. The
scratch buffer is 4096 bytes of DMA-capable internal memory. Aligned data uses
up to eight 512-byte SD sectors per raw read/write; partial first/last sectors
retain read/modify/write semantics. No read-ahead or deferred-write cache is
introduced.

`CONFIG_TINYUSB_MSC_BUFSIZE=4096` lets the TinyUSB core deliver eight sectors
per callback while the full-speed bulk endpoints remain 64 bytes. Each batch
checks the real card-presence state and media generation once before I/O;
longer callbacks recheck between bounded batches. A 4096-byte aligned read
therefore makes one SD read and one presence query, instead of eight SD reads
and sixteen presence queries. An observed card removal/generation change still
revokes the entire export lease, and stop joins any in-flight synchronous batch
before releasing its card or buffer. Increasing the buffer is a throughput
change, not proof that a particular host filesystem will mount successfully.

Descriptors present one MSC interface, bulk endpoints 0x01/0x81, VID 0x303A,
PID 0x4002, and a serial derived from the factory MAC. The official core returns
MAX_LUN minus one itself, so the application callback returns one LUN.

## Verification

Run `python3 tools/test_pico_usb.py` from the firmware root. It compiles this
production source against host SDK doubles in normal and ASan/UBSan modes.
The host test exercises real callbacks, eight-sector batches and their query
counts, byte-preserving partial writes, exact media bounds, removal between
batches,
SD errors, media invalidation, asynchronous write shutdown, cooperative timeout,
USB/PHY failures and retryable remount, plus repeated enter/leave cycles.

These checks do not prove real USB enumeration, host filesystem/cache behavior,
physical card I/O, console restoration or display usability. The final target
build must also confirm that the wrapper helper objects are absent and the app
fits its partition. The actual device still needs USB drag-and-drop, host eject,
return to reading and re-enumerated debug-console acceptance.
