from __future__ import annotations

import argparse
import hashlib
import sys
import time
from pathlib import Path

import serial


def read_line(port: serial.Serial, timeout_s: float) -> str:
    deadline = time.time() + timeout_s
    buffer = bytearray()

    while time.time() < deadline:
        chunk = port.read(1)
        if not chunk:
            continue
        if chunk == b"\n":
            return buffer.decode("utf-8", errors="replace").strip()
        if chunk != b"\r":
            buffer.extend(chunk)

    raise TimeoutError("Timed out waiting for device response")


def wait_for_prefix(port: serial.Serial, prefixes: tuple[str, ...], timeout_s: float) -> str:
    deadline = time.time() + timeout_s

    while time.time() < deadline:
        line = read_line(port, max(0.2, deadline - time.time()))
        if not line:
            continue
        print(f"< {line}")
        if line.startswith(prefixes):
            return line

    raise TimeoutError(f"Timed out waiting for one of: {prefixes}")


def send_line(port: serial.Serial, text: str) -> None:
    print(f"> {text}")
    port.write((text + "\n").encode("utf-8"))
    port.flush()


def do_info(port: serial.Serial) -> None:
    send_line(port, "info")
    wait_for_prefix(port, ("INFO ",), 3.0)


def do_ping(port: serial.Serial) -> None:
    send_line(port, "ping")
    wait_for_prefix(port, ("OK pong",), 3.0)


def send_image_in_chunks(port: serial.Serial, image: bytes, chunk_size: int, chunk_delay_ms: float) -> None:
    total = len(image)
    sent = 0
    next_report = 0

    while sent < total:
        end = min(sent + chunk_size, total)
        port.write(image[sent:end])
        port.flush()
        sent = end

        progress = int((sent * 100) / total)
        if progress >= next_report or sent == total:
            print(f"> progress {sent}/{total} ({progress}%)")
            next_report += 10

        if chunk_delay_ms > 0 and sent < total:
            time.sleep(chunk_delay_ms / 1000.0)


def wait_for_ota_result(port: serial.Serial, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    saw_disconnect = False

    while time.time() < deadline:
        try:
            line = read_line(port, max(0.2, deadline - time.time()))
            if line:
                print(f"< {line}")
                if line.startswith("OK ota_complete"):
                    return
                if line.startswith("ERR "):
                    raise RuntimeError(line)
        except serial.SerialException:
            saw_disconnect = True
            break
        except TimeoutError:
            continue

    if saw_disconnect:
        print("< serial disconnected during reboot, treating as probable success")
        return

    raise TimeoutError("Timed out waiting for OTA completion response")


def do_ota(port: serial.Serial, image_path: Path, chunk_size: int, chunk_delay_ms: float) -> None:
    image = image_path.read_bytes()
    image_sha256 = hashlib.sha256(image).hexdigest()

    send_line(port, f"ota {len(image)} {image_sha256}")
    wait_for_prefix(port, ("READY ",), 5.0)
    print(f"> sending {len(image)} bytes in chunks of {chunk_size}")
    send_image_in_chunks(port, image, chunk_size, chunk_delay_ms)
    wait_for_ota_result(port, 40.0)


def main() -> int:
    parser = argparse.ArgumentParser(description="ESP32-S3 CDC OTA helper")
    parser.add_argument("--port", type=str,default="COM11", help="Serial port, for example COM6")
    parser.add_argument("--baud", type=int, default=460800, help="Serial baudrate (CDC usually ignores this)")
    parser.add_argument("--timeout", type=float, default=1.0, help="Per-read timeout in seconds")
    parser.add_argument("--image", type=Path,default="../build/ota_demo.bin", help="Firmware image path, usually build/ota_example.bin")
    parser.add_argument("--chunk-size", type=int, default=512, help="Bytes sent per chunk during OTA")
    parser.add_argument("--chunk-delay-ms", type=float, default=10.0, help="Delay between OTA chunks in milliseconds")
    parser.add_argument("command", choices=("ping", "info", "ota"))
    args = parser.parse_args()

    if args.chunk_size <= 0:
        parser.error("--chunk-size must be > 0")
    if args.chunk_delay_ms < 0:
        parser.error("--chunk-delay-ms must be >= 0")

    try:
        with serial.Serial(args.port, args.baud, timeout=args.timeout, write_timeout=10) as port:
            time.sleep(0.2)
            port.reset_input_buffer()
            do_ping(port)

            if args.command == "ping":
                return 0

            if args.command == "info":
                do_info(port)
                return 0

            if args.image is None:
                parser.error("--image is required for ota")

            do_ota(port, args.image, args.chunk_size, args.chunk_delay_ms)
            return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
