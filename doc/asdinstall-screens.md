# asdinstall TUI Installer — Screen Designs

## Overview

`asdinstall` is a TUI installer inspired by FreeBSD's `bsdinstall`. It uses
box-drawing pseudographics for all chrome. Navigation: arrow keys move focus,
Tab cycles panels, Enter confirms, Esc cancels/goes back.

## v1 Install Contract (local media)

For the current boot-from-disk flow, `asdinstall` operates on a local install
media directory and a target "installed disk" directory:

- Source media: `build/run` (prepared by `make run` / `make install`)
- Target disk: `build/disk` (used by `make bfd`)
- Layout mode:
  - Recommended: supported
  - Manual: shown but falls back to Recommended in v1
- Copied artifacts:
  - `EFI/BOOT/BOOTX64.EFI`
  - `boot/asdkernel.bin`
  - `boot/asdboot.conf`
  - top-level compatibility copies when present (`asdkernel.bin`, `asdboot.conf`)
- Component choices are written to `root/installed-components.txt` on target.

## Screen 1: Welcome / Language

```
┌──────────────────────────────────────────────────────────┐
│                                                          │
│          █████╗  ██████╗  █████╗  ██████╗                 │
│         ██╔══██╗██╔════╝ ██╔══██╗██╔═══██╗                │
│         ███████║██║      ███████║██║   ██║                │
│         ██╔══██║██║      ██╔══██║██║   ██║                │
│         ██║  ██║╚██████╗ ██║  ██║╚██████╔╝                │
│         ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝ ╚═════╝                 │
│                                                          │
│                    ASD Operating System                   │
│                     Installer v0.1                        │
│                                                          │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  Select language:                                        │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ > English                                          │  │
│  │   Deutsch                                          │  │
│  │   Français                                         │  │
│  │   日本語                                            │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  [ Enter: Next ]  [ Esc: Exit ]                          │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

## Screen 2: Disk Selection and Partition Layout

```
┌──────────────────────────────────────────────────────────┐
│ Disk Selection                              [ 2/10 ]     │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  Available disks:                   Partition layout:    │
│  ┌───────────────────────────────┐  ┌─────────────────┐  │
│  │ > ada0  Intel SSD 512GB       │  │ /boot  512MB    │  │
│  │   ada1  Samsung HDD 1TB       │  │ /      rest     │  │
│  │   nvme0  WD NVMe 1TB          │  │                 │  │
│  │                               │  │                 │  │
│  │   Size: 512GB                 │  │ [ Auto ]        │  │
│  │   Sectors: 1000215216         │  │ [ Custom ]      │  │
│  └───────────────────────────────┘  └─────────────────┘  │
│                                                          │
│  Warning: All data on selected disk will be erased!      │
│                                                          │
│  [ Enter: Next ]  [ Esc: Back ]                          │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

## Screen 3: Filesystem Options

```
┌──────────────────────────────────────────────────────────┐
│ Filesystem Options                          [ 3/10 ]     │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  Root filesystem type:                                   │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ > ASDF  (ASD native filesystem)                    │  │
│  │             append-only, extent-based, no journal  │  │
│  │   ZFS       (if available)                         │  │
│  │   UFS2      (BSD compatibility)                    │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  Additional options:                                     │
│  ┌────────────────────────────────────────────────────┐  │
│  │ [ ] Enable compression                             │  │
│  │ [x] Create swap partition (4GB)                    │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  [ Enter: Next ]  [ Esc: Back ]                          │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

## Screen 4: Timezone

```
┌──────────────────────────────────────────────────────────┐
│ Timezone Selection                          [ 4/10 ]     │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  Select region:                  Select timezone:        │
│  ┌───────────────────────────┐  ┌─────────────────────┐  │
│  │ > Americas                │  │ > America/New_York  │  │
│  │   Europe                  │  │   America/Chicago   │  │
│  │   Asia                    │  │   America/Denver    │  │
│  │   Africa                  │  │   America/Los_Angeles│ │
│  │   Australia               │  │                     │  │
│  │   Pacific                 │  │   Current time:     │  │
│  └───────────────────────────┘  │   14:32:07 EDT      │  │
│                                 └─────────────────────┘  │
│  UTC offset: -04:00                                      │
│                                                          │
│  [ Enter: Next ]  [ Esc: Back ]                          │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

## Screen 5: Hostname and Network

