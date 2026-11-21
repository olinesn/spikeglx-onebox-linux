"""Send SpikeGLX command-server commands (one per argument) and print the replies.
Usage: python3 -I sglx_cmd.py GETVERSION ISRUNNING ...
SGLX_TIMEOUT=<seconds> (default 10) is how long to wait for a reply; SELECTDEVS (Detect) can
take a few minutes under Box64."""
import os, socket, sys
s = socket.create_connection(("127.0.0.1", 4142), timeout=float(os.environ.get("SGLX_TIMEOUT", "10")))
f = s.makefile("rwb")
for cmd in sys.argv[1:]:
    f.write((cmd + "\n").encode()); f.flush()
    lines = []
    while True:
        line = f.readline().decode(errors="replace").rstrip("\n")
        if not line: break
        lines.append(line)
        if line == "OK" or line.startswith("ERROR"): break
    print(cmd, "->", " | ".join(lines))
