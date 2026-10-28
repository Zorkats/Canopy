#!/usr/bin/env python3
"""Cut the patterns Canopy finds its game addresses with on an unchecked build.

Canopy knows the addresses of every game build listed in
src/common/game_builds.txt. On any other build it searches the running
village.exe with the lines of src/common/game_addresses.txt, each line one way
to find one address. This script writes those lines from builds whose
addresses are known:

    pip install -r scripts/requirements.txt
    python scripts/gen_address_patterns.py "village <new>.exe" "village 1.30.exe"

Every exe must be a build listed in game_builds.txt. New lines are cut from
the first one that has the name. A line, old or new, is kept when it finds the
listed address on one exe and never finds a different address on the others,
so lines written by hand survive as long as they still pass, and a line only
an older build needs stays for it. Each name gets up to
--want lines, the ones that work on the most builds first. The file is
rewritten in place, keeping its leading comment block.

Fields are offsets and sizes of the game's layouts, declared with GAME_FIELD
in src/common/game_retail.h. A field line reads one out of an instruction of a
function the same source files use, such as the 0x208 of
`mov rax,[rcx+0x208]`, so a build whose layout moved is caught. Its listed
value is the build's value in game_builds.txt, or the GAME_FIELD value while
the build lists none. With --candidates every plain `constexpr size_t` offset
is tried as well, and the ones a line was found for are named, to see which
can become fields. A field's lines come from two functions where they can, so
one that matches by chance has to agree with another. With --wider the
functions those functions call, and the ones that call them, are tried too,
and their lines are only kept where a second function agrees.

Run `canopy addresses <exe>` afterwards: the loader's own search decides what
counts, and this script only proposes.
"""

import argparse
import bisect
import os
import re
import struct
import sys

try:
    import capstone
    import numpy
except ImportError:
    sys.exit("Needs capstone and numpy: pip install -r scripts/requirements.txt")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILDS_PATH = os.path.join(ROOT, "src", "common", "game_builds.txt")
PATTERNS_PATH = os.path.join(ROOT, "src", "common", "game_addresses.txt")
RETAIL_PATH = os.path.join(ROOT, "src", "common", "game_retail.h")
SOURCE_PATH = os.path.join(ROOT, "src")

SCN_MEM_EXECUTE = 0x20000000
# Pattern lengths tried, shortest first. Short patterns survive more code
# edits; long ones are less likely to match somewhere else.
LENGTHS = (24, 32, 48, 64, 96, 128)
MIN_SOLID = 12
# Referencing sites tried per address, and instructions a ref pattern may
# start before its referencing one.
MAX_SITES = 80
MAX_LEAD = 3
# A field this small is in too many instructions to say anything, and 0 is
# no displacement at all.
MIN_FIELD = 0x10
# Names whose value may be an instruction's immediate rather than a
# displacement, like the length a memset is given.
SIZE_NAME = re.compile(r"(Size|Capacity|Length|Stride)$")

DISASSEMBLER = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
DISASSEMBLER.detail = True


