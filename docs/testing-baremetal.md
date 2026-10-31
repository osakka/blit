# Bare-metal KMS testing

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
- the libdrm, GBM, EGL, GLES2, and DRI libraries used by the KMS compositor.

## Safe first test

Use a spare TTY or a machine where losing the graphical session is acceptable.
Do not run this first test inside an active X11 or Wayland session: another
process may already own DRM master.

1. Build the server and the read-only DRM probe on the development machine.
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

6. Run the read-only probe, then the server, as root for the first hardware
   test:

   ```sh
   cd /path/to/hw-manager
   ./build/blit/drm-kms-probe
   ./build/blit/blit-server 8080
   ```

The probe enumerates cards, connectors, and modes without changing display
state. The server should then report the connected outputs on `/v1/displays`,
or fail explicitly with a DRM diagnostic in `/v1/diagnostics`. Set
`BLIT_DRM_DEVICE` to choose a node other than `/dev/dri/card0`. The display
may blank or change mode briefly while DRM master is acquired and released.

## Build the probe with TCC

```sh
tcc -std=c99 \
  -Ibuild/deps/prefix/include \
  -Ibuild/deps/prefix/include/libdrm \
  tools/probes/drm_kms.c \
  -Lbuild/deps/prefix/lib/x86_64-linux-gnu -ldrm \
  -o build/blit/drm-kms-probe
```

The source program is TCC-built; libdrm is an external C library. See
[`testing-kms.md`](testing-kms.md) for the production KMS boundary.

## If the root test succeeds

Replace root access with a dedicated service/user policy. The exact device
permissions depend on the host distribution and seat/session manager. Verify
that the non-root process can open the primary `/dev/dri/cardN` node and become
DRM master, then repeat the test from a clean TTY.

## QEMU smoke test status

QEMU can validate the guest-side DRM/KMS path without a physical monitor. The
test used Alpine Linux 3.24.0, QEMU TCG (no `/dev/kvm` was available), and a
`virtio-gpu-pci,max_outputs=2` device. The test kernel had
`CONFIG_DRM_VIRTIO_GPU=y`; the guest created a VKMS card and a virtio DRM card
(`/dev/dri/card0` and `/dev/dri/card1`) plus `/dev/dri/renderD128`. Point the
server at the virtio card with `BLIT_DRM_DEVICE=/dev/dri/card1`. For GLES
composition (the default) the initramfs must stage Mesa's `kms_swrast_dri.so`,
its dependencies, and the GLVND vendor file, then set `LIBGL_DRIVERS_PATH` and
`__EGL_VENDOR_LIBRARY_DIRS`; without them the CPU compositor is used. A real
bare-metal KMS test remains required before accepting physical display
ownership and hardware timing behavior.

In this guest, `/v1/graphics` should report the virtio PCI card and
`/v1/diagnostics/runtime` the output's refresh and the refresh-aware
scheduler. The guest must mount `devtmpfs`, create `/dev/pts` after that
mount, mount `devpts` with a usable `/dev/ptmx`, and bring up loopback for the
API readiness check.

The QEMU test also exposed an important fixture rule: the Alpine initramfs did
not include a usable network driver in the direct-shell boot path. Test
artifacts were therefore supplied through a second read-only ISO rather than
through guest networking.

`max_outputs=2` gives the virtio device two scanout slots, but QEMU exposes
only one active connector in this fixture. It therefore proves single-output
presentation, not Blit's two-connector topology. The deterministic
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
  /blit-stage/blit-server 18091
```

The server should report two outputs at `0,0 1024x768` and `1024,0 1024x768`
on `/v1/displays`. Stage `drm-kms-probe` alongside it to list the connectors
without taking DRM master.

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
the staged server with a token, the network bind and the GBM path; without
`BLIT_AUTH_TOKEN` Blit refuses to listen on `0.0.0.0`:

```sh
BLIT_AUTH_TOKEN=change-me \
BLIT_BIND_ADDRESS=0.0.0.0 \
GBM_BACKENDS_PATH=/blit-stage/gbm \
/blit-stage/ld-linux-x86-64.so.2 --library-path /blit-stage \
/blit-stage/blit-server 18091
```

The host can then exercise `http://127.0.0.1:18091`, sending
`Authorization: Bearer change-me`. The validated sequence
returns both displays, creates a PTY session, applies a cell update, accepts a
frame, and upgrades the WebSocket stream.

## Failure interpretation

- the DRM node cannot be opened: the card does not exist, or
  `BLIT_DRM_DEVICE` names the wrong node.
- no outputs: the DRM node opened, but no connector/mode was available.
- permission or DRM-master errors: the display manager, another compositor,
  or device permissions still own the primary node.
- `GLES compositor not started` or `GLES compositor unavailable, using CPU`
  in the log: the EGL/GLES, GBM backend, or DRI driver needs
  investigation; Blit composes with the CPU compositor in the meantime.
