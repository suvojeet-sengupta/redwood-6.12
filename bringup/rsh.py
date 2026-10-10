#!/usr/bin/env python3
# Run commands on the redwood 6.12 bring-up shell over USB networking.
import socket, sys, time
IF = sys.argv[1]
cmd = sys.argv[2]
timeout = float(sys.argv[3]) if len(sys.argv) > 3 else 15
s = None
for addr in [("fe80::ff:fe61:2001%" + IF, 2323, 0, socket.if_nametoindex(IF)), ("192.168.90.1", 2323)]:
    try:
        fam = socket.AF_INET6 if ":" in addr[0] else socket.AF_INET
        s = socket.socket(fam, socket.SOCK_STREAM)
        s.settimeout(5)
        s.connect(addr)
        break
    except OSError as e:
        print("connect", addr[0], e, file=sys.stderr)
        s = None
if not s:
    sys.exit(1)
marker = "__END_%d__" % int(time.time())
s.sendall((cmd + "\necho " + marker + "\n").encode())
buf = b""
s.settimeout(timeout)
end = time.time() + timeout
while time.time() < end:
    try:
        d = s.recv(65536)
    except socket.timeout:
        break
    if not d:
        break
    buf += d
    if marker.encode() + b"\n" in buf:
        break
s.close()
out = buf.decode(errors="replace").replace("redwood612# ", "")
print(out.split(marker)[0].rstrip())