# =============================================================
# IMAGES
# =============================================================
class Image:
    """A PE file laid out by RVA, as the Windows loader maps it, unrelocated."""

    def __init__(self, path):
        self.path = path
        data = open(path, "rb").read()
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        count = struct.unpack_from("<H", data, pe + 6)[0]
        self.build = struct.unpack_from("<I", data, pe + 8)[0]
        optional_size = struct.unpack_from("<H", data, pe + 20)[0]
        optional = pe + 24
        self.image_base = struct.unpack_from("<Q", data, optional + 24)[0]
        self.size = struct.unpack_from("<I", data, optional + 56)[0]
        headers_size = struct.unpack_from("<I", data, optional + 60)[0]
        directories = optional + 112
        image = bytearray(self.size)
        image[:headers_size] = data[:headers_size]
        self.sections = []
        self.raw_sizes = {}
        for index in range(count):
            entry = pe + 24 + optional_size + index * 40
            virtual_size, address, raw_size, raw = struct.unpack_from("<IIII", data, entry + 8)
            characteristics = struct.unpack_from("<I", data, entry + 36)[0]
            length = min(virtual_size, raw_size)
            image[address:address + length] = data[raw:raw + length]
            self.sections.append((address, virtual_size, bool(characteristics & SCN_MEM_EXECUTE)))
            self.raw_sizes[address] = raw_size
        self.bytes = bytes(image)
        self.code = [(a, s) for a, s, e in self.sections if e]
        self.data = [(a, s) for a, s, e in self.sections if not e]
        self.relocated = self._relocated(directories)
        self.functions = self._functions(directories)
        self.function_starts = [begin for begin, _ in self.functions]

    def _directory(self, directories, index):
        return struct.unpack_from("<II", self.bytes, directories + index * 8)

    def _relocated(self, directories):
        # Bytes the loader rewrites when it relocates the image. The running
        # game differs from the file there, so no pattern may pin them.
        marked = bytearray(self.size)
        address, size = self._directory(directories, 5)
        end = address + size
        while address < end:
            page, block = struct.unpack_from("<II", self.bytes, address)
            if block < 8:
                break
            for offset in range(address + 8, address + block, 2):
                entry = struct.unpack_from("<H", self.bytes, offset)[0]
                kind, where = entry >> 12, page + (entry & 0xFFF)
                if kind == 10:
                    marked[where:where + 8] = b"\x01" * 8
                elif kind == 3:
                    marked[where:where + 4] = b"\x01" * 4
            address += block
        return marked

    def _functions(self, directories):
        # Unwind ranges from .pdata. A range whose unwind info is chained
        # (UNW_FLAG_CHAININFO) is a piece of the function its chain ends at.
        address, size = self._directory(directories, 3)
        functions, self.parents = [], {}
        for offset in range(address, address + size, 12):
            begin, end, unwind = struct.unpack_from("<III", self.bytes, offset)
            if not begin:
                continue
            functions.append((begin, end))
            if self.bytes[unwind] >> 3 & 4:
                codes = self.bytes[unwind + 2]
                self.parents[begin] = struct.unpack_from("<I", self.bytes, unwind + 4 + 2 * (codes + (codes & 1)))[0]
        functions.sort()
        return functions

    def function_start(self, rva):
        """Entry of the function whose unwind ranges hold rva, or None."""
        index = bisect.bisect_right(self.function_starts, rva) - 1
        if index < 0 or not (self.functions[index][0] <= rva < self.functions[index][1]):
            return None
        begin = self.functions[index][0]
        for _ in range(16):
            if begin not in self.parents:
                return begin
            begin = self.parents[begin]
        return None

    def in_code(self, rva):
        return any(a <= rva < a + s for a, s in self.code)

    def in_data(self, rva):
        return any(a <= rva < a + s for a, s in self.data)

    def region(self, rva):
        """The function around rva: its unwind range, or up to the next one."""
        index = bisect.bisect_right(self.function_starts, rva) - 1
        if index >= 0:
            begin, end = self.functions[index]
            if begin <= rva < end:
                return begin, end
        following = self.function_starts[index + 1] if index + 1 < len(self.functions) else rva + 256
        return rva, min(following, rva + 256)


def decode(image, begin, end):
    return list(DISASSEMBLER.disasm(image.bytes[begin:end], begin))


def is_rip_relative(instruction):
    return any(
        op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP
        for op in instruction.operands
    )


def is_branch(instruction):
    return instruction.group(capstone.CS_GRP_JUMP) or instruction.group(capstone.CS_GRP_CALL)


def masked(image, instructions, length):
    """Bytes and mask of the instructions until at least length bytes."""
    values, mask = [], []
    for instruction in instructions:
        solid = [True] * instruction.size
        if instruction.disp_size and is_rip_relative(instruction):
            for k in range(instruction.disp_offset, instruction.disp_offset + instruction.disp_size):
                solid[k] = False
        if is_branch(instruction) and instruction.imm_size == 4:
            for k in range(instruction.imm_offset, instruction.imm_offset + 4):
                solid[k] = False
        for k in range(instruction.size):
            if image.relocated[instruction.address + k]:
                solid[k] = False
        values += instruction.bytes
        mask += solid
        if len(values) >= length:
            return bytes(values), mask
    return None


