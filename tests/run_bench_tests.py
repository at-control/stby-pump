"""Compile actual main.c logic for ARM and run with simulated hardware.

Requires Python packages unicorn and pyelftools; no physical device is accessed.
Set STM32_TOOLCHAIN_PATH if the compiler is outside the default ST bundle.
"""
import os
import argparse
from pathlib import Path
import subprocess

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0

parser = argparse.ArgumentParser()
group = parser.add_mutually_exclusive_group()
group.add_argument('--pin-test', action='store_true')
group.add_argument('--display-status', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source_name = "pin_test" if args.pin_test else "display_status_test" if args.display_status else "bench_test"
toolchain = Path(os.environ.get("STM32_TOOLCHAIN_PATH", str(
    Path(os.environ["LOCALAPPDATA"]) / "stm32cube/bundles/gnu-tools-for-stm32/14.3.1+st.2/bin")))
output = root / f"build/{source_name}.elf"
output.parent.mkdir(exist_ok=True)
includes = ["Core/Inc", "Drivers/STM32F4xx_HAL_Driver/Inc",
            "Drivers/CMSIS/Device/ST/STM32F4xx/Include", "Drivers/CMSIS/Include"]
subprocess.run([str(toolchain / "arm-none-eabi-gcc.exe"),
                "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=soft", "-Og", "-g",
                "-ffunction-sections", "-fdata-sections", "-fno-builtin",
                "-DUSE_HAL_DRIVER", "-DSTM32F405xx",
                "-DSTBY_CONTROL_MODE=" + ("3" if args.pin_test else "2"),
                *["-I" + str(root / p) for p in includes],
                str(root / f"tests/{source_name}.c"), "-nostdlib",
                "-Wl,--gc-sections,-e,run_tests,-Ttext=0x10000,-Tdata=0x30000",
                "-o", str(output)], check=True)
with output.open("rb") as source:
    elf = ELFFile(source)
    symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    cpu.mem_map(0x10000, 0x100000)
    cpu.mem_map(0x20000000, 0x10000)
    for section in elf.iter_sections():
        if section["sh_flags"] & 2 and section["sh_size"]:
            cpu.mem_write(section["sh_addr"], section.data())
    cpu.reg_write(UC_ARM_REG_SP, 0x2000FFF0)
    cpu.reg_write(UC_ARM_REG_LR, 0xF0001)
    cpu.emu_start(symbols["run_tests"] | 1, 0xF0000, count=2000000)
    from unicorn.arm_const import UC_ARM_REG_PC
    if cpu.reg_read(UC_ARM_REG_PC) != 0xF0000:
        raise SystemExit("FAIL: test did not return within instruction limit")
    failure = cpu.reg_read(UC_ARM_REG_R0)
    if failure:
        raise SystemExit(f"FAIL: tests/{source_name}.c:{failure}")
print("PASS: production display status, live DIP override and production restoration" if args.display_status else "PASS: 595 pin test, all 16 outputs, phase timing, SYS LEDs, input independence, tick wrap" if args.pin_test else "PASS: production AUTO flow, DIP3 LED durability pattern and relay inhibition, live mode transitions and debounce, SPI recovery, tick wrap")
