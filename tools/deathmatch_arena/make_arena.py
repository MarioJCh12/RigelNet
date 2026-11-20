"""Generate a deathmatch arena level file (Duke Nukem II .MNI format).

Uses the tiles of tileset CZONE1.MNI (the one used by L1), in the same
patterns L1 uses for its start room: girder floor/ceiling, metal pillars
as walls, and the grey machinery panels as background.

Usage: make_arena.py <output.MNI>

The result can be dropped into the game directory as a loose file. RigelEngine
prefers loose files over the ones in NUKEM2.CMP. Naming it M1.MNI adds it as an
extra map when using the shareware version (which only has L1-L8), without
replacing any original map. Then: RigelEngine --deathmatch -l M1 <game path>
"""

import struct
import sys

WIDTH = 64
MAP_DATA_WORDS = 32750
HEIGHT = MAP_DATA_WORDS // WIDTH  # 511, fixed by the format

BLACK = 320

# Patterns taken from L1's start room
CEILING_DECOR = [[494, 498, 493], [534, 538, 533], [574, 663, 573]]
GIRDER = [702, 703, 704]  # solid
SHADE_TOP = [[956, 957, 958, 959], [916, 917, 918, 919]]
BACKGROUND = [
    [876, 877, 878, 879],
    [916, 917, 918, 919],
    [878, 879, 876, 877],
    [918, 919, 916, 917],
]
FLOOR = [
    [660, 661, 662],  # solid
    [497, 701, 496],
    [537, 495, 536],
    [577, 535, 576],
    [BLACK, 575, BLACK],
]
PILLAR = [460, 500, 540, 580, 459, 499, 539, 579]  # solid

# Vertical layout
CEILING_ROW = 3  # solid
FLOOR_ROW = 38  # solid, players stand at row 37
LEFT_WALL = (0, 1)
RIGHT_WALL = (WIDTH - 2, WIDTH - 1)

# Platforms: (solid row, first x, last x). Tiers are 6 rows apart, Duke
# can jump 7 rows high.
PLATFORMS = [
    (32, 7, 18), (32, 45, 56),
    (26, 2, 6), (26, 24, 39), (26, 57, 61),
    (20, 9, 20), (20, 43, 54),
    (14, 2, 8), (14, 26, 37), (14, 55, 61),
]

# Actor IDs (see src/data/actor_ids.hpp)
DUKE_RIGHT = 6
ROCKET_LAUNCHER = 19
FLAME_THROWER = 20
LASER = 23
HEALTH_MOLECULE = 28
RAPID_FIRE = 53
COLA = 168
SIX_PACK = 174
TURKEY = 201

# (actor id, x, y). y is the row the actor stands on top of minus one,
# i.e. the bottom row of the actor - same as the player position.
ACTORS = [
    (DUKE_RIGHT, 4, FLOOR_ROW - 1),
    # Floor
    (COLA, 14, FLOOR_ROW - 1),
    (LASER, 31, FLOOR_ROW - 1),
    (COLA, 48, FLOOR_ROW - 1),
    (TURKEY, 58, FLOOR_ROW - 1),
    # Tier 32
    (FLAME_THROWER, 12, 31),
    (FLAME_THROWER, 50, 31),
    # Tier 26
    (TURKEY, 3, 25),
    (RAPID_FIRE, 31, 25),
    (TURKEY, 59, 25),
    # Tier 20
    (HEALTH_MOLECULE, 14, 19),
    (HEALTH_MOLECULE, 48, 19),
    # Tier 14
    (SIX_PACK, 4, 13),
    (ROCKET_LAUNCHER, 31, 13),
    (SIX_PACK, 57, 13),
]


def build_map():
    tiles = [[BLACK] * WIDTH for _ in range(HEIGHT)]

    for y in range(0, FLOOR_ROW + len(FLOOR)):
        for x in range(WIDTH):
            if y < CEILING_ROW:
                tile = CEILING_DECOR[y][x % 3]
            elif y == CEILING_ROW:
                tile = GIRDER[x % 3]
            elif y < CEILING_ROW + 1 + len(SHADE_TOP):
                tile = SHADE_TOP[y - CEILING_ROW - 1][x % 4]
            elif y < FLOOR_ROW:
                tile = BACKGROUND[y % 4][x % 4]
            else:
                tile = FLOOR[y - FLOOR_ROW][x % 3]
            tiles[y][x] = tile

    # Walls go all the way from top to bottom of the arena
    for y in range(0, FLOOR_ROW + len(FLOOR)):
        for x in LEFT_WALL + RIGHT_WALL:
            tiles[y][x] = PILLAR[y % len(PILLAR)]

    for row, x0, x1 in PLATFORMS:
        for x in range(x0, x1 + 1):
            tiles[row][x] = GIRDER[x % 3]

    return tiles


def fixed_string(text):
    raw = text.encode("ascii")
    assert len(raw) <= 12
    return raw.ljust(12, b" ") + b"\0"


def write_level(path, tiles):
    actor_words = []
    for actor_id, x, y in ACTORS:
        actor_words += [actor_id, x, y]

    header_size = 2 + 13 * 3 + 1 + 1 + 2 + 2
    data_offset = header_size + len(actor_words) * 2

    out = bytearray()
    out += struct.pack("<H", data_offset)
    out += fixed_string("czone1.mni")
    out += fixed_string("drop24.mni")
    out += fixed_string("kickbuta.imf")
    out += struct.pack("<BBHH", 0x02, 0, 0, len(actor_words))
    out += struct.pack("<%dH" % len(actor_words), *actor_words)

    out += struct.pack("<H", WIDTH)
    words = []
    for row in tiles:
        for tile in row:
            assert 0 <= tile < 1000, "only solid layer tiles are used"
            words.append(tile * 8)
    words += [0] * (MAP_DATA_WORDS - len(words))
    out += struct.pack("<%dH" % len(words), *words)

    # Extra bits for masked tiles: all zero, RLE compressed.
    # 2 bits per tile -> one byte per 4 tiles.
    needed_bytes = (MAP_DATA_WORDS + 3) // 4
    rle = bytearray()
    remaining = needed_bytes
    while remaining > 0:
        run = min(127, remaining)
        rle += struct.pack("<bB", run, 0)
        remaining -= run
    rle += b"\0"
    out += struct.pack("<H", len(rle))
    out += rle

    with open(path, "wb") as f:
        f.write(out)

    return len(out)


def main():
    tiles = build_map()
    size = write_level(sys.argv[1], tiles)
    print("Wrote %s (%d bytes), %dx%d" % (sys.argv[1], size, WIDTH, HEIGHT))


if __name__ == "__main__":
    main()