```
┌──────────────────────────────────────────────────────────┐
│ Hostname and Network                      [ 5/10 ]     │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  Hostname: ┌──────────────────────────────────────────┐  │
│            │ asd-box                                  │  │
│            └──────────────────────────────────────────┘  │
│                                                          │
│  Network interfaces:                                     │
│  ┌────────────────────────────────────────────────────┐  │
│  │ Interface   Address         Status                 │  │
│  │ ─────────────────────────────────────────────────  │  │
│  │ > em0       192.168.1.100   Connected              │  │
│  │   lo0       127.0.0.1       Loopback               │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ [x] Configure IPv4 via DHCP                        │  │
│  │ [ ] Configure IPv6 via SLAAC                       │  │
│  │ [ ] Set static IP                                  │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  [ Enter: Next ]  [ Esc: Back ]                          │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

## Screen 6: Root Password and User Creation

```
┌──────────────────────────────────────────────────────────┐
│ Root Password and User Creation             [ 6/10 ]     │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  Root password:                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ Password:      ********                            │  │
│  │ Confirm:       ********                            │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  Create a regular user account:                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ [x] Create user account                            │  │
│  │                                                    │  │
│  │   Username:  ┌─────────────────────────────────┐   │  │
│  │              │ icevein                         │   │  │
│  │              └─────────────────────────────────┘   │  │
│  │   Full name: ┌─────────────────────────────────┐   │  │
│  │              │ ASD User                        │   │  │
│  │              └─────────────────────────────────┘   │  │
│  │   Password:  ┌─────────────────────────────────┐   │  │
│  │              │ ********                        │   │  │
│  │   Confirm:   ┌─────────────────────────────────┐   │  │
│  │              │ ********                        │   │  │
│  │              └─────────────────────────────────┘   │  │
│  │   [x] Add to wheel group                           │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  [ Enter: Next ]  [ Esc: Back ]                          │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

## Screen 7: Package / Component Selection

```
┌──────────────────────────────────────────────────────────┐
│ Component Selection                         [ 7/10 ]     │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  Select components to install:                           │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ [x] Base system                  (core ASD userland)│  │
│  │ [x] asdsh                        (ASD shell)        │  │
│  │ [x] dctl                         (driver manager)   │  │
│  │ [x] actl                         (service manager)  │  │
│  │ [ ] Development tools            (compilers, etc.)  │  │
│  │ [ ] Network utilities            (netstat, etc.)    │  │
│  │ [ ] Extra drivers                (e1000, ahci, etc.)│  │
│  │ [ ] Documentation                (man pages, etc.)  │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  Estimated space: 256MB                                  │
│                                                          │
│  [ Enter: Next ]  [ Esc: Back ]                          │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

## Screen 8: Review and Confirm

```
┌──────────────────────────────────────────────────────────┐
│ Review and Confirm                          [ 8/10 ]     │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  The following actions will be performed:                 │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ Disk:       ada0 (Intel SSD 512GB)                 │  │
│  │ Partitions: /boot (512MB), / (rest), swap (4GB)    │  │
│  │ Filesystem: ASDF                                   │  │
│  │ Timezone:   America/New_York (UTC-04:00)           │  │
│  │ Hostname:   asd-box                                │  │
│  │ Network:    DHCP on em0                            │  │
│  │ Root pass:  **** (set)                             │  │
│  │ User:       icevein (wheel)                        │  │
│  │ Components: Base system, asdsh, dctl, actl         │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  !! This will destroy all data on the selected disk !!   │
│                                                          │
│  [ Enter: Install ]  [ Esc: Back ]                       │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

## Screen 9: Installation Progress

```
┌──────────────────────────────────────────────────────────┐
│ Installation Progress                       [ 9/10 ]     │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  Partitioning disk...                      [Done]       │
│  Creating filesystem...                  [Done]         │
│  Mounting filesystems...                 [Done]         │
│  Installing base system...               [####      ]   │
│  Installing asdsh...                     [          ]   │
│  Installing dctl...                      [          ]   │
│  Installing actl...                      [          ]   │
│  Configuring network...                  [          ]   │
│  Setting up timezone...                  [          ]   │
│  Creating user accounts...               [          ]   │
│  Installing bootloader...                [          ]   │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ Installing base system...                          │  │
│  │ Copying /sbin/asdinit           42K of 128K        │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

## Screen 10: Reboot

```
┌──────────────────────────────────────────────────────────┐
│                                                          │
│                                                          │
│              Installation Complete!                      │
│                                                          │
│          ASD has been installed successfully.            │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │                                                    │  │
│  │  Disk:       ada0                                  │  │
│  │  Hostname:   asd-box                               │  │
│  │  User:       icevein                               │  │
│  │  Components: 4 installed                           │  │
│  │                                                    │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│                                                          │
│  [ Enter: Reboot ]  [ Esc: Shell ]                       │
│                                                          │
│                                                          │
└──────────────────────────────────────────────────────────┘
```
