<!-- SPDX-License-Identifier: GPL-2.0-only -->
# FT3518 touch and framebuffer console

The existing community board DTS describes an FT3518 at I2C address 0x38,
interrupt GPIO88, reset GPIO87 and a fixed 3.3 V supply enabled by GPIO83.
The ACK edt-ft5x06 driver already matches focaltech,ft3518. This change enables
that existing support rather than importing another driver. Imported authorship
and board wiring are retained. Touch, GENI I2C, GENI SE, GPI DMA and the fixed
regulator driver are built in, including for recovery without vendor modules.

The prior profile disabled the touch driver, I2C2, its QUP wrapper, DMA0 and
the supply. All are enabled together. Other I2C engines and DMA1 stay disabled.

The kernel framebuffer console is now disabled. SimpleDRM and its fbdev
interface remain enabled; removing fbcon must not unregister the DRM device
or remove recovery's framebuffer. This removes early text/penguins as well as
future console writes. Recovery still uses its graphics backend. These new
settings have not yet been built or validated on the device.

An earlier normal Android capture showed fbcon already detached despite the
physical screen retaining console contents. Therefore disabling fbcon removes
one competing writer but cannot establish that the graphics HAL presents
correctly. Unsupported DRM vblank waits and slow software rendering are separate
issues. Do not claim a usable physical GUI until the next device check.

## Maintainer validation after a matching rebuild

First verify recovery still draws its UI and lists a touchscreen input device.
Then check normal Android:

```sh
adb shell cat /proc/bus/input/devices
adb shell getevent -lp
adb shell cat /proc/fb
adb shell 'for v in /sys/class/vtconsole/vtcon*; do cat "$v/name" "$v/bind"; done'
adb shell getprop sys.boot_completed
adb shell am start -a android.settings.SETTINGS
```

Use getevent on the touchscreen's actual event node to check contacts and
coordinates. Do not assume its event number. Expect no bound "frame buffer"
console; /proc/fb should still list simpledrmdrmfb. The framebuffer console is
distinct from that graphics interface.

If touch is absent, collect dmesg for edt-ft5x06, I2C/GENI, GPI, regulator and
deferred-probe errors. If input works but Android remains blank or stale, collect
logcat for drmfb, SurfaceFlinger and buffer import/modeset/page-flip failures,
plus dumpsys SurfaceFlinger. Diagnose presentation rather than clearing fb0
while Android is drawing or unbinding SimpleDRM.

## 2026-10-05 live diagnosis and pending composer correction

The maintainer confirmed touch in recovery. Normal Android ADB subsequently
reported boot completion and an FT3518 input device (generic ft5x06 (48),
INPUT_PROP_DIRECT). No framebuffer console was registered, but the physical
screen retained the splash. DRM had no active CRTC and no Android framebuffer.

Temporary userspace probes confirmed present() received a selected CRTC and
720 x 1560 ABGR8888 minigbm buffers: numFds=2, num_planes=1, valid magic.
The composer cached framebuffer ID zero. On a fresh graphics-service instance,
both PRIME imports succeeded, then drmModeAddFB2 returned -22 (EINVAL).
The importer used numFds as the image-plane count, including the reserved-region
metadata FD. This was initially identified as the rejection cause, but that
conclusion was premature: DRM ignores unused handles when FB_MODIFIERS is absent.
The subsequent updated-build diagnosis below establishes the actual rejection.

The composer now uses num_planes, validates the FD/plane bounds and zeros all
unused handle/pitch/offset slots. This fix is in hardware/mainline/common and
is retained here as rom-patches/hardware-mainline-common/
0001-drmfb-import-image-planes-and-pace-simpledrm.patch. Apply it from that
repository on a fresh checkout; do not apply it twice. The same patch switches
permanently unsupported vblank waits to the existing software pacing fallback
and logs the transition once, keeping framebuffer errors visible.