# =============================================================
# PATTERNS
# =============================================================
class Line:
    """One way to find one address: a line of game_addresses.txt."""

    def __init__(self, name, kind, options, values=b"", mask=(), type_name=""):
        self.name, self.kind, self.options = name, kind, dict(options)
        self.values, self.mask, self.type_name = values, list(mask), type_name

    def text(self):
        if self.kind == "rtti":
            text = f"{self.name} rtti {self.type_name}"
            if self.options.get("offset", 0):
                text += f" offset={self.options['offset']}"
            if "slot" in self.options:
                text += f" slot={self.options['slot']}"
            return text
        options = ""
        if self.kind == "code" and self.options.get("at", 0):
            options = f" at={self.options['at']}"
        elif self.kind == "string":
            options = f" {self.type_name} lea={self.options['lea']} at={self.options['at']}"
        elif self.kind == "ref":
            options = f" disp={self.options['disp']} next={self.options['next']} to={self.options['to']}"
        elif self.kind == "field":
            options = f" read={self.options['read']} size={self.options['size']}"
        tokens = " ".join(f"{v:02X}" if s else "??" for v, s in zip(self.values, self.mask))
        return f"{self.name} {self.kind}{options} {tokens}"


def parse_line(text):
    parts = text.split()
    name, kind = parts[0], parts[1]
    if kind == "rtti":
        options = {key: int(value, 0) for key, value in (p.split("=") for p in parts[3:])}
        return Line(name, kind, options, type_name=parts[2])
    type_name, rest = "", parts[2:]
    if kind == "string":
        type_name, rest = parts[2], parts[3:]
    options, values, mask = {}, [], []
    for part in rest:
        if "=" in part:
            key, value = part.split("=")
            options[key] = value if key == "to" or value == "start" else int(value, 0)
        elif part in ("?", "??"):
            values.append(0)
            mask.append(False)
        else:
            values.append(int(part, 16))
            mask.append(True)
    return Line(name, kind, options, bytes(values), mask, type_name)


def initialized(image, section):
    """The part of a data section the file fills; the rest is zeros."""
    begin, size = section
    return begin, min(size, image.raw_sizes[begin])


def find_string(image, text):
    """The one NUL-delimited string text in the data sections, or None."""
    needle = b"\x00" + text.encode() + b"\x00"
    found = []
    for begin, size in (initialized(image, s) for s in image.data):
        at = image.bytes.find(needle, begin, begin + size)
        while at >= 0 and len(found) < 2:
            found.append(at + 1)
            at = image.bytes.find(needle, at + 1, begin + size)
    return found[0] if len(found) == 1 else None


def find(image, values, mask, limit=2):
    """Start of every match in the code sections, up to limit."""
    best, run = (0, 0), None
    for k in range(len(mask)):
        if mask[k] and (run is None):
            run = k
        if run is not None and (k + 1 == len(mask) or not mask[k + 1]):
            if k + 1 - run > best[1] - best[0]:
                best = (run, k + 1)
            run = None
    anchor = values[best[0]:best[1]]
    hits = []
    if not anchor:
        return hits
    for begin, size in image.code:
        end = begin + size
        at = image.bytes.find(anchor, begin + best[0], end)
        while at >= 0:
            start = at - best[0]
            if start >= begin and start + len(values) <= end and all(
                image.bytes[start + k] == values[k] for k in range(len(values)) if mask[k]
            ):
                hits.append(start)
                if len(hits) >= limit:
                    return hits
            at = image.bytes.find(anchor, at + 1, end)
    return hits


def moved(image, hit, at):
    """hit moved by at bytes, or to the start of its function for at=start."""
    return image.function_start(hit) if at == "start" else hit + at


def resolve(image, line):
    """The address line finds in image, or None."""
    if line.kind == "rtti":
        vtable = find_vtable(image, line.type_name, line.options.get("offset", 0))
        if vtable is None or "slot" not in line.options:
            return vtable
        function = struct.unpack_from("<Q", image.bytes, vtable + line.options["slot"] * 8)[0] - image.image_base
        return function if image.in_code(function) else None
    if line.kind == "string":
        text = find_string(image, line.type_name)
        if text is None:
            return None
        lea = line.options["lea"]
        hits = [
            hit for hit in find(image, line.values, line.mask, limit=64)
            if hit + lea + 4 + struct.unpack_from("<i", image.bytes, hit + lea)[0] == text
        ]
        return moved(image, hits[0], line.options["at"]) if len(hits) == 1 else None
    hits = find(image, line.values, line.mask)
    if len(hits) != 1:
        return None
    if line.kind == "field":
        size = line.options["size"]
        value = int.from_bytes(image.bytes[hits[0] + line.options["read"]:][:size], "little", signed=True)
        return value if value >= 0 else None
    if line.kind == "code":
        return moved(image, hits[0], line.options.get("at", 0))
    disp = struct.unpack_from("<i", image.bytes, hits[0] + line.options["disp"])[0]
    target = hits[0] + line.options["next"] + disp
    inside = image.in_code(target) if line.options["to"] == "code" else image.in_data(target)
    return target if inside else None


