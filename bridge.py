#!/usr/bin/env python3
import argparse
import asyncio
import re

import serial
import websockets


VALID_CMD = re.compile(r"^(?:[A-Z](?:,-?\d+)?|X,-?\d+,-?\d+)$")


class SerialBridge:
    def __init__(self, port: str, baud: int):
        self.serial = serial.Serial(port=port, baudrate=baud, timeout=0.1)
        self.lock = asyncio.Lock()

    async def send(self, line: str) -> None:
        line = line.strip().upper()
        if not VALID_CMD.match(line):
            return
        async with self.lock:
            self.serial.write((line + "\n").encode("ascii"))
            self.serial.flush()

    async def safe_stop(self) -> None:
        await self.send("S")
        await self.send("B,0")


async def run_server(host: str, ws_port: int, bridge: SerialBridge) -> None:
    async def handler(websocket):
        print("Client connected")
        try:
            async for message in websocket:
                cmd = message.strip().upper()
                if not VALID_CMD.match(cmd):
                    await websocket.send("ERR invalid command")
                    continue
                await bridge.send(cmd)
                await websocket.send(f"OK {cmd}")
        finally:
            print("Client disconnected -> stop")
            await bridge.safe_stop()

    async with websockets.serve(handler, host, ws_port):
        print(f"WebSocket server on ws://{host}:{ws_port}")
        await asyncio.Future()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="WebSocket to Serial bridge for ESP32 motor control"
    )
    parser.add_argument(
        "--serial-port", default="/dev/cu.usbserial-0001", help="ESP32 serial port"
    )
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--host", default="127.0.0.1", help="WebSocket host")
    parser.add_argument("--ws-port", type=int, default=8765, help="WebSocket port")
    return parser.parse_args()


async def main() -> None:
    args = parse_args()
    bridge = SerialBridge(args.serial_port, args.baud)
    await run_server(args.host, args.ws_port, bridge)


if __name__ == "__main__":
    asyncio.run(main())
