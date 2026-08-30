#!/usr/bin/env python3
"""Assemble the vita-manifest JSON document from a stream of fact records.

Each input line is one JSON object with:
  {"path": ["a", "b", "c"], "op": "set", "value": <any JSON value>}
or
  {"path": ["a", "b"], "op": "append", "value": <any JSON value>}

"set" assigns doc[a][b][c] = value (creating intermediate dicts as needed).
"append" treats doc[a][b] as a list and appends value to it.

This keeps the JSON-shape decisions in one small, testable place instead of
hand-building quoted JSON inside the orchestrating shell script.
"""
import json
import sys


def get_container(doc, path):
    node = doc
    for key in path[:-1]:
        node = node.setdefault(key, {})
    return node


def main():
    doc = {}
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        rec = json.loads(line)
        path = rec["path"]
        op = rec.get("op", "set")
        value = rec["value"]
        if op == "set":
            container = get_container(doc, path)
            container[path[-1]] = value
        elif op == "append":
            container = get_container(doc, path)
            container.setdefault(path[-1], [])
            container[path[-1]].append(value)
        else:
            raise SystemExit("vita-manifest-render: unknown op: %s" % op)
    json.dump(doc, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