def find_vtable(image, type_name, offset):
    # MSVC RTTI: the type descriptor holds the decorated name 16 bytes in, a
    # complete object locator names the type descriptor and the subobject
    # offset, and the vtable's slot -1 points at the locator.
    found = []
    needle = type_name.encode() + b"\x00"
    for begin, size in image.data:
        at = image.bytes.find(needle, begin, begin + size)
        while at >= 0:
            if at % 8 == 0 and image.bytes[at - 1] == 0:
                found.append(at - 16)
            at = image.bytes.find(needle, at + 1, begin + size)
    if len(found) != 1:
        return None
    descriptor = struct.pack("<I", found[0])
    locators = []
    for begin, size in image.data:
        at = image.bytes.find(descriptor, begin, begin + size)
        while at >= 0:
            locator = at - 12
            if locator % 4 == 0 and struct.unpack_from("<II", image.bytes, locator) == (1, offset):
                locators.append(locator)
            at = image.bytes.find(descriptor, at + 1, begin + size)
    if len(locators) != 1:
        return None
    pointer = struct.pack("<Q", image.image_base + locators[0])
    slots = []
    for begin, size in image.data:
        at = image.bytes.find(pointer, begin, begin + size)
        while at >= 0:
            if at % 8 == 0:
                slots.append(at + 8)
            at = image.bytes.find(pointer, at + 1, begin + size)
    return slots[0] if len(slots) == 1 else None


def vtable_type(image, rva):
    """Decorated type name and subobject offset of the vtable at rva."""
    if not image.in_data(rva - 8):
        return None
    locator = struct.unpack_from("<Q", image.bytes, rva - 8)[0] - image.image_base
    if not (0 < locator < image.size - 24) or struct.unpack_from("<I", image.bytes, locator)[0] != 1:
        return None
    offset, _, descriptor = struct.unpack_from("<III", image.bytes, locator + 4)
    if not (0 < descriptor < image.size - 16):
        return None
    name = image.bytes[descriptor + 16:descriptor + 272].split(b"\x00")[0]
    return (name.decode("ascii"), offset) if name.startswith(b".?A") else None


# =============================================================
# CUTTING NEW LINES
# =============================================================
def cut_code(image, name, target):
    begin, end = image.region(target)
    # A patch site may name an immediate inside an instruction; cut from the
    # instruction that holds it.
    first = target
    if begin < target:
        for instruction in decode(image, begin, target + 16):
            if instruction.address <= target < instruction.address + instruction.size:
                first = instruction.address
                break
    starts = [first] + ([begin] if begin != first and target - begin <= 256 else [])
    for start in starts:
        instructions = decode(image, start, end)
        for length in LENGTHS:
            cut = masked(image, instructions, length)
            if cut is None:
                break
            if sum(cut[1]) < MIN_SOLID:
                continue
            if find(image, *cut) == [start]:
                yield Line(name, "code", {"at": target - start}, *cut)
                break


def cut_strings(image, name, target):
    """Lines that find target through a unique string its function loads."""
    begin, end = image.region(target)
    for instruction in decode(image, begin, end):
        if instruction.mnemonic != "lea" or not is_rip_relative(instruction) or instruction.disp_size != 4:
            continue
        string = instruction.address + instruction.size + instruction.disp
        if not image.in_data(string) or image.bytes[string - 1] != 0:
            continue
        raw = image.bytes[string:string + 65].split(bytes(1))[0]
        if not (3 <= len(raw) <= 64) or any(c < 0x20 or c >= 0x7F for c in raw):
            continue
        text = raw.decode("ascii")
        if " " in text or find_string(image, text) != string:
            continue
        instructions = decode(image, instruction.address, end)
        for length in (16, 24, 32):
            cut = masked(image, instructions, length)
            if cut is None:
                break
            at = "start" if image.function_start(target) == target else target - instruction.address
            options = {"lea": instruction.disp_offset, "at": at}
            line = Line(name, "string", options, *cut, type_name=text)
            if resolve(image, line) == target:
                yield line
                return


