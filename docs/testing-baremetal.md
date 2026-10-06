# Bare-metal KMSDRM testing

## Meaning of bare metal here

For blit, “bare metal” means a normal Linux kernel and userspace running
directly on the machine, with blit owning the display through DRM/KMS. It
does not mean that blit replaces the Linux kernel or boots without an
operating system.

The test host must expose:

- a Linux DRM/KMS-capable GPU;
- `/dev/dri/cardN` for display control;
- a connected display and a valid mode;
- permissions for the test process to become DRM master;
- the workspace-local SDL3, libdrm, GBM, EGL, GLES2, and DRI libraries.

## Safe first test

Use a spare TTY or a machine where losing the graphical session is acceptable.
Do not run this first test inside an active X11 or Wayland session: another
process may already own DRM master.

1. Build the dependencies and probe on the development machine.
2. Copy the source/build artifacts to the target host, or build there.
3. Switch to a virtual console with `Ctrl+Alt+F3`.
4. Stop the display manager if it is running, for example:

   ```sh
   sudo systemctl stop display-manager
   ```

5. Confirm that a primary DRM node exists:

   ```sh
   ls -l /dev/dri/card* /dev/dri/render* 2>/dev/null
   ```

6. Run the probe as root for the first hardware test:

   ```sh
   cd /path/to/hw-manager
   export SDL_VIDEODRIVER=kmsdrm
   export SDL_KMSDRM_REQUIRE_DRM_MASTER=1
   export SDL_KMSDRM_DEVICE_INDEX=0
   export LD_LIBRARY_PATH="$PWD/build/sdl3-kms:$PWD/build/deps/prefix/lib/x86_64-linux-gnu"
   ./build/probes/kmsdrm_display
   ```

The probe should enumerate displays, create a fullscreen KMSDRM window, and
return after five seconds. Press Escape to exit early. The display may blank or
change mode briefly while DRM master is acquired and released.

## Build the probe with TCC

```sh
tcc -std=c99 \
  -Ivendor/SDL3/include \
  tools/probes/kmsdrm_display.c \
  -Lbuild/sdl3-kms -lSDL3 \
  -o build/probes/kmsdrm_display
```

The source program is TCC-built; SDL3 and its graphics dependencies are
external C libraries.

## If the root test succeeds

Replace root access with a dedicated service/user policy. The exact device
permissions depend on the host distribution and seat/session manager. Verify
that the non-root process can open the primary `/dev/dri/cardN` node and become
DRM master, then repeat the probe from a clean TTY.

## QEMU smoke test status

QEMU can validate the guest-side DRM/KMS path without a physical monitor. The
test used Alpine Linux 3.24.0, QEMU TCG (no `/dev/kvm` was available), and a
`virtio-gpu-pci,max_outputs=2` device. The test kernel had
`CONFIG_DRM_VIRTIO_GPU=y`; the guest created a VKMS card and a virtio DRM card
(`/dev/dri/card0` and `/dev/dri/card1`) plus `/dev/dri/renderD128`. The TCC-built
probe selected the virtio card and successfully created an SDL3 KMSDRM window:

```text
Found 1 display(s)
display[0] id=1 name=Virtual-2 bounds=0,0 1280x800
KMSDRM window created; press Escape or wait five seconds
VIRTIO_PROBE_STATUS=0
```

This is a passing guest-side end-to-end SDL3/KMSDRM window test. The initramfs
must stage Mesa's `kms_swrast_dri.so`, its dependencies, and the GLVND vendor
file, then set `LIBGL_DRIVERS_PATH`, `__EGL_VENDOR_LIBRARY_DIRS`, and
`SDL_KMSDRM_DEVICE_INDEX=1`. Without those inputs, enumeration can succeed
while EGL window creation fails. A real bare-metal KMS test remains required
before accepting physical display ownership and hardware timing behavior.

With the same virtio guest and the current TCC-built `blit-server`, the full
runtime path also passes: `/v1/server` reports `backend: "sdl3"` and ready,
`/v1/graphics` reports the virtio PCI card, `/v1/diagnostics/runtime` reports
the 74.99 Hz output and refresh-aware scheduler, and a PTY-backed Hibr
workload reaches `running` with its surface revision advancing and desktop
text present. The guest must mount `devtmpfs`, create `/dev/pts` after that
mount, mount `devpts` with a usable `/dev/ptmx`, and bring up loopback for the
API readiness check.

The QEMU test also exposed an important fixture rule: the Alpine initramfs did
not include a usable network driver in the direct-shell boot path. Test
artifacts were therefore supplied through a second read-only ISO rather than
through guest networking.

`max_outputs=2` gives the virtio device two scanout slots, but QEMU exposes
only one active connector in this fixture. It therefore proves the SDL3
KMSDRM presentation path, not Blit's two-connector topology. The deterministic
two-output VKMS fixture below remains authoritative for multi-display layout,
pixel, page-flip, pane, and input testing.

## Reproducible multi-display QEMU fixture

The reliable fixture uses Alpine userspace with a small Linux test kernel that
has VKMS built in. QEMU's virtio-GPU `max_outputs` setting exposes capacity but
does not create multiple active connectors; separate virtio GPUs create
separate DRM cards and are not one multi-output device. VKMS configfs creates
two connectors on one DRM card, which matches the topology blit must test.

The test kernel must include at least:

