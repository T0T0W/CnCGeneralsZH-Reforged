"""Read, dump and rewrite the skirmish AI's script file, Data/Scripts/SkirmishScripts.scb.

The file is EA's chunk format (DataChunk.cpp): a "CkMp" table of names, then nested chunks of
id, version and size. Script actions and conditions name their template through that table, so a
dump reads without the engine. WorldBuilder is the only other writer and it is not built here.

usage: scbtool.py dump <file.scb> [<substring>]    every script, its conditions and actions
       scbtool.py check <file.scb>                  decode and encode again, compare byte for byte

Editing is done from Python: load(), change the tree, save(). A patch script imports this module.
"""
import struct
import sys

MAGIC = b"CkMp"
CHUNK_HEADER = struct.Struct("<IHi")
COORD3D = 16
DICT_BOOL, DICT_INT, DICT_REAL, DICT_ASCII, DICT_UNICODE = range(5)
CONTAINERS = {"PlayerScriptsList", "ScriptList", "OrCondition"}
PARAMETER_TYPE_NAMES = [
    "INT", "REAL", "SCRIPT", "TEAM", "COUNTER", "FLAG", "COMPARISON", "WAYPOINT", "BOOLEAN",
    "TRIGGER_AREA", "TEXT_STRING", "SIDE", "SOUND", "SCRIPT_SUBROUTINE", "UNIT", "OBJECT_TYPE",
    "COORD3D", "ANGLE", "TEAM_STATE", "RELATION", "AI_MOOD", "DIALOG", "MUSIC", "MOVIE",
    "WAYPOINT_PATH", "LOCALIZED_TEXT", "BRIDGE", "KIND_OF_PARAM", "ATTACK_PRIORITY_SET",
    "RADAR_EVENT_TYPE", "SPECIAL_POWER", "SCIENCE", "UPGRADE", "COMMANDBUTTON_ABILITY", "BOUNDARY",
    "BUILDABLE", "SURFACES_ALLOWED", "SHAKE_INTENSITY", "COMMAND_BUTTON", "FONT_NAME",
    "OBJECT_STATUS", "COMMANDBUTTON_ALL_ABILITIES", "SKIRMISH_WAYPOINT_PATH", "COLOR", "EMOTICON",
    "OBJECT_PANEL_FLAG", "FACTION_NAME", "OBJECT_TYPE_LIST", "REVEALNAME", "SCIENCE_AVAILABILITY",
    "LEFT_OR_RIGHT", "PERCENT",
]


class Chunk:
    def __init__(self, name, version, fields, children):
        self.name = name
        self.version = version
        self.fields = fields
        self.children = children


class Reader:
    def __init__(self, data, names):
        self.data = data
        self.position = 0
        self.names = names

    def take(self, fmt):
        values = struct.unpack_from(fmt, self.data, self.position)
        self.position += struct.calcsize(fmt)
        return values[0]

    def string(self):
        length = self.take("<H")
        text = self.data[self.position:self.position + length].decode("latin-1")
        self.position += length
        return text

    def unicode(self):
        length = self.take("<H")
        text = self.data[self.position:self.position + 2 * length].decode("utf-16-le")
        self.position += 2 * length
        return text

    def name_key(self):
        return self.names[self.take("<i") >> 8]

    def dict(self):
        entries = []
        for _ in range(self.take("<H")):
            key_and_type = self.take("<i")
            kind, key = key_and_type & 0xFF, self.names[key_and_type >> 8]
            readers = {DICT_BOOL: lambda: self.take("<B"), DICT_INT: lambda: self.take("<i"),
                       DICT_REAL: lambda: self.take("<f"), DICT_ASCII: self.string, DICT_UNICODE: self.unicode}
            entries.append([key, kind, readers[kind]()])
        return entries

    def parameter(self):
        kind = self.take("<i")
        if kind == COORD3D:
            return [kind, (self.take("<f"), self.take("<f"), self.take("<f"))]
        return [kind, self.take("<i"), self.take("<f"), self.string()]

    def chunk(self):
        chunk_id, version, size = CHUNK_HEADER.unpack_from(self.data, self.position)
        self.position += CHUNK_HEADER.size
        end = self.position + size
        name = self.names[chunk_id]
        fields = []
        if name == "ScriptGroup":
            fields = [self.string(), self.take("<B")] + ([self.take("<B")] if version >= 2 else [])
        elif name == "Script":
            fields = [self.string() for _ in range(4)] + [self.take("<B") for _ in range(6)]
            fields += [self.take("<i")] if version >= 2 else []
        elif name == "Condition":
            fields = [self.take("<i"), self.name_key() if version >= 4 else None]
            fields.append([self.parameter() for _ in range(self.take("<i"))])
        elif name in ("ScriptAction", "ScriptActionFalse"):
            fields = [self.take("<i"), self.name_key() if version >= 2 else None]
            fields.append([self.parameter() for _ in range(self.take("<i"))])
        elif name == "ScriptsPlayers":
            has_dicts = self.take("<i") if version >= 2 else 0
            count = self.take("<i")
            fields = [has_dicts, [[self.string(), self.dict() if has_dicts else None] for _ in range(count)]]
        elif name == "ScriptTeams":
            while self.position < end:
                fields.append(self.dict())
        elif name not in CONTAINERS:
            fields = [self.data[self.position:end]]
            self.position = end
        children = []
        while self.position < end:
            children.append(self.chunk())
        if self.position != end:
            raise ValueError(f"{name} chunk read {self.position - end} bytes past its end")
        return Chunk(name, version, fields, children)