def cut_vtable_slots(image, name, target):
    """Lines that read target out of a vtable found by its RTTI type."""
    pointer = struct.pack("<Q", image.image_base + target)
    for begin, size in (initialized(image, s) for s in image.data):
        at = image.bytes.find(pointer, begin, begin + size)
        while at >= 0:
            for slot in range(0, 64):
                vtable = vtable_type(image, at - slot * 8)
                if vtable:
                    line = Line(name, "rtti", {"offset": vtable[1], "slot": slot}, type_name=vtable[0])
                    if resolve(image, line) == target:
                        yield line
                    break
            at = image.bytes.find(pointer, at + 1, begin + size)


def reference_sites(image, targets):
    """Every instruction that reaches a target through a rel32 or a rip disp."""
    sites = {target: [] for target in targets}
    wanted = numpy.array(sorted(targets), dtype=numpy.int64)
    for begin, size in image.code:
        text = image.bytes[begin:begin + size]
        for phase in range(4):
            count = (len(text) - phase) // 4
            values = numpy.frombuffer(text, dtype="<i4", count=count, offset=phase).astype(numpy.int64)
            positions = begin + phase + numpy.arange(count, dtype=numpy.int64) * 4
            for tail in (0, 1, 2, 4):
                reached = positions + 4 + tail + values
                for index in numpy.nonzero(numpy.isin(reached, wanted))[0]:
                    sites[int(reached[index])].append(int(positions[index]))
    return sites


def cut_refs(image, name, target, positions):
    to = "code" if image.in_code(target) else "data"
    regions_used = set()
    for position in positions[:MAX_SITES]:
        begin, end = image.region(position)
        if begin in regions_used:
            continue
        instructions = decode(image, begin, end)
        index = next(
            (
                i for i, ins in enumerate(instructions)
                if ins.address < position < ins.address + ins.size
            ),
            None,
        )
        if index is None:
            continue
        site = instructions[index]
        displacement = position - site.address
        if not ((site.disp_size == 4 and site.disp_offset == displacement and is_rip_relative(site)) or
                (is_branch(site) and site.imm_size == 4 and site.imm_offset == displacement)):
            continue
        if site.address + site.size + struct.unpack_from("<i", image.bytes, position)[0] != target:
            continue
        for lead in range(0, MAX_LEAD + 1):
            if index - lead < 0:
                break
            start = instructions[index - lead].address
            done = False
            for length in LENGTHS:
                cut = masked(image, instructions[index - lead:], length)
                if cut is None:
                    break
                # The pattern has to cover the displacement it reads.
                if len(cut[0]) < position + 4 - start or sum(cut[1]) < MIN_SOLID:
                    continue
                if find(image, *cut) == [start]:
                    options = {"disp": position - start, "next": site.address + site.size - start, "to": to}
                    yield Line(name, "ref", options, *cut)
                    regions_used.add(begin)
                    done = True
                    break
            if done:
                break


def frame_bases(instructions):
    """Registers that address a function's stack frame: rsp, and rbp where the
    function points it into the frame. Their displacements are stack slots,
    not fields, whatever value they share with one."""
    x86 = capstone.x86
    bases = {x86.X86_REG_RSP}
    for instruction in instructions:
        operands = instruction.operands
        if (instruction.id in (x86.X86_INS_LEA, x86.X86_INS_MOV) and len(operands) == 2 and
                operands[0].type == x86.X86_OP_REG and operands[0].reg == x86.X86_REG_RBP and
                ((operands[1].type == x86.X86_OP_MEM and operands[1].mem.base == x86.X86_REG_RSP) or
                 (operands[1].type == x86.X86_OP_REG and operands[1].reg == x86.X86_REG_RSP))):
            bases.add(x86.X86_REG_RBP)
    return bases


def is_frame_slot(instruction, bases):
    return any(op.type == capstone.x86.X86_OP_MEM and op.mem.base in bases for op in instruction.operands)


def reads_frame(image, line):
    """A field line whose value is a stack slot's displacement."""
    hits = find(image, line.values, line.mask)
    if len(hits) != 1:
        return False
    read = hits[0] + line.options["read"]
    instructions = decode(image, *image.region(hits[0]))
    bases = frame_bases(instructions)
    return any(
        instruction.address <= read < instruction.address + instruction.size and
        instruction.disp_size and is_frame_slot(instruction, bases)
        for instruction in instructions
    )