This userspace correction is not built or live validated. Rebuild the ROM's
composer HAL/vendor image using the same sources. A kernel-only rebuild will
not replace the installed composer. After installation, expect a nonzero Android
framebuffer on an active CRTC and verify that the physical screen updates.
Software rendering performance and native GPU/display support remain separate.
Temporary trace probes were removed, strace detached, and logical display-size
overrides were reset after diagnosis.

## Updated build: XBGR8888 rejection

The installed composer SHA-256 was
3a0a1f01118eac410b3d212f3f55567309809da2efdb30638a2d670f92027b6c,
matching the rebuilt local output with the plane-count correction. Android
reported boot completion. Temporary DRM debug output captured:

```text
[124.880873] DRM_IOCTL_MODE_ADDFB2
[124.880891] drm_gem_fb_init_with_funcs: Unsupported pixel format XB24 little-endian (0x34324258) / modifier 0x0
[124.880908] drm_internal_framebuffer_create: could not create framebuffer
```

The fbdev framebuffer reports XRGB8888 (XR24); the later raw-DT inspection
confirmed native scanout is ARGB8888 (see below). ACK's sysfb format list adds
only XRGB8888 as an emulated input. Android RGBA buffers are ABGR8888; the
composer correctly discards alpha by importing them as XBGR8888 (XB24), but
the primary plane does not advertise that format. This causes EINVAL during
framebuffer creation, before a modeset.

The initial kernel correction advertised XBGR8888 only for XRGB8888 native
SimpleDRM scanout, missing this board's ARGB8888 variant. The correction below
covers both. drm_fb_blit gains the reverse red/blue swap, using
the existing symmetric packed-32-bit conversion with pitch and damage clipping.
Other native formats are unchanged. Relabeling an XBGR buffer as XRGB without
conversion would produce incorrect colours and is not the solution.

This kernel correction is source-only and not built or device validated.
Rebuild matching kernel/boot artifacts (or the complete ROM), retaining the
updated composer. Validate recovery, then normal Android, nonzero framebuffer
ID/active CRTC, screen updates, red/blue colours and touch. DRM debug verbosity
was restored to its original zero value after capture.

## Actual bootloader format: ARGB8888

The next installed kernel reported 6.18.32-g731bfb81b1f3-dirty, build #6,
2026-10-05 19:36:13 IST, matching the newly built artifacts. Android booted
but still had no active scanout. Reading the live raw DT, rather than inferring
from fbdev's XR24 format, identified the missed condition:

```text
/proc/device-tree/chosen/framebuffer@5c000000/format: a8r8g8b8
```

This is native ARGB8888. The sysfb helper removes alpha from advertised input
formats, explaining why the earlier fbdev inspection showed XRGB8888. The
previous exact-XRGB native-format guard did not advertise XBGR on this board.

The source now advertises XBGR8888 for both XRGB8888 and ARGB8888 native
scanout. The ARGB blit swaps red/blue and sets alpha to 0xff using the existing
symmetric packed-pixel helper; the XRGB blit preserves filler bits. Other
native formats remain unchanged. The updated paths still respect pitches and
damage clipping. This additional correction needs a kernel/boot rebuild and
maintainer validation; it has not been built or installed by the agent.

## Successful physical transition, 2026-10-05

The maintainer installed the rebuilt ROM and reports that recovery sideload
completed and the physical screen transitioned from splash to Android, after
a delay. Live ADB reports kernel 6.18.32-g731bfb81b1f3-dirty build #7
(2026-10-05 19:50:33 IST), slot B, and sys.boot_completed=1. SurfaceFlinger
reports an active 720x1560 mode at a nominal 60 Hz; this does not establish
actual achieved frame rate. Current logs still identify ANGLE/SwiftShader
software rendering. The physical transition is maintainer-verified; colours,
frame pacing, native GPU acceleration and performance remain separate checks.

Logs are saved under out/recovery-sideload-debug/completed-install-20261005
and completed-install-20261005-live.log in the Lineage workspace. The two
persistent boot directories are older boots; use the live capture for the
current boot (cdfe436e-b33e-4540-8c44-f099df28e7d6). Recovery installation
timings were not retained, so these captures cannot quantify sideload speedup.
