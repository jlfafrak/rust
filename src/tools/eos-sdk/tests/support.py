import json
import os
from pathlib import Path
import struct
import subprocess
import sys


SDK_SOURCE_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[4]
LINK_WRAPPER = SDK_SOURCE_ROOT / "bin" / "eos-rust-link"
ELF_VALIDATOR = SDK_SOURCE_ROOT / "bin" / "eos-elf-validate"
AUTH_PACKAGER = SDK_SOURCE_ROOT / "bin" / "eos-auth-package"
AUTH_MARKER = b"martos_smp_elf_authentication_block_sha2_256_adbc_1394_e532_101\n"


def write_executable(path: Path, source: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(source, encoding="utf-8")
    path.chmod(0o755)


def minimal_elf(entry: int = 0x1000) -> bytes:
    """Return a bounded ELF32/LE/ARM header with one LOAD and one null section."""
    ident = b"\x7fELF\x01\x01\x01" + b"\x00" * 9
    header = struct.pack(
        "<16sHHIIIIIHHHHHH",
        ident,
        3,  # ET_DYN
        40,  # EM_ARM
        1,
        entry,
        52,
        84,
        0x05000200,
        52,
        32,
        1,
        40,
        1,
        0,
    )
    file_size = 124
    program_header = struct.pack(
        "<IIIIIIII", 1, 0, 0, 0, file_size, file_size, 5, 0x1000
    )
    return header + program_header + bytes(40)


def valid_tool_outputs() -> dict[str, str]:
    return {
        "-hW": """ELF Header:
  Class:                             ELF32
  Data:                              2's complement, little endian
  Type:                              DYN (Shared object file)
  Machine:                           ARM
  Entry point address:               0x1000
  Flags:                             0x5000200, Version5 EABI, soft-float ABI
""",
        "-lW": """Program Headers:
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  ARM_EXIDX      0x000100 0x00001000 0x00001000 0x00008 0x00008 R   0x4
  INTERP         0x0000f4 0x000000f4 0x000000f4 0x00011 0x00011 R   0x1
      [Requesting program interpreter: /usr/lib/ld.so.1]
  LOAD           0x000000 0x00000000 0x00000000 0x00100 0x00100 R E 0x1000
  LOAD           0x001000 0x00001000 0x00001000 0x00100 0x00200 RW  0x1000
  DYNAMIC        0x001040 0x00001040 0x00001040 0x00080 0x00080 RW  0x4
""",
        "-SW": """Section Headers:
  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            00000000 000000 000000 00      0   0  0
  [ 1] .text             PROGBITS        00001000 000100 000010 00  AX  0   0  4
  [ 2] .ARM.exidx        ARM_EXIDX       00001010 000110 000008 00  AL  1   0  4
  [ 3] .dynamic          DYNAMIC         00002000 000200 000080 08  WA  0   0  4
""",
        "-rW": """Relocation section '.rel.dyn' at offset 0x200 contains 2 entries:
 Offset     Info    Type                Sym. Value  Symbol's Name
00002000  00000017 R_ARM_RELATIVE
00002004  00000102 R_ARM_ABS32          00000000   os_app_get_id
Relocation section '.rel.plt' at offset 0x210 contains 1 entry:
00002008  00000116 R_ARM_JUMP_SLOT      00000000   os_app_get_id
""",
        "-dW": """Dynamic section at offset 0x200 contains 3 entries:
 0x00000001 (NEEDED)                     Shared library: [libmartos_app.so.1.0]
 0x6ffffffb (FLAGS_1)                    Flags: PIE
 0x00000000 (NULL)                       0x0
""",
        "-AW": """Attribute Section: aeabi
File Attributes
  Tag_CPU_name: "7-A"
  Tag_CPU_arch: v7
  Tag_CPU_arch_profile: Application
  Tag_ARM_ISA_use: Yes
  Tag_THUMB_ISA_use: Thumb-2
  Tag_FP_arch: VFPv3
  Tag_Advanced_SIMD_arch: NEONv1
""",
        "-Ws": """Symbol table '.dynsym' contains 3 entries:
   Num:    Value  Size Type    Bind   Vis      Ndx Name
     0: 00000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 00001000    16 FUNC    GLOBAL DEFAULT    1 main
     2: 00000000     0 FUNC    GLOBAL DEFAULT  UND os_app_get_id

Symbol table '.symtab' contains 2 entries:
   Num:    Value  Size Type    Bind   Vis      Ndx Name
     0: 00000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 00001000    16 FUNC    GLOBAL DEFAULT    1 main
""",
        "nm": "00001000 T main\n",
    }


def install_fake_elf_tools(root: Path, outputs: dict[str, str]) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    fixture = root / "tool-output.json"
    control = root / "tool-control.json"
    fixture.write_text(json.dumps(outputs), encoding="utf-8")
    tool = f"""#!/usr/bin/env python3
import json
from pathlib import Path
import sys

outputs = json.loads(Path({str(fixture)!r}).read_text(encoding="utf-8"))
control_path = Path({str(control)!r})
control = json.loads(control_path.read_text(encoding="utf-8")) if control_path.exists() else {{}}
name = Path(sys.argv[0]).name
key = "nm" if name.endswith("nm") else sys.argv[1]
failure = control.get("failure")
if failure == key:
    print("synthetic tool failure", file=sys.stderr)
    raise SystemExit(7)
original = control.get("original")
replacement = control.get("replacement")
replacement_once = control.get("replacement_once")
if original and replacement and replacement_once and not Path(replacement_once).exists():
    Path(original).write_bytes(Path(replacement).read_bytes())
    Path(replacement_once).touch()
expected = control.get("expected")
if expected and Path(sys.argv[-1]).read_bytes() != Path(expected).read_bytes():
    print("inspection target bytes differ from the owned snapshot", file=sys.stderr)
    raise SystemExit(9)
sys.stdout.write(outputs[key])
"""
    bin_dir = root / "arm-gnu" / "bin"
    write_executable(bin_dir / "arm-none-eabi-readelf", tool)
    write_executable(bin_dir / "arm-none-eabi-nm", tool)
    return fixture


def run_python(script: Path, args: list[str], *, cwd: Path, env: dict[str, str]):
    merged = os.environ.copy()
    merged.update(env)
    return subprocess.run(
        [sys.executable, str(script), *args],
        cwd=cwd,
        env=merged,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=20,
        check=False,
    )