def field_sites(image, value, immediate, functions):
    """Instructions of functions that hold value: (instructions, index, offset, size)."""
    for begin, end in functions:
        instructions = decode(image, begin, end)
        bases = frame_bases(instructions)
        for index, instruction in enumerate(instructions):
            if (instruction.disp_size in (1, 4) and instruction.disp == value and
                    not is_rip_relative(instruction) and not is_frame_slot(instruction, bases)):
                yield instructions, index, instruction.disp_offset, instruction.disp_size
            elif (immediate and instruction.imm_size in (1, 2, 4) and not is_branch(instruction) and
                  any(op.type == capstone.x86.X86_OP_IMM and op.imm == value for op in instruction.operands)):
                yield instructions, index, instruction.imm_offset, instruction.imm_size


def neighbours(image, functions):
    """The functions functions call directly, and the ones that call them."""
    found = set()
    for begin, end in functions:
        for instruction in decode(image, begin, end):
            if (instruction.group(capstone.CS_GRP_CALL) and instruction.operands and
                    instruction.operands[0].type == capstone.x86.X86_OP_IMM and
                    image.in_code(instruction.operands[0].imm)):
                found.add(image.region(instruction.operands[0].imm))
    for hits in reference_sites(image, {begin for begin, _ in functions}).values():
        for hit in hits:
            # rel32 of a call: the opcode byte is right before it.
            if hit > 0 and image.bytes[hit - 1] == 0xE8:
                found.add(image.region(hit - 1))
    return sorted(found)


def cut_fields(image, name, value, functions):
    """Lines that read value out of an instruction of the functions."""
    for instructions, index, offset, size in field_sites(image, value, SIZE_NAME.search(name), functions):
        site = instructions[index]
        read = site.address + offset
        for lead in range(0, min(MAX_LEAD, index) + 1):
            start = instructions[index - lead].address
            done = False
            for length in LENGTHS:
                cut = masked(image, instructions[index - lead:], length)
                if cut is None:
                    break
                values, mask = cut
                if len(values) < read + size - start:
                    continue
                for k in range(read - start, read - start + size):
                    mask[k] = False
                if sum(mask) < MIN_SOLID:
                    continue
                if find(image, values, mask) == [start]:
                    yield Line(name, "field", {"read": read - start, "size": size}, values, mask)
                    done = True
                    break
            if done:
                break


# =============================================================
# FILES
# =============================================================
def read_builds():
    builds, current = {}, None
    for raw in open(BUILDS_PATH, encoding="utf-8"):
        text = raw.split("#", 1)[0].strip()
        if not text:
            continue
        parts = text.split()
        if parts[0] == "build":
            current = {"order": [], "addresses": {}}
            builds[int(parts[1], 16)] = current
        else:
            current["order"].append(parts[0])
            current["addresses"][parts[0]] = None if parts[1] == "none" else int(parts[1], 16)
    return builds


def read_patterns():
    header, lines = [], []
    if os.path.exists(PATTERNS_PATH):
        for raw in open(PATTERNS_PATH, encoding="utf-8"):
            text = raw.rstrip("\n")
            if not lines and (not text.strip() or text.lstrip().startswith("#")):
                header.append(text)
            elif text.strip() and not text.lstrip().startswith("#"):
                lines.append(parse_line(text))
    return header, lines


def read_fields(candidates):
    """GAME_FIELD values by name, with candidates every plain size_t constant too."""
    text = open(RETAIL_PATH, encoding="utf-8").read()
    fields = {m[1]: int(m[2], 0) for m in re.finditer(r"GAME_FIELD\((\w+), (0x[0-9A-Fa-f]+|\d+)\)", text)}
    if candidates:
        for m in re.finditer(r"constexpr size_t k(\w+) = (0x[0-9A-Fa-f]+|\d+);", text):
            fields.setdefault(m[1], int(m[2], 0))
    return fields


def read_vias():
    """GAME_FIELD_VIA witnesses by name."""
    text = open(RETAIL_PATH, encoding="utf-8").read()
    return {m[1]: m[2] for m in re.finditer(r"GAME_FIELD_VIA\((\w+), (\w+)\)", text)}


