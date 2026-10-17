"""Read-only PE inventory for the remaining native excavation backend.

Prints class-name, complete-object-locator and vtable RVAs, not executable bytes.
No process is opened and no game files are changed. An RTTI match is a research
lead, not evidence that calling or mutating the object is safe.
"""
import argparse
import hashlib
import struct


class Image:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        if self.data[:2] != b"MZ":
            raise ValueError("not a PE image")
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("missing PE header")
        count = struct.unpack_from("<H", self.data, pe + 6)[0]
        self.timestamp = struct.unpack_from("<I", self.data, pe + 8)[0]
        optional_size = struct.unpack_from("<H", self.data, pe + 20)[0]
        optional = pe + 24
        if struct.unpack_from("<H", self.data, optional)[0] != 0x20B:
            raise ValueError("expected PE32+")
        self.base = struct.unpack_from("<Q", self.data, optional + 24)[0]
        self.size = struct.unpack_from("<I", self.data, optional + 56)[0]
        self.sections = []
        for i in range(count):
            offset = optional + optional_size + i * 40
            _, rva, size, raw = struct.unpack_from("<IIII", self.data, offset + 8)
            flags = struct.unpack_from("<I", self.data, offset + 36)[0]
            if raw + size > len(self.data):
                raise ValueError("section outside image")
            self.sections.append((raw, size, rva, flags))

    def rva(self, offset):
        for raw, size, rva, _ in self.sections:
            if raw <= offset < raw + size:
                return rva + offset - raw
        return None

    def executable(self, address):
        return any(self.base + rva <= address < self.base + rva + size and flags & 0x20000000
                   for _, size, rva, flags in self.sections)

    def matches(self, pattern):
        start = 0
        while True:
            found = self.data.find(pattern, start)
            if found < 0:
                return
            yield found
            start = found + 1

    def describe(self, name):
        encoded = name.encode("ascii") + b"\0"
        for offset in self.matches(encoded):
            name_rva = self.rva(offset)
            print("name RVA=0x{:08x} {}".format(name_rva, name))
            if not name.startswith(".?AV") or offset < 16:
                continue
            descriptor = self.rva(offset - 16)
            if descriptor is None:
                continue
            for reference in self.matches(struct.pack("<I", descriptor)):
                locator = reference - 12
                if locator < 0 or locator + 24 > len(self.data):
                    continue
                signature, displacement, _, _, _, self_rva = struct.unpack_from("<6I", self.data, locator)
                if signature != 1 or self.rva(locator) != self_rva:
                    continue
                for pointer in self.matches(struct.pack("<Q", self.base + self_rva)):
                    table = pointer + 8
                    if table + 8 > len(self.data):
                        continue
                    first = struct.unpack_from("<Q", self.data, table)[0]
                    table_rva = self.rva(table)
                    if table_rva is None or not self.executable(first):
                        continue
                    print("  COL=0x{:08x} objectOffset=0x{:x} vtable=0x{:08x} firstMethod=0x{:08x}".format(
                        self_rva, displacement, table_rva, first - self.base))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", help="path to the installed witcher3.exe")
    args = parser.parse_args()
    image = Image(args.executable)
    print("timestamp=0x{:08x} imageSize=0x{:08x} preferredBase=0x{:x}".format(
        image.timestamp, image.size, image.base))
    print("sha256=" + hashlib.sha256(image.data).hexdigest())
    for name in (".?AVCTerrainTile@@", ".?AVCRenderProxy_Terrain@@", ".?AVCRenderTerrainUpdateData@@",
                 ".?AVCRenderMesh@@", ".?AVHeightField@Gu@physx@@",
                 ".?AVCRenderCommand_UpdateTerrainTileHeightRanges@@",
                 "MC_TerrainHoles", "SHOW_TerrainHoles"):
        image.describe(name)


if __name__ == "__main__":
    main()