```text
CONFIG_DRM=y
CONFIG_DRM_KMS_HELPER=y
CONFIG_DRM_VKMS=y
CONFIG_CONFIGFS_FS=y
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y
CONFIG_BLK_DEV_INITRD=y
CONFIG_RD_GZIP=y
CONFIG_FUTEX=y
```

Boot it with the Alpine initramfs and disable VKMS's default device:

```sh
qemu-system-x86_64 -M q35 -m 1024 -smp 2 \
  -kernel arch/x86/boot/bzImage \
  -initrd alpine-vkms-initramfs.gz \
  -append 'console=ttyS0 init=/bin/sh vkms.create_default_dev=0' \
  -display none -serial stdio -monitor none -no-reboot
```

Inside the guest, mount configfs and create two
plane/CRTC/encoder/connector pipelines. The following uses the initramfs's
explicit BusyBox path because the direct shell has no useful `PATH`:

```sh
/usr/bin/busybox mkdir -p /sys/kernel/config
/usr/bin/busybox mount -t configfs none /sys/kernel/config
/usr/bin/busybox mkdir -p \
  /sys/kernel/config/vkms/blit/planes/plane0 \
  /sys/kernel/config/vkms/blit/planes/plane1 \
  /sys/kernel/config/vkms/blit/crtcs/crtc0 \
  /sys/kernel/config/vkms/blit/crtcs/crtc1 \
  /sys/kernel/config/vkms/blit/encoders/encoder0 \
  /sys/kernel/config/vkms/blit/encoders/encoder1 \
  /sys/kernel/config/vkms/blit/connectors/connector0 \
  /sys/kernel/config/vkms/blit/connectors/connector1
echo 1 > /sys/kernel/config/vkms/blit/planes/plane0/type
echo 1 > /sys/kernel/config/vkms/blit/planes/plane1/type
echo 1 > /sys/kernel/config/vkms/blit/connectors/connector0/status
echo 1 > /sys/kernel/config/vkms/blit/connectors/connector1/status
/usr/bin/busybox ln -s /sys/kernel/config/vkms/blit/crtcs/crtc0 \
  /sys/kernel/config/vkms/blit/planes/plane0/possible_crtcs
/usr/bin/busybox ln -s /sys/kernel/config/vkms/blit/crtcs/crtc1 \
  /sys/kernel/config/vkms/blit/planes/plane1/possible_crtcs
/usr/bin/busybox ln -s /sys/kernel/config/vkms/blit/crtcs/crtc0 \
  /sys/kernel/config/vkms/blit/encoders/encoder0/possible_crtcs
/usr/bin/busybox ln -s /sys/kernel/config/vkms/blit/crtcs/crtc1 \
  /sys/kernel/config/vkms/blit/encoders/encoder1/possible_crtcs
/usr/bin/busybox ln -s /sys/kernel/config/vkms/blit/encoders/encoder0 \
  /sys/kernel/config/vkms/blit/connectors/connector0/possible_encoders
/usr/bin/busybox ln -s /sys/kernel/config/vkms/blit/encoders/encoder1 \
  /sys/kernel/config/vkms/blit/connectors/connector1/possible_encoders
echo 1 > /sys/kernel/config/vkms/blit/enabled
/usr/bin/busybox mount -t devtmpfs devtmpfs /dev
GBM_BACKENDS_PATH=/blit-stage/gbm \
  /blit-stage/ld-linux-x86-64.so.2 --library-path /blit-stage \
  /blit-stage/kmsdrm_display
```

With the current fixture the probe reports two outputs at `0,0 1024x768` and
`1024,0 1024x768`, creates the KMSDRM window, and exits successfully. The VKMS
teardown may log `Could not restore CRTC`; it does not indicate a failed probe
when the process exit status is zero.

Set `BLIT_KMSDRM_ENUM_ONLY=1` to stop after output enumeration. This is a
diagnostic mode of the same probe, not a second display implementation.

## Gateway integration in QEMU

The original tiny VKMS kernel is intentionally network-free. To test the
actual TCP gateway in the fixture, rebuild it with built-in `NET`, `INET`,
`PACKET`, `UNIX`, `E1000`, and `IP_PNP` support, then add QEMU user networking:

```sh
qemu-system-x86_64 ... \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:18091-:18091 \
  -device e1000,netdev=n0
```

Inside the guest, mount `proc`, `sysfs`, `devtmpfs`, and `devpts`, configure
`eth0` as `10.0.2.15/24`, and create the two-output VKMS topology above. Run
the staged server with the test-only network bind and GBM path:

```sh
BLIT_BIND_ADDRESS=0.0.0.0 \
GBM_BACKENDS_PATH=/blit-stage/gbm \
/blit-stage/ld-linux-x86-64.so.2 --library-path /blit-stage \
/blit-stage/blit-server 18091
```

The host can then exercise `http://127.0.0.1:18091`. The validated sequence
returns both displays, creates a PTY session, applies a cell update, accepts a
frame, and upgrades the WebSocket stream.

## Failure interpretation

- `kmsdrm not available`: SDL3 was built without KMSDRM, or its dynamic
  libdrm/GBM/EGL dependencies cannot be loaded.
- `No displays found`: the DRM node opened, but no connector/mode was
  available.
- permission or DRM-master errors: the display manager, another compositor,
  or device permissions still own the primary node.
- window creation failure after enumeration: the mode, EGL/GLES, GBM backend,
  or selected renderer needs investigation.
