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
`virtio-gpu-pci` device. The guest created `/dev/dri/card0` and
`/dev/dri/renderD128`; the TCC-built probe successfully initialized SDL3's
KMSDRM backend and reported:

```text
Found 1 display(s)
display[0] id=1 name=Virtual-1 bounds=0,0 1280x800
```

The current minimal guest did not complete `SDL_CreateWindow` reliably: the
fullscreen path stalled and the guest reported a kernel soft-lockup. Therefore
this is a DRM/KMS enumeration smoke test, not yet a passing end-to-end window
test. A real bare-metal KMS test remains required before accepting display
ownership behavior.

The QEMU test also exposed an important fixture rule: the Alpine initramfs did
not include a usable network driver in the direct-shell boot path. Test
artifacts were therefore supplied through a second read-only ISO rather than
through guest networking.

Additional fixture checks did not change the result. Giving QEMU a VNC
scanout still allowed display enumeration but did not complete
`SDL_CreateWindow`; increasing guest memory from 1 GiB to 2 GiB also did not
help. QEMU standard VGA was not a substitute because this Alpine kernel did
not expose a DRM node for that device. The remaining VM-specific suspect is
the TCG plus virtio-GPU plus software-Mesa presentation path, not TCC linking
or SDL display enumeration.

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

## Failure interpretation

- `kmsdrm not available`: SDL3 was built without KMSDRM, or its dynamic
  libdrm/GBM/EGL dependencies cannot be loaded.
- `No displays found`: the DRM node opened, but no connector/mode was
  available.
- permission or DRM-master errors: the display manager, another compositor,
  or device permissions still own the primary node.
- window creation failure after enumeration: the mode, EGL/GLES, GBM backend,
  or selected renderer needs investigation.
