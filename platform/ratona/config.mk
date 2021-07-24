#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2020 Western Digital Corporation or its affiliates.
#
# Authors:
#   Anup Patel <anup.patel@wdc.com>
#

# Compiler flags
platform-cppflags-y =
platform-cflags-y =
platform-asflags-y =
platform-ldflags-y =

# Command for platform specific "make run"
platform-runcmd = qemu-system-riscv$(PLATFORM_RISCV_XLEN) -M virt -m 256M \
  -nographic -bios $(build_dir)/platform/ratona/firmware/fw_payload.elf

# Blobs to build
FW_TEXT_START=0x81F00000
FW_DYNAMIC=y
FW_JUMP=y
FW_JUMP_ADDR=0x80000000
FW_JUMP_FDT_ADDR=0x81EF0000
FW_PAYLOAD=y
# Yes, it wraps around. It would be neat if the .ld defines this offset absolute. 
# The true value is just 0x80000000
FW_PAYLOAD_OFFSET=0xFE100000
FW_PAYLOAD_FDT_ADDR=0x81EF0000
