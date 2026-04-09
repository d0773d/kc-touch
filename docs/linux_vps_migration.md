# Linux VPS Build Migration (ESP32-P4 / YamUI)

This runbook reproduces the validated local build flow on a Linux VPS using ESP-IDF `v5.5.1`.

## 1) Copy or clone the repository

```bash
git clone <your-repo-url> YamUI
cd YamUI
```

## 2) Run the Linux build helper

From the repo root:

```bash
chmod +x tools/linux_vps_build.sh
./tools/linux_vps_build.sh
```

Optional flags:

```bash
./tools/linux_vps_build.sh --project-dir "$(pwd)" --idf-dir "$HOME/esp/esp-idf-v5.5.1"
./tools/linux_vps_build.sh --skip-system-deps
```

## 3) Verify artifacts

Expected files after a successful build:

- `build/YamUI.bin`
- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`

Quick check:

```bash
ls -lh build/YamUI.bin build/bootloader/bootloader.bin build/partition_table/partition-table.bin
```

## 4) Common follow-up commands

```bash
# Rebuild incrementally
source "$HOME/esp/esp-idf-v5.5.1/export.sh"
idf.py build

# Build from clean state
idf.py fullclean build
```

## Notes

- The script pins ESP-IDF to `v5.5.1` to match current project lock/build expectations.
- It sets target to `esp32p4` before `fullclean build`.
- If your VPS distro is not apt-based, install equivalent system packages manually and rerun with `--skip-system-deps`.
