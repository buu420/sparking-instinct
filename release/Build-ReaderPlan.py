"""Generate a local reconstruction plan from the upstream and tested reader."""

import argparse
from collections import defaultdict
from difflib import SequenceMatcher
from hashlib import sha256
import json
from pathlib import Path
from zipfile import ZipFile

COMMIT = "527125583d5d7ef36890c1c006ed187923e7e923"
ARCHIVE_SHA256 = "81320e984ab4f1bbdcb301d296da829a2fe0705ea7dd99221eab848331faa21f"
UPSTREAM_ROOT = f"SparkingZeroAccess-{COMMIT}/SparkingZeroAccess/"
TESTED_ROOT = "Payload/Mods/sparkingzero.accessibility.reloaded/Scripts/"


def reader_lines(data):
    text = data.decode("utf-8").replace("\r\n", "\n")
    if not text.endswith("\n") or "\r" in text:
        raise ValueError("Unexpected reader line endings")
    return text[:-1].split("\n")


def crlf_ranges(data):
    ranges = []
    for index, line in enumerate(data.split(b"\n")[:-1]):
        if not line.endswith(b"\r"):
            continue
        if ranges and ranges[-1]["start"] + ranges[-1]["count"] == index:
            ranges[-1]["count"] += 1
        else:
            ranges.append({"start": index, "count": 1})
    return ranges


def additions_for(lines, exact, content):
    for line in lines:
        if line.strip() and line in exact:
            path, number = exact[line]
            yield {"from": path, "line": number}
        elif line.strip() and line.lstrip() in content:
            path, number = content[line.lstrip()]
            yield {
                "from": path,
                "line": number,
                "indent": line[:len(line) - len(line.lstrip())],
            }
        else:
            yield line


def build(upstream_path, tested_path, output):
    if sha256(upstream_path.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise ValueError("Wrong AccessForge source archive")
    with ZipFile(upstream_path) as upstream, ZipFile(tested_path) as tested:
        paths = sorted(
            item.removeprefix(TESTED_ROOT)
            for item in tested.namelist()
            if item.startswith(TESTED_ROOT)
            and (item.endswith((".lua", ".wav")) or item.endswith("/cue-sheet.json"))
        )
        counts = defaultdict(int)
        for path in paths:
            counts[Path(path).suffix] += 1
        if dict(counts) != {".json": 1, ".wav": 37, ".lua": 24}:
            raise ValueError(f"Unexpected tested reader contents: {dict(counts)}")
        base_files = {path: upstream.read(UPSTREAM_ROOT + path) for path in paths}
        base_lines = {p: reader_lines(b) for p, b in base_files.items() if p.endswith((".lua", ".json"))}
        exact = {}
        content = {}
        for path, lines in base_lines.items():
            for number, line in enumerate(lines):
                exact.setdefault(line, (path, number))
                content.setdefault(line.lstrip(), (path, number))
        records = []
        for path in paths:
            base = base_files[path]
            target = tested.read(TESTED_ROOT + path)
            record = {
                "path": path,
                "original": sha256(base).hexdigest().upper(),
                "patched": sha256(target).hexdigest().upper(),
            }
            if base != target:
                if path not in base_lines:
                    raise ValueError(f"Upstream asset differs from the tested package: {path}")
                after = reader_lines(target)
                edits = []
                for tag, start, end, new_start, new_end in SequenceMatcher(
                    None, base_lines[path], after, autojunk=False
                ).get_opcodes():
                    if tag != "equal":
                        edits.append({
                            "at": start,
                            "remove": end - start,
                            "add": list(additions_for(after[new_start:new_end], exact, content)),
                        })
                record["edits"] = edits
                record["crlf"] = crlf_ranges(target)
            records.append(record)
    output.write_text(
        json.dumps({"commit": COMMIT, "files": records}, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("upstream", type=Path)
    parser.add_argument("tested", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build(args.upstream, args.tested, args.output)