def related_names():
    """Each k-name of the sources, mapped to the k-names used beside it in a file."""
    used = {}
    for folder, _, files in os.walk(SOURCE_PATH):
        for file in files:
            path = os.path.join(folder, file)
            if not file.endswith((".cpp", ".h")) or os.path.samefile(path, RETAIL_PATH):
                continue
            names = set(re.findall(r"\bk([A-Z]\w*)\b", open(path, encoding="utf-8", errors="replace").read()))
            for name in names:
                used.setdefault(name, set()).update(names)
    return used


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exes", nargs="+", help="village.exe builds listed in game_builds.txt, the newest first")
    parser.add_argument("--want", type=int, default=3, help="lines to keep per name (default 3)")
    parser.add_argument("--candidates", action="store_true",
                        help="also try every plain size_t constant of game_retail.h as a field")
    parser.add_argument("--wider", action="store_true",
                        help="also cut fields from the functions one call away, where two functions agree")
    args = parser.parse_args()

    builds = read_builds()
    images = [Image(path) for path in args.exes]
    for image in images:
        if image.build not in builds:
            sys.exit(f"{image.path}: build 0x{image.build:08X} is not in game_builds.txt")
    header, old_lines = read_patterns()
    fields = read_fields(args.candidates)

    # Each name is cut from the newest build that has it. A build that lists
    # a name as none has no such site, so a line that finds one there is wrong.
    sources = {}
    for image in images:
        for name in builds[image.build]["order"]:
            if name not in sources and name not in fields and builds[image.build]["addresses"][name] is not None:
                sources[name] = image
    # Fields are cut from the newest build, where a build lists no value the
    # GAME_FIELD one.
    for name in fields:
        sources[name] = images[0]

    def listed(image, name):
        addresses = builds[image.build]["addresses"]
        return addresses[name] if name in addresses else fields.get(name)

    def score(line):
        """Builds line finds the right address on, or None when it is ever wrong."""
        right = 0
        for image in images:
            addresses = builds[image.build]["addresses"]
            if line.name in fields:
                addresses = {line.name: listed(image, line.name)}
            found = resolve(image, line)
            if line.name in addresses and addresses[line.name] is None:
                if found is not None:
                    return None
                continue
            if line.name not in addresses or found is None:
                continue
            if found != addresses[line.name]:
                return None
            right += 1
        return right or None

    kept = {name: [] for name in sources}
    for line in old_lines:
        if line.name in kept:
            if line.kind == "field" and any(reads_frame(image, line) for image in images):
                print(f"Dropped, it reads a stack slot: {line.text()}")
                continue
            right = score(line)
            if right is not None:
                kept[line.name].append((right, line))

    checked = {}

    def right_on(image, line):
        key = (image.build, line.text())
        if key not in checked:
            value = listed(image, line.name)
            checked[key] = value is not None and resolve(image, line) == value
        return checked[key]

    def uncovered(name):
        """Builds that list name but that no kept line finds it on."""
        return [
            image for image in images
            if listed(image, name) is not None and not (name in fields and listed(image, name) < MIN_FIELD)
            and not any(right_on(image, line) for _, line in kept[name])
        ]

    places = {}

    def place(image, line):
        """The function a field line's match lies in on image, or None."""
        key = (image.build, line.text())
        if key not in places:
            hits = find(image, line.values, line.mask) if line.kind == "field" else []
            places[key] = (image.function_start(hits[0]) or image.region(hits[0])[0]) if len(hits) == 1 else None
        return places[key]

    def spread(image, lines):
        """The functions of the lines right on image."""
        return {place(image, line) for line in lines if right_on(image, line)} - {None}

    def one_place(name):
        """A field whose lines on the newest build all lie in one function.
        Two functions that agree are much less likely to match by chance."""
        value = listed(images[0], name)
        return (name in fields and value is not None and value >= MIN_FIELD and
                len(spread(images[0], [line for _, line in kept[name]])) < 2)

    needed = [name for name, lines in kept.items() if len(lines) < args.want or uncovered(name) or one_place(name)]
    used = related_names()
    sites = {}
    for image in images:
        mine = [name for name in needed if sources[name] is image and name not in fields]
        found = reference_sites(image, {builds[image.build]["addresses"][name] for name in mine})
        sites.update({(image.build, target): hits for target, hits in found.items()})
    for number, name in enumerate(needed, 1):
        reference = sources[name]
        target = listed(reference, name)
        candidates = []
        if name in fields:
            existing = {line.text() for _, line in kept[name]}
            # Cut from each build no kept line reads the value on yet, in the
            # functions of the addresses the field's own source files use.
            for image in images:
                value = listed(image, name)
                if value is None or value < MIN_FIELD:
                    continue
                enough = lambda: (len(kept[name]) >= args.want and
                                  len(spread(image, [line for _, line in kept[name]])) >= 2)
                if any(resolve(image, line) == value for _, line in kept[name]) and enough():
                    continue
                addresses = builds[image.build]["addresses"]
                own = sorted({
                    image.region(addresses[other]) for other in used.get(name, ())
                    if other not in fields and addresses.get(other) is not None and image.in_code(addresses[other])
                })
                reads = lambda: any(resolve(image, line) == value for _, line in kept[name])
                pools = [own]
                if args.wider:
                    pools.append([region for region in neighbours(image, own) if region not in own])
                for pool in pools:
                    start = len(kept[name])
                    for line in cut_fields(image, name, value, pool):
                        if enough() and reads():
                            break
                        right = score(line)
                        if right is not None and line.text() not in existing:
                            kept[name].append((right, line))
                            existing.add(line.text())
                    # A neighbour is further from what the sources do with
                    # the field, so its lines only count where a second
                    # function agrees.
                    if pool is not own and len(spread(image, [line for _, line in kept[name]])) < 2:
                        for _, line in kept[name][start:]:
                            existing.discard(line.text())
                        del kept[name][start:]
            print(f"[{number}/{len(needed)}] {name}: {len(kept[name])} lines", file=sys.stderr)
            continue
        if reference.in_code(target):
            candidates += list(cut_code(reference, name, target))
            candidates += list(cut_vtable_slots(reference, name, target))
            candidates += list(cut_strings(reference, name, target))
        else:
            vtable = vtable_type(reference, target)
            if vtable:
                candidates.append(Line(name, "rtti", {"offset": vtable[1]}, type_name=vtable[0]))
        existing = {line.text() for _, line in kept[name]}
        for line in candidates:
            right = score(line)
            if right is not None and line.text() not in existing:
                kept[name].append((right, line))
                existing.add(line.text())
        for line in cut_refs(reference, name, target, sites[(reference.build, target)]):
            if len(kept[name]) >= args.want:
                break
            right = score(line)
            if right is not None and line.text() not in existing:
                kept[name].append((right, line))
                existing.add(line.text())
        print(f"[{number}/{len(needed)}] {name}: {len(kept[name])} lines", file=sys.stderr)

    rank = {"code": 0, "rtti": 1, "string": 2, "ref": 3, "field": 4}
    output = header or ["# See scripts/gen_address_patterns.py."]
    missing, unanchored, anchored = [], [], []
    for name, lines in kept.items():
        lines.sort(key=lambda pair: (-pair[0], rank[pair[1].kind], len(pair[1].values)))
        if not lines:
            (unanchored if name in fields else missing).append(name)
        elif name in fields:
            anchored.append(name)
        # A line for every build that has one first, then the best others.
        chosen = []
        for image in images:
            if not any(right_on(image, line) for line in chosen):
                chosen += [line for _, line in lines if right_on(image, line)][:1]
        # A field's next line is from another function on the newest build.
        for _, line in (lines if name in fields else ()):
            if len(chosen) >= args.want or len(spread(images[0], chosen)) >= 2:
                break
            if line not in chosen and right_on(images[0], line) and place(images[0], line) not in spread(
                    images[0], chosen):
                chosen.append(line)
        chosen += [line for _, line in lines if line not in chosen][:max(0, args.want - len(chosen))]
        kept[name] = [(0, line) for line in chosen]
        order = [line for _, line in lines]
        output += [line.text() for line in sorted(chosen, key=order.index)]
    with open(PATTERNS_PATH, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(output) + "\n")
    print(f"Wrote {PATTERNS_PATH}: {sum(min(len(v), args.want) for v in kept.values())} lines.")
    if missing:
        print("No line found for: " + ", ".join(missing))
    for name in kept:
        for image in uncovered(name) if kept[name] else ():
            print(f"No line finds {name} on {os.path.basename(image.path)}")
    if args.candidates:
        print(f"Fields with lines ({len(anchored)}): " + ", ".join(anchored))
    single = [name for name in anchored if one_place(name)]
    if single:
        print(f"Fields read in one function only on {os.path.basename(images[0].path)} ({len(single)}): " +
              ", ".join(single))
    vias = read_vias()
    via = [name for name in unanchored if name in vias]
    unchecked = [name for name in unanchored if name not in vias]
    if via:
        print(f"Fields without a line, checked through a GAME_FIELD_VIA witness ({len(via)}): " +
              ", ".join(f"{name} ({vias[name]})" for name in via))
    if unchecked:
        print(f"Fields without a line, left unchecked ({len(unchecked)}): " + ", ".join(unchecked))
    declared = [name for name in unanchored if name in read_fields(False)]
    return 0 if not missing and not declared else 1


if __name__ == "__main__":
    sys.exit(main())
