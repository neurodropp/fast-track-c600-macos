# Fast Track C600 on Apple Silicon

[Italiano](README.md)

Open development project for the M-Audio Fast Track C600 (`0763:2031`) on
modern macOS. **Current status: USB diagnostics and capture transport work on
one M3 Pro Mac running macOS 15.6.1. A Core Audio driver is still in development.**

```sh
make
./build/c600-probe          # read-only USB descriptor and clock probe
./build/c600-capture-check  # 2-second capture transport check, no audio saved
```

The capture check temporarily selects the internal 48 kHz clock. It should
not run concurrently with another driver using the interface. Build requires
Xcode Command Line Tools or Xcode.

## Hardware findings

The C600 enumerates on USB but does not appear in Core Audio because its
audio streaming interfaces use vendor-specific class `0xff`. The device
descriptors expose 8 output and 6 input channels of 24-bit PCM. Linux already
supports this USB ID, including the implicit feedback relationship between
capture endpoint `0x81` and playback endpoint `0x01`.

On the tested unit, the capture check received 15,984 packets and 1,726,812
bytes in two seconds, with zero packet or submission errors. This is about
1.999 seconds of 6-channel, 24-bit, 48 kHz audio. No audio content was saved.

Next: validate silent playback transport, implement the Core Audio driver,
then test actual audio, rate switching, and reconnect behavior. Firmware work
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
