"""Reads and writes 8-bit PNG files for the ui-style-system scripts (standard library only)."""

import struct
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def chunks(data):
    position = len(SIGNATURE)
    while position + 8 <= len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        kind = data[position + 4:position + 8]
        yield kind, data[position + 8:position + 8 + length]
        if kind == b"IEND":
            return
        position += 12 + length


def unfilter(raw, width, height, channels):
    stride = width * channels
    if len(raw) < height * (stride + 1):
        raise ValueError("the image data is damaged")
    samples = bytearray(height * stride)
    previous = bytearray(stride)
    offset = 0
    for y in range(height):
        filter_type = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        if filter_type == 1:
            for x in range(channels, stride):
                line[x] = (line[x] + line[x - channels]) & 0xFF
        elif filter_type == 2:
            for x in range(stride):
                line[x] = (line[x] + previous[x]) & 0xFF
        elif filter_type == 3:
            for x in range(stride):
                left = line[x - channels] if x >= channels else 0
                line[x] = (line[x] + ((left + previous[x]) >> 1)) & 0xFF
        elif filter_type == 4:
            for x in range(stride):
                up = previous[x]
                left, up_left = (line[x - channels], previous[x - channels]) if x >= channels else (0, 0)
                estimate = left + up - up_left
                distance_left, distance_up, distance_up_left = abs(estimate - left), abs(estimate - up), abs(estimate - up_left)
                if distance_left <= distance_up and distance_left <= distance_up_left:
                    predictor = left
                elif distance_up <= distance_up_left:
                    predictor = up
                else:
                    predictor = up_left
                line[x] = (line[x] + predictor) & 0xFF
        elif filter_type != 0:
            raise ValueError("the image data is damaged")
        samples[y * stride:(y + 1) * stride] = line
        previous = line
    return samples


def read_png(path):
    """(width, height, RGBA pixels) of an 8-bit PNG file without interlacing."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data.startswith(b"\xff\xd8"):
        raise ValueError(f"{path} is a JPEG file; save it as PNG")
    if len(data) < 33 or not data.startswith(SIGNATURE) or data[12:16] != b"IHDR":
        raise ValueError(f"{path} is not a PNG file")
    width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
    if bit_depth != 8 or interlace != 0 or color_type not in CHANNELS:
        raise ValueError(f"{path} is a PNG with bit depth {bit_depth}, color type {color_type} and interlace {interlace}; "
                         "only 8-bit PNG files without interlacing are read")
    palette, transparency, compressed = b"", b"", []
    for kind, payload in chunks(data):
        if kind == b"PLTE":
            palette = payload
        elif kind == b"tRNS":
            transparency = payload
        elif kind == b"IDAT":
            compressed.append(payload)
    try:
        samples = unfilter(zlib.decompress(b"".join(compressed)), width, height, CHANNELS[color_type])
    except zlib.error:
        raise ValueError(f"the image data of {path} is damaged") from None

    count = width * height
    if color_type == 6:
        return width, height, samples
    rgba = bytearray(count * 4)
    rgba[3::4] = b"\xff" * count
    if color_type == 2:
        rgba[0::4], rgba[1::4], rgba[2::4] = samples[0::3], samples[1::3], samples[2::3]
        if len(transparency) == 6:
            key = bytes((transparency[1], transparency[3], transparency[5]))
            for index in range(count):
                if samples[index * 3:index * 3 + 3] == key:
                    rgba[index * 4 + 3] = 0
    elif color_type in (0, 4):
        gray = samples[0::CHANNELS[color_type]]
        rgba[0::4] = rgba[1::4] = rgba[2::4] = gray
        if color_type == 4:
            rgba[3::4] = samples[1::2]
        elif len(transparency) == 2:
            for index in range(count):
                if samples[index] == transparency[1]:
                    rgba[index * 4 + 3] = 0
    else:
        if not palette:
            raise ValueError(f"the palette PNG {path} has no palette")
        colors = []
        for index in range(256):
            rgb = palette[index * 3:index * 3 + 3] if index * 3 + 3 <= len(palette) else b"\x00\x00\x00"
            colors.append(bytes(rgb) + bytes((transparency[index] if index < len(transparency) else 255,)))
        rgba = bytearray(b"".join(colors[index] for index in samples))
    return width, height, rgba


def write_png(path, width, height, rgba):
    stride = width * 4
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    with open(path, "wb") as handle:
        handle.write(SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))
