#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
SOURCE="$ROOT/toolkit/share/vita-fbserve.c"
TOOL="$ROOT/toolkit/bin/vita-fbserve"
TMP=$(mktemp -d)
PORT=$((20000 + ($$ % 20000)))
SERVER_PID=
trap 'if [ -n "$SERVER_PID" ]; then kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; fi; rm -rf "$TMP"' 0 HUP INT TERM

[ -f "$SOURCE" ] || { printf 'FAIL: missing %s\n' "$SOURCE" >&2; exit 1; }
[ -x "$TOOL" ] || { printf 'FAIL: missing executable %s\n' "$TOOL" >&2; exit 1; }

python3 - "$TMP/fb" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
with path.open('wb') as f:
    for y in range(720):
        row = bytearray()
        for x in range(1280):
            row += bytes((x & 255, y & 255, (x + y) & 255, 255))
        f.write(row)
PY
before=$(sha256sum "$TMP/fb" | awk '{print $1}')
cc -std=c11 -Wall -Wextra -Werror "$SOURCE" -o "$TMP/vita-fbserve"

"$TMP/vita-fbserve" --device "$TMP/fb" --port "$PORT" --machine >"$TMP/server.log" 2>&1 &
SERVER_PID=$!

attempts=10
while [ "$attempts" -gt 0 ]; do
    if python3 - "$PORT" <<'PY'
import socket
import sys
try:
    with socket.create_connection(('127.0.0.1', int(sys.argv[1])), timeout=0.2):
        pass
except OSError:
    raise SystemExit(1)
PY
    then
        break
    fi
    sleep 0.1
    attempts=$((attempts - 1))
done

python3 - "$PORT" "$TMP/frame.response" <<'PY'
import socket
import sys
port = int(sys.argv[1])
out = sys.argv[2]
request = b'GET /frame.bmp HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n'
with socket.create_connection(('127.0.0.1', port), timeout=2) as sock:
    sock.sendall(request)
    chunks = []
    while True:
        data = sock.recv(65536)
        if not data:
            break
        chunks.append(data)
open(out, 'wb').write(b''.join(chunks))
PY

python3 - "$TMP/frame.response" <<'PY'
from pathlib import Path
import struct
import sys
raw = Path(sys.argv[1]).read_bytes()
head, body = raw.split(b'\r\n\r\n', 1)
assert b'HTTP/1.0 200 OK' in head, head
assert b'Content-Type: image/bmp' in head, head
assert len(body) == 54 + 320 * 3 * 180
assert body[:2] == b'BM'
assert struct.unpack_from('<I', body, 2)[0] == 54 + 320 * 3 * 180
assert struct.unpack_from('<I', body, 18)[0] == 320
assert struct.unpack_from('<i', body, 22)[0] == 180
# BMP is bottom-up, and the 4x reduction samples source (20, 28).
offset = 54 + (179 - 7) * 960 + 5 * 3
assert body[offset:offset + 3] == bytes((48, 28, 20)), body[offset:offset + 3]
PY

after=$(sha256sum "$TMP/fb" | awk '{print $1}')
[ "$before" = "$after" ]
grep -F 'schema=1' "$TMP/server.log" >/dev/null
grep -F 'bind=127.0.0.1' "$TMP/server.log" >/dev/null
grep -F 'scale=4' "$TMP/server.log" >/dev/null

python3 - "$PORT" <<'PY'
import socket
import sys
for request, expected in (
    (b'GET /health HTTP/1.0\r\nHost: localhost\r\n\r\n', b'HTTP/1.0 200 OK'),
    (b'POST /frame.bmp HTTP/1.0\r\nHost: localhost\r\n\r\n', b'HTTP/1.0 405 Method Not Allowed'),
):
    with socket.create_connection(('127.0.0.1', int(sys.argv[1])), timeout=2) as sock:
        sock.sendall(request)
        data = b''
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
    assert expected in data, (expected, data[:200])
PY

fake="$TMP/fake"
# shellcheck disable=SC2016
printf '%s\n' '#!/bin/sh' \
    'printf "%s\\n" "$*" > "$VITA_FBSERVE_TEST_LOG"' \
    'printf "wrapper_ok\\n"' > "$fake"
chmod +x "$fake"
wrapper_out=$(VITA_FBSERVE_BINARY="$fake" VITA_FBSERVE_TEST_LOG="$TMP/wrapper.log" "$TOOL" --bind 127.0.0.1 --port 12345)
printf '%s\n' "$wrapper_out" | grep -F 'wrapper_ok' >/dev/null
grep -F -- '--bind 127.0.0.1 --port 12345' "$TMP/wrapper.log" >/dev/null

kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=
printf 'vita-fbserve test passed\n'
