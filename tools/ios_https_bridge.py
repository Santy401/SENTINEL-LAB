#!/usr/bin/env python3
"""Opt-in local TLS bridge. Requires user-provided trusted certificate/key."""
import argparse
import http.client
import http.server
import ipaddress
import ssl

ROUTES = {"GET": {"/", "/motion", "/capture", "/api/state", "/api/rf", "/api/lab", "/api/events", "/api/export.csv"},
          "POST": {"/api/accel", "/api/config", "/api/experiments", "/api/measurements", "/api/calibration"}}

class Bridge(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def proxy(self):
        if self.path not in ROUTES.get(self.command, set()):
            self.send_error(404)
            return
        if self.command == "POST" and self.headers.get("Origin") != self.server.origin:
            self.send_error(403, "Origin rejected")
            return
        if self.headers.get("Transfer-Encoding"):
            self.send_error(400)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_error(400)
            return
        if not 0 <= length <= 1024:
            self.send_error(413)
            return
        connection = http.client.HTTPConnection("192.168.4.1", 80, timeout=3)
        try:
            payload = self.rfile.read(length) if length else None
            connection.request(self.command, self.path, body=payload,
                               headers={"Content-Type": "application/json"})
            response = connection.getresponse()
            data = response.read(262145)
            if len(data) > 262144:
                raise ValueError("response too large")
            self.send_response(response.status)
            self.send_header("Content-Type", response.getheader("Content-Type", "application/json"))
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Permissions-Policy", "accelerometer=(self), gyroscope=(self)")
            self.end_headers()
            self.wfile.write(data)
        except (OSError, ValueError, http.client.HTTPException):
            self.send_error(502, "ESP32 unavailable")
        finally:
            connection.close()

    do_GET = proxy
    do_POST = proxy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", required=True, help="Host IPv4 in the SENTINEL AP subnet")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    args = parser.parse_args()
    address = ipaddress.ip_address(args.bind)
    if address not in ipaddress.ip_network("192.168.4.0/24") or str(address) in {"192.168.4.0", "192.168.4.1", "192.168.4.255"}:
        parser.error("bind must be the host address in the laboratory AP subnet")
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.cert, args.key)
    # Single worker bounds resource use; browser posting is also serialized.
    server = http.server.HTTPServer((args.bind, args.port), Bridge)
    server.origin = f"https://{args.bind}:{args.port}"
    server.socket = context.wrap_socket(server.socket, server_side=True)
    print(f"Local bridge: {server.origin}/capture")
    try:
        server.serve_forever()
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
