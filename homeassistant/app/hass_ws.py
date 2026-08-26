"""A minimum-viable WebSocket client, for the one thing REST cannot do.

Registering a Lovelace resource is not a service call and is not in the REST
API - `lovelace/resources/create` exists only on the WebSocket API. Everything
else this add-on does goes over REST precisely to avoid this file; it is here
because "copy the card into /config/www and then go and paste a URL into a
settings page yourself" is a step that goes wrong.

RFC 6455, only as far as Home Assistant needs it: one text frame out, text
frames in, masked client-to-server, no extensions, no compression. Standard
library only.
"""

import base64
import json
import os
import socket
import struct
from urllib.parse import urlparse


class WSError(RuntimeError):
    pass


class HassWS:
    def __init__(self, url, token, timeout=10):
        self.url = url
        self.token = token
        self.timeout = timeout
        self.sock = None
        self._buf = b""
        self._id = 0

    # ---- framing ----------------------------------------------------------
    def _send(self, payload):
        data = payload.encode()
        header = bytearray([0x81])                 # FIN + text
        n = len(data)
        if n < 126:
            header.append(0x80 | n)                # client frames MUST be masked
        elif n < 1 << 16:
            header.append(0x80 | 126)
            header += struct.pack(">H", n)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", n)
        mask = os.urandom(4)
        header += mask
        self.sock.sendall(bytes(header) + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _read(self, n):
        while len(self._buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise WSError("connection closed")
            self._buf += chunk
        out, self._buf = self._buf[:n], self._buf[n:]
        return out

    def _recv(self):
        """One complete text message, reassembling fragments."""
        parts = []
        while True:
            b0, b1 = self._read(2)
            fin, opcode = b0 & 0x80, b0 & 0x0F
            masked, n = b1 & 0x80, b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._read(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._read(8))[0]
            mask = self._read(4) if masked else None
            data = self._read(n) if n else b""
            if mask:
                data = bytes(c ^ mask[i % 4] for i, c in enumerate(data))

            if opcode == 0x8:                       # close
                raise WSError("server closed the connection")
            if opcode == 0x9:                       # ping -> pong, same payload
                # The payload has to be MASKED like any other client frame.
                # Sending the key and then the plaintext leaves the server
                # unmasking garbage, which it reads as the next command.
                m = os.urandom(4)
                self.sock.sendall(
                    bytes([0x8A, 0x80 | len(data)]) + m
                    + bytes(c ^ m[i % 4] for i, c in enumerate(data)))
                continue
            if opcode == 0xA:                       # pong, ignore
                continue
            parts.append(data)
            if fin:
                return json.loads(b"".join(parts).decode())

    # ---- session ----------------------------------------------------------
    def __enter__(self):
        u = urlparse(self.url)
        host = u.hostname
        port = u.port or (443 if u.scheme == "wss" else 80)
        path = u.path or "/"
        self.sock = socket.create_connection((host, port), self.timeout)
        self.sock.settimeout(self.timeout)
        if u.scheme == "wss":
            import ssl
            self.sock = ssl.create_default_context().wrap_socket(
                self.sock, server_hostname=host)

        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(
            ("GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n"
             "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n\r\n" % (path, host, key)).encode())

        while b"\r\n\r\n" not in self._buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise WSError("no handshake response")
            self._buf += chunk
        head, self._buf = self._buf.split(b"\r\n\r\n", 1)
        if b"101" not in head.split(b"\r\n")[0]:
            raise WSError("handshake refused: %s" % head.split(b"\r\n")[0].decode())

        hello = self._recv()
        if hello.get("type") == "auth_required":
            self._send(json.dumps({"type": "auth", "access_token": self.token}))
            reply = self._recv()
            if reply.get("type") != "auth_ok":
                raise WSError("authentication refused: %s" % reply.get("message", reply))
        elif hello.get("type") != "auth_ok":
            raise WSError("unexpected greeting: %s" % hello.get("type"))
        return self

    def __exit__(self, *exc):
        try:
            self.sock.close()
        except OSError:
            pass
        return False

    def command(self, **payload):
        self._id += 1
        payload["id"] = self._id
        self._send(json.dumps(payload))
        while True:
            msg = self._recv()
            # Events and other ids can interleave; wait for our result.
            if msg.get("type") == "result" and msg.get("id") == payload["id"]:
                if not msg.get("success", False):
                    err = msg.get("error") or {}
                    raise WSError(err.get("message") or "command failed")
                return msg.get("result")