class Writer:
    def __init__(self, table):
        self.table = list(table)
        self.ids = dict(table)
        self.parts = []

    def name_index(self, name):
        # DataChunkOutput writes the newest name first with the next id up, so a name the file did
        # not have goes to the front.
        if name not in self.ids:
            self.ids[name] = max(self.ids.values(), default=0) + 1
            self.table.insert(0, (name, self.ids[name]))
        return self.ids[name]

    def put(self, fmt, value):
        self.parts.append(struct.pack(fmt, value))

    def string(self, text):
        encoded = text.encode("latin-1")
        self.put("<H", len(encoded))
        self.parts.append(encoded)

    def unicode(self, text):
        self.put("<H", len(text))
        self.parts.append(text.encode("utf-16-le"))

    def name_key(self, name):
        self.put("<i", (self.name_index(name) << 8) | DICT_ASCII)

    def dict(self, entries):
        self.put("<H", len(entries))
        for key, kind, value in entries:
            self.put("<i", (self.name_index(key) << 8) | kind)
            writers = {DICT_BOOL: lambda v: self.put("<B", v), DICT_INT: lambda v: self.put("<i", v),
                       DICT_REAL: lambda v: self.put("<f", v), DICT_ASCII: self.string, DICT_UNICODE: self.unicode}
            writers[kind](value)

    def parameter(self, parameter):
        self.put("<i", parameter[0])
        if parameter[0] == COORD3D:
            for coordinate in parameter[1]:
                self.put("<f", coordinate)
            return
        self.put("<i", parameter[1])
        self.put("<f", parameter[2])
        self.string(parameter[3])

    def chunk(self, chunk):
        outer_parts, self.parts = self.parts, []
        fields = chunk.fields
        if chunk.name == "ScriptGroup":
            self.string(fields[0])
            for flag in fields[1:]:
                self.put("<B", flag)
        elif chunk.name == "Script":
            for text in fields[:4]:
                self.string(text)
            for flag in fields[4:10]:
                self.put("<B", flag)
            if chunk.version >= 2:
                self.put("<i", fields[10])
        elif chunk.name in ("Condition", "ScriptAction", "ScriptActionFalse"):
            self.put("<i", fields[0])
            if fields[1] is not None:
                self.name_key(fields[1])
            self.put("<i", len(fields[2]))
            for parameter in fields[2]:
                self.parameter(parameter)
        elif chunk.name == "ScriptsPlayers":
            if chunk.version >= 2:
                self.put("<i", fields[0])
            self.put("<i", len(fields[1]))
            for player_name, side_dict in fields[1]:
                self.string(player_name)
                if fields[0]:
                    self.dict(side_dict)
        elif chunk.name == "ScriptTeams":
            for team in fields:
                self.dict(team)
        elif chunk.name not in CONTAINERS:
            self.parts.append(fields[0])
        for child in chunk.children:
            self.chunk(child)
        body = b"".join(self.parts)
        self.parts = outer_parts
        self.parts += [CHUNK_HEADER.pack(self.name_index(chunk.name), chunk.version, len(body)), body]


