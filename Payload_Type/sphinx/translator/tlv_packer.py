import struct

from .utils import UUID_LEN


class BinPacker:
    def __init__(self):
        self.buffer = bytearray()

    def pack_byte(self, value: int) -> None:
        self.buffer.append(value & 0xFF)

    def pack_uint16(self, value: int) -> None:
        self.buffer.extend(struct.pack(">H", value))

    def pack_uint32(self, value: int) -> None:
        self.buffer.extend(struct.pack(">I", value))

    def pack_uint64(self, value: int) -> None:
        self.buffer.extend(struct.pack(">Q", value))

    def pack_bytes(self, data: bytes) -> None:
        self.pack_uint32(len(data))
        self.buffer.extend(data)

    def pack_string(self, s: str) -> None:
        encoded = s.encode("utf-8")
        self.pack_bytes(encoded)

    def get_bytes(self) -> bytes:
        return bytes(self.buffer)


class BinParser:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def get_byte(self) -> int:
        val = self.data[self.offset]
        self.offset += 1
        return val

    def get_uint32(self) -> int:
        val = struct.unpack_from(">I", self.data, self.offset)[0]
        self.offset += 4
        return val

    def get_uint64(self) -> int:
        val = struct.unpack_from(">Q", self.data, self.offset)[0]
        self.offset += 8
        return val

    def get_bytes(self, size: int = 0) -> bytes:
        if size == 0:
            size = self.get_uint32()
        val = self.data[self.offset:self.offset + size]
        self.offset += size
        return val

    def get_string(self, size: int = 0) -> str:
        b = self.get_bytes(size)
        return b.decode("utf-8", errors="replace")
