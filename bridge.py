#!/usr/bin/env python3
"""
Process Performance Monitor - Hardware Bridge Server
------------------------------------------------------
Connects your Arduino board to your local network and the web.
Streams real-time step-response process telemetry to smartphones and laptops.

Usage:
    python bridge.py
    python bridge.py --port COM3 --baud 115200
    python bridge.py --http-port 8765
    python bridge.py --demo (run in simulation mode without hardware)
"""

import sys
import os
import time
import socket
import struct
import base64
import hashlib
import threading
import argparse
from http.server import HTTPServer, SimpleHTTPRequestHandler

# Try importing pyserial
try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

MAGIC_GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

def get_local_ip():
    """Resolve the machine's primary local network IP address."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = '127.0.0.1'
    finally:
        s.close()
    return ip

def auto_detect_arduino():
    """Scan and find connected Arduino / USB-to-serial ports."""
    if not HAS_SERIAL:
        return None
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").lower()
        if any(k in desc or k in hwid for k in ['arduino', 'ch340', 'ftdi', 'cp210', 'usb-serial', 'acm']):
            return p.device
    if ports:
        return ports[0].device
    return None

class WebSocketClient:
    """Represents a connected browser / smartphone client."""
    def __init__(self, sock, addr):
        self.sock = sock
        self.addr = addr
        self.lock = threading.Lock()
        self.closed = False

    def send_text(self, text):
        if self.closed:
            return False
        data = text.encode('utf-8')
        length = len(data)
        if length <= 125:
            header = struct.pack("!BB", 0x81, length)
        elif length <= 65535:
            header = struct.pack("!BBH", 0x81, 126, length)
        else:
            header = struct.pack("!BBQ", 0x81, 127, length)
        try:
            with self.lock:
                self.sock.sendall(header + data)
            return True
        except Exception:
            self.closed = True
            return False

    def close(self):
        self.closed = True
        try:
            self.sock.close()
        except Exception:
            pass

class BridgeServer:
    def __init__(self, serial_port=None, baud=115200, http_port=8765, is_demo=False):
        self.serial_port_name = serial_port
        self.baud = baud
        self.http_port = http_port
        self.is_demo = is_demo
        self.ser = None
        self.clients = []
        self.clients_lock = threading.Lock()
        self.running = True

    def broadcast(self, line):
        """Broadcast a telemetry line to all connected smartphones & browsers."""
        dead_clients = []
        with self.clients_lock:
            for c in self.clients:
                if not c.send_text(line):
                    dead_clients.append(c)
            for c in dead_clients:
                self.clients.remove(c)

    def on_client_message(self, client, message):
        """Handle incoming command from smartphone / web client."""
        msg = message.strip()
        print(f"  [<- CMD] Received from {client.addr[0]}: {msg}")
        if self.ser and self.ser.is_open:
            try:
                self.ser.write(msg.encode('latin1'))
            except Exception as e:
                print(f"  [!] Serial write error: {e}")

    def serial_loop(self):
        """Read lines from Arduino and broadcast to clients."""
        while self.running:
            if not self.ser or not self.ser.is_open:
                time.sleep(1)
                continue
            try:
                raw = self.ser.readline()
                if not raw:
                    continue
                line = raw.decode('utf-8', errors='ignore').strip()
                if line:
                    if line.startswith('DATA') or line.startswith('META') or line == 'END':
                        print(f"  [-> DATA] {line}")
                    self.broadcast(line)
            except Exception as e:
                print(f"  [!] Serial read error: {e}")
                time.sleep(0.5)

    def demo_loop(self):
        """Simulated signal generator when no hardware is connected."""
        print("  [*] Running in simulated Demo Mode.")
        t = 0.0
        while self.running:
            time.sleep(0.2)
            if not self.clients:
                continue
            # idle wait until a start command
            t += 0.2

    def start_serial(self):
        if self.is_demo:
            threading.Thread(target=self.demo_loop, daemon=True).start()
            return

        if not HAS_SERIAL:
            print("  [!] pyserial is not installed. Running in demo mode.")
            self.is_demo = True
            threading.Thread(target=self.demo_loop, daemon=True).start()
            return

        if not self.serial_port_name:
            self.serial_port_name = auto_detect_arduino()

        if not self.serial_port_name:
            print("  [!] No Arduino board detected on USB. Starting in demo mode.")
            self.is_demo = True
            threading.Thread(target=self.demo_loop, daemon=True).start()
            return

        try:
            print(f"  [*] Connecting to Arduino on {self.serial_port_name} at {self.baud} baud...")
            self.ser = serial.Serial(self.serial_port_name, self.baud, timeout=1)
            time.sleep(2)  # Arduino auto-reset settling time
            print(f"  [+] Serial port {self.serial_port_name} connected successfully!")
            threading.Thread(target=self.serial_loop, daemon=True).start()
        except Exception as e:
            print(f"  [!] Could not open serial port {self.serial_port_name}: {e}")
            print("  [*] Falling back to simulation mode.")
            self.is_demo = True
            threading.Thread(target=self.demo_loop, daemon=True).start()

    def start_http(self):
        bridge = self
        workspace_dir = os.path.dirname(os.path.abspath(__file__))

        class UnifiedHandler(SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=workspace_dir, **kwargs)

            def log_message(self, format, *args):
                pass  # Suppress routine static asset access logs

            def do_GET(self):
                # Detect WebSocket Upgrade header
                if self.headers.get('Upgrade', '').lower() == 'websocket':
                    self.handle_websocket()
                else:
                    if self.path in ['/', '']:
                        self.path = '/Process_Monitor_Dashboard.html'
                    super().do_GET()

            def handle_websocket(self):
                key = self.headers.get('Sec-WebSocket-Key', '')
                accept_key = base64.b64encode(hashlib.sha1(key.encode('latin1') + MAGIC_GUID).digest()).decode('latin1')

                self.send_response(101, 'Switching Protocols')
                self.send_header('Upgrade', 'websocket')
                self.send_header('Connection', 'Upgrade')
                self.send_header('Sec-WebSocket-Accept', accept_key)
                self.end_headers()

                # Handshake complete, handle WebSocket frames
                client = WebSocketClient(self.connection, self.client_address)
                with bridge.clients_lock:
                    bridge.clients.append(client)
                print(f"  [+] Smartphone / Browser client connected from {self.client_address[0]}:{self.client_address[1]}")

                try:
                    while bridge.running and not client.closed:
                        header = self.rfile.read(2)
                        if not header or len(header) < 2:
                            break
                        b1, b2 = header[0], header[1]
                        opcode = b1 & 0x0F
                        masked = (b2 & 0x80) != 0
                        payload_len = b2 & 0x7F

                        if payload_len == 126:
                            payload_len = struct.unpack("!H", self.rfile.read(2))[0]
                        elif payload_len == 127:
                            payload_len = struct.unpack("!Q", self.rfile.read(8))[0]

                        masks = self.rfile.read(4) if masked else None
                        payload = bytearray(self.rfile.read(payload_len))
                        if masked and masks:
                            for i in range(len(payload)):
                                payload[i] ^= masks[i % 4]

                        if opcode == 0x8:  # Close frame
                            break
                        elif opcode == 0x9:  # Ping frame
                            # Pong response
                            pong = struct.pack("!BB", 0x8A, 0)
                            self.connection.sendall(pong)
                        elif opcode == 0x1:  # Text frame
                            text = payload.decode('utf-8', errors='ignore')
                            bridge.on_client_message(client, text)
                except Exception:
                    pass
                finally:
                    client.close()
                    with bridge.clients_lock:
                        if client in bridge.clients:
                            bridge.clients.remove(client)
                    print(f"  [-] Client disconnected: {self.client_address[0]}")

        server = HTTPServer(('0.0.0.0', self.http_port), UnifiedHandler)
        local_ip = get_local_ip()
        url = f"http://{local_ip}:{self.http_port}"
        ws_url = f"ws://{local_ip}:{self.http_port}"

        print("\n" + "=" * 68)
        print("🚀 PROCESS PERFORMANCE MONITOR - SMARTPHONE & NETWORK BRIDGE")
        print("=" * 68)
        if self.ser and self.ser.is_open:
            print(f"📡 Hardware:   Arduino connected on {self.serial_port_name} (115200 baud)")
        else:
            print("📡 Hardware:   Demo simulation mode active")
        print(f"🌐 Local IP:   {local_ip}")
        print(f"📱 Web URL:    {url}")
        print(f"⚡ WebSocket:  {ws_url}")
        print("=" * 68)
        print("\n👉 TO MONITOR ON YOUR SMARTPHONE:")
        print(f"   1. Connect your phone to the same WiFi network (or laptop hotspot).")
        print(f"   2. Open Chrome or Safari on your phone and browse to:")
        print(f"      {url}")
        print("=" * 68)
        print("Press Ctrl+C to terminate the bridge.\n")

        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down bridge...")
        finally:
            self.running = False
            if self.ser and self.ser.is_open:
                self.ser.close()
            server.server_close()

def main():
    parser = argparse.ArgumentParser(description="Process Performance Monitor - Smartphone Network Bridge")
    parser.add_argument('--port', type=str, default=None, help="Serial COM port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument('--baud', type=int, default=115200, help="Serial baud rate (default: 115200)")
    parser.add_argument('--http-port', type=int, default=8765, help="HTTP & WebSocket port (default: 8765)")
    parser.add_argument('--demo', action='store_true', help="Run in simulation mode without hardware")
    args = parser.parse_args()

    bridge = BridgeServer(serial_port=args.port, baud=args.baud, http_port=args.http_port, is_demo=args.demo)
    bridge.start_serial()
    bridge.start_http()

if __name__ == '__main__':
    main()