def load(path):
    with open(path, "rb") as file:
        data = file.read()
    if data[:4] != MAGIC:
        raise ValueError(f"{path} does not start with {MAGIC!r}")
    count = struct.unpack_from("<i", data, 4)[0]
    position = 8
    table = []
    for _ in range(count):
        length = data[position]
        name = data[position + 1:position + 1 + length].decode("latin-1")
        position += 1 + length
        table.append((name, struct.unpack_from("<I", data, position)[0]))
        position += 4
    reader = Reader(data, {name_id: name for name, name_id in table})
    reader.position = position
    chunks = []
    while reader.position < len(data):
        chunks.append(reader.chunk())
    return table, chunks


def encode(table, chunks):
    writer = Writer(table)
    for chunk in chunks:
        writer.chunk(chunk)
    header = [MAGIC, struct.pack("<i", len(writer.table))]
    for name, name_id in writer.table:
        encoded = name.encode("latin-1")
        header += [bytes([len(encoded)]), encoded, struct.pack("<I", name_id)]
    return b"".join(header + writer.parts)


def save(path, table, chunks):
    with open(path, "wb") as file:
        file.write(encode(table, chunks))


def walk_scripts(chunks, group=""):
    for chunk in chunks:
        if chunk.name == "Script":
            yield group, chunk
        else:
            yield from walk_scripts(chunk.children, chunk.fields[0] if chunk.name == "ScriptGroup" else group)


def describe_parameter(parameter):
    kind = PARAMETER_TYPE_NAMES[parameter[0]]
    if parameter[0] == COORD3D:
        return f"{kind}{parameter[1]}"
    value = parameter[3] if parameter[3] else (parameter[2] if parameter[0] in (1, 17, 51) else parameter[1])
    return f"{kind}={value}"


def describe_call(chunk):
    arguments = ", ".join(describe_parameter(parameter) for parameter in chunk.fields[2])
    return f"{chunk.fields[1]}({arguments})"


def dump(chunks, needle):
    for chunk in chunks:
        if chunk.name == "ScriptsPlayers":
            print("players:", [player for player, _ in chunk.fields[1]])
    player_lists = [chunk for chunk in chunks if chunk.name == "PlayerScriptsList"]
    for list_chunk in player_lists:
        for index, script_list in enumerate(list_chunk.children):
            for group, script in walk_scripts(script_list.children):
                name, flags = script.fields[0], script.fields[4:10]
                text = [describe_call(child) for child in script.children if child.name != "OrCondition"]
                conditions = [" AND ".join(describe_call(c) for c in orc.children)
                              for orc in script.children if orc.name == "OrCondition"]
                block = f"[{index}] {group} / {name}  active={flags[0]} once={flags[1]} " \
                        f"e/n/h={flags[2]}{flags[3]}{flags[4]} sub={flags[5]} delay={script.fields[10] if len(script.fields) > 10 else 0}"
                body = [f"    IF {' OR '.join(conditions) or 'true'}"]
                body += [f"    {'ELSE' if child.name == 'ScriptActionFalse' else 'DO'} {call}"
                         for child, call in zip([c for c in script.children if c.name != "OrCondition"], text)]
                full = "\n".join([block] + body)
                if needle is None or needle.lower() in full.lower():
                    print(full)


def main():
    command, path = sys.argv[1], sys.argv[2]
    table, chunks = load(path)
    if command == "dump":
        dump(chunks, sys.argv[3] if len(sys.argv) > 3 else None)
    elif command == "check":
        with open(path, "rb") as file:
            original = file.read()
        rewritten = encode(table, chunks)
        if rewritten != original:
            first = next(i for i, (a, b) in enumerate(zip(original, rewritten)) if a != b)
            raise SystemExit(f"round trip differs at byte {first} ({len(original)} -> {len(rewritten)} bytes)")
        print(f"round trip identical, {len(original)} bytes")
    else:
        raise SystemExit(f"unknown command {command!r}; expected dump or check")


if __name__ == "__main__":
    main()
