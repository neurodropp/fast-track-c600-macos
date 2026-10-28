# Fast Track C600 on Apple Silicon

[Italiano](README.it.md)

Open development project for the M-Audio Fast Track C600 (`0763:2031`) on
modern macOS. **Current status: the experimental Core Audio driver is installed
and recognized on one M3 Pro Mac running macOS 15.6.1. Audible playback through
analog outputs 1–2 has been confirmed with a 440 Hz test tone and a macOS
system sound. Microphone recording still needs physical verification.**

```sh
make
./build/c600-probe          # read-only USB descriptor and clock probe
./build/c600-capture-check  # 2-second capture transport check, no audio saved
./build/c600-capture-check --duplex  # also send silent output packets
```

The capture check temporarily selects the internal 48 kHz clock. The duplex
mode sends silence using capture packet counts for implicit feedback. It should
not run concurrently with another driver using the interface. Build requires
Xcode Command Line Tools or Xcode.

## Hardware findings

Without a custom driver, the C600 enumerates on USB but does not appear in
Core Audio because its audio streaming interfaces use vendor-specific class
`0xff`. The device
descriptors expose 8 output and 6 input channels of 24-bit PCM. Linux already
supports this USB ID, including the implicit feedback relationship between
capture endpoint `0x81` and playback endpoint `0x01`.

On the tested unit, the capture check received 15,984 packets and 1,726,812
bytes in two seconds, with zero packet or submission errors. This is about
1.999 seconds of 6-channel, 24-bit, 48 kHz audio. No audio content was saved.
The silent duplex check received 15,992 packets and sent 15,968 packets with
zero packet errors; the result repeated across three consecutive runs.

## Installation (experimental)

Requirements: a Mac with macOS 12 or newer, Xcode Command Line Tools or Xcode,
CMake, and the Fast Track C600 connected by USB. Only macOS 15.6.1 on M3 Pro
has been tested so far. The C600 can run from USB power, but its analog inputs
3–4, analog outputs 5–6, and second headphone output require the original
5 V / 2 A external supply.

1. Clone and build:

   ```sh
   git clone --recurse-submodules https://github.com/neurodropp/fast-track-c600-macos.git
   cd fast-track-c600-macos
   cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
   cmake --build build/cmake --parallel
   ```

2. Install or update. Run this **without** `sudo`; the script copies the signed
   bundle to a temporary location before asking for an administrator password.
   This avoids macOS privacy restrictions on the Documents folder. When
   updating, the installer keeps the previous bundle until the new copy has
   passed signature verification.

   ```sh
   ./scripts/install.sh
   ```

3. Open **Audio MIDI Setup** or **System Settings → Sound**. “Fast Track C600”
   should appear as a USB device with 6 inputs, 8 outputs, and 48 kHz. For a
   silent Core Audio I/O test:

   ```sh
   make build/c600-hal-io-check
   ./build/c600-hal-io-check
   ```

   To check audible playback, connect speakers to analog outputs 1–2, start
   with a low monitor level, and run `./build/c600-hal-io-check --tone`. It sends
   a quiet 440 Hz tone for two seconds. The command's successful completion
   confirms Core Audio callbacks, but only listening confirms analog output.

4. To remove the experimental driver:

   ```sh
   ./scripts/uninstall.sh
   ```

The install and uninstall scripts restart Core Audio, which may briefly
interrupt audio in other applications. The driver does not modify firmware or
macOS security settings.

## Current driver scope

The driver uses [libASPL](https://github.com/gavv/libASPL) (MIT, included as a
Git submodule) and supports one fixed 48 kHz mode: 6 input and 8 output
channels, 24-bit USB PCM converted to 32-bit float for Core Audio. The USB
engine streamed about 2.4 seconds of full duplex audio with zero errors in a
standalone test. The installed driver appears in Core Audio and completed 535
silent I/O cycles in a two-second test, with nonempty input and output buffers.
After fixing the mixer's zero-based routing and USB request recipient, the
440 Hz output test completed 534 callbacks without a Core Audio error and was
heard from speakers connected to line outputs 1–2. A macOS system sound sent
to the default output was also heard from those speakers.

```sh
./build/cmake/c600-engine-check   # hardware transport test, sends silence
./build/cmake/c600-plugin-check ./build/cmake/FastTrackC600.driver/Contents/MacOS/FastTrackC600
```

The driver currently lacks hot-plug handling, sample-rate switching, and
long-session timing qualification. Its FIFO audio bridge still needs physical
recording and long playback tests before recording quality or sustained
playback quality can be claimed. Use the uninstall script if Core Audio behaves
unexpectedly.

Next: validate physical input recording, then test long-session timing, rate
switching, and reconnect behavior. Firmware work
is deferred until there is evidence of a firmware defect and a recovery path.

## Prior work and license

As of 27 October 2026, we found no public Core Audio driver for the C600.
[`c600gui`](https://github.com/obijywk/c600gui) is a control panel and
[`maudio_fast_track_c400`](https://github.com/ruslanvakhitov/maudio_fast_track_c400)
is a driver for the related C400. Neither repository declares a reuse license,
so no code from either is included here. The original code in this repository
is licensed under [MIT](LICENSE).

Protocol references: [Linux C600 USB audio quirk](https://cos.googlesource.com/third_party/kernel/+/a65a6e1004cb6e445657dc2519022115fbde0c90/sound/usb/quirks-table.h),
[Linux implicit feedback](https://github.com/torvalds/linux/blob/master/sound/usb/implicit.c),
[Avid's legacy drivers](https://kb.avid.com/pkb/articles/en_US/Knowledge/Fast-Track-C600-Drivers).
