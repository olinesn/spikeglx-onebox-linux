"""Send SpikeGLX command-server commands (one per argument) and print the replies.
Usage: python3 -I sglx_cmd.py GETVERSION ISRUNNING ..."""
import socket, sys
s = socket.create_connection(("127.0.0.1", 4142), timeout=10)
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
