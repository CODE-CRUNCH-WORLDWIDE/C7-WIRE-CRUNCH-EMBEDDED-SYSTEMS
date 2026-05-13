# Exercise 1 — Toolchain install

**Time estimate:** ~120 minutes (mostly compiling and downloading).

## Problem statement

Install the embedded toolchain you will use for 24 weeks of C7, and prove each component works. By the end, this command sequence runs without error on your machine:

```bash
arm-none-eabi-gcc --version
arm-none-eabi-gdb --version
openocd --version
probe-rs --version
picotool version
cmake --version
ninja --version
echo $PICO_SDK_PATH
ls $PICO_SDK_PATH/pico_sdk_init.cmake
```

## Acceptance criteria

- [ ] `arm-none-eabi-gcc --version` prints **≥ 13.2.Rel1**.
- [ ] `arm-none-eabi-gdb --version` (or `gdb-multiarch --version` on macOS) prints a version and exits 0.
- [ ] `openocd --version` prints **≥ 0.12.0** with RP2040 support.
- [ ] `probe-rs --version` prints **≥ 0.24**.
- [ ] `picotool version` prints **≥ 2.0.0**.
- [ ] `cmake --version` prints **≥ 3.13**.
- [ ] `$PICO_SDK_PATH` is set and points at a directory containing `pico_sdk_init.cmake`.
- [ ] You have a screenshot or terminal log of all of the above committed to `notes/toolchain-install.md` in your Week 1 repo.
- [ ] You have plugged the Pi Pico W into your laptop in BOOTSEL mode and confirmed `picotool info -a` enumerates the board.

## Hints

<details>
<summary>macOS (Apple Silicon and Intel) — Homebrew path</summary>

```bash
# 1) Xcode CLI tools (host C toolchain, git, make)
xcode-select --install

# 2) GCC ARM Embedded — use the official ARM tap, not the deprecated brew formula
brew install --cask gcc-arm-embedded
# verify
arm-none-eabi-gcc --version

# 3) cmake, ninja, openocd (Raspberry Pi fork has best RP2040 support)
brew install cmake ninja libusb pkg-config
brew install --HEAD raspberrypi/raspberrypi/openocd

# 4) probe-rs (requires Rust; install rustup if you do not have it)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
cargo install probe-rs-tools --locked

# 5) picotool
brew install picotool

# 6) Clone the SDK and set the env var
mkdir -p ~/code
cd ~/code
git clone --recurse-submodules -j8 https://github.com/raspberrypi/pico-sdk.git
echo 'export PICO_SDK_PATH=$HOME/code/pico-sdk' >> ~/.zshrc
source ~/.zshrc

# 7) GDB — Apple ships LLDB, not GDB. Two options:
#    a) Install gdb-multiarch via macports or build from source. Code-sign it. (Painful.)
#    b) Use lldb for stepping; use arm-none-eabi-gdb-py from the GCC ARM Embedded
#       package, which is bundled and pre-signed. We recommend (b).
which arm-none-eabi-gdb || \
  ls $(brew --prefix)/Caskroom/gcc-arm-embedded/*/arm-gnu-toolchain-*/bin/arm-none-eabi-gdb
```

</details>

<details>
<summary>Ubuntu 22.04+ — apt + cargo path</summary>

```bash
# 1) Host build tools, git, libusb (for OpenOCD and probe-rs)
sudo apt update
sudo apt install -y build-essential git cmake ninja-build pkg-config \
                    libusb-1.0-0-dev libudev-dev libftdi-dev libhidapi-dev

# 2) GCC ARM Embedded — the apt package is too old on 22.04 (10.x);
#    download 13.2.Rel1 directly from ARM
cd /tmp
wget https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz
sudo tar -xJf arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz -C /opt
echo 'export PATH=/opt/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
arm-none-eabi-gcc --version

# 3) OpenOCD — build the Raspberry Pi fork from source for RP2040
git clone https://github.com/raspberrypi/openocd.git --branch sdk-2.0.0 --depth=1
cd openocd
./bootstrap
./configure --enable-cmsis-dap
make -j$(nproc)
sudo make install
openocd --version

# 4) probe-rs
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
cargo install probe-rs-tools --locked

# 5) picotool — build from source against the SDK
git clone https://github.com/raspberrypi/picotool.git
cd picotool
mkdir build && cd build
cmake -DPICO_SDK_PATH=$PICO_SDK_PATH ..
make -j$(nproc)
sudo make install

# 6) udev rules so non-root users can talk to the Pico
sudo cp ~/probe-rs/udev/99-probe-rs.rules /etc/udev/rules.d/ 2>/dev/null || true
sudo udevadm control --reload-rules
sudo usermod -aG plugdev $USER
# log out and back in
```

</details>

<details>
<summary>Windows with WSL2 — `usbipd-win` path</summary>

```powershell
# 1) On Windows host: install usbipd
winget install --interactive --exact dorssel.usbipd-win

# 2) Inside WSL2 (Ubuntu): follow the Linux path above.

# 3) Plug the Pico in BOOTSEL mode. From an Admin PowerShell on the host:
usbipd list                                  # find the BUSID of the RP2 Boot device
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>

# 4) Inside WSL2:
lsusb       # you should see "Raspberry Pi RP2 Boot" in the listing
picotool info -a
```

USB-over-WSL is finicky. If `lsusb` does not show the Pico, the `usbipd attach` step did not stick — re-run it from an admin shell, and confirm `usbipd state` shows the device as "Attached."

</details>

<details>
<summary>If `cargo install probe-rs-tools` fails</summary>

The most common failures are missing libudev / libusb headers (Linux), or an old Rust toolchain. Update Rust (`rustup update`) and install the dev headers (`sudo apt install libusb-1.0-0-dev libudev-dev`). On macOS, `brew install libusb pkg-config` covers it.

If the build itself is slow (probe-rs depends on many crates), this is expected. ~5 minutes on a recent laptop.

</details>

<details>
<summary>If `openocd` does not see your Pico</summary>

You have three places to look:

1. **Cable.** Many micro-USB cables are power-only. Try a different cable.
2. **Probe firmware.** If using a second Pi Pico as a `debugprobe`, did you flash `debugprobe_on_pico.uf2` onto it first? Get the `.uf2` from <https://github.com/raspberrypi/debugprobe/releases>.
3. **Udev rules (Linux).** Without the udev rules in step 6 of the Ubuntu instructions, OpenOCD will see the probe only as root. Either run `sudo openocd …` (acceptable for a one-off test) or install the rules and re-login.

</details>

## Why this matters

You will rebuild this toolchain three or four times in the next 24 weeks — every time you switch laptops, every time someone joins your cohort, every time you set up CI. The first install is slow. The fifth is muscle memory. Do not skip the screenshot in the acceptance criteria; you will be glad you have it when a colleague asks "what versions are you running?"

The toolchain is also the layer most likely to silently regress. A `brew upgrade` that pulls a new `arm-none-eabi-gcc` can break a project that worked yesterday. The version-pinning discipline of Week 1 — write down the version numbers, commit them — is the discipline that keeps Week 24 builds reproducible.

## Submission

Commit `notes/toolchain-install.md` containing:

- The output of each `--version` command above.
- One screenshot of `picotool info -a` against your Pi Pico W in BOOTSEL mode.
- One sentence on which platform you are on and any hiccups you hit.

When you submit the Week 1 mini-project, link to this note from the bring-up document. Reviewers will check that the versions in your bring-up match the versions in this note.
