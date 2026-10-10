#!/usr/bin/env python3
"""Run one bench from docs/physical/BENCH_PROOF.md and record it.

Each subcommand builds and flashes the firmware for its bench, captures the
serial output of the master during this run, stores the raw capture, and
calls tools/bench_record.py. It refuses to record when nothing real was
captured, and it refuses a pass that the capture does not support.

  device-on-commercial-master       iolinki reference firmware on the
                                    NUCLEO-G0B1RE + TIOL112 EVM, attached to a
                                    commercial master; captures that master's
                                    serial log.
  commercial-sensor-on-our-master   an iolinki-master build with an
                                    out-of-tree PHY adapter, attached to a
                                    commercial sensor; waits for the
                                    iolinki-gw/1 gateway line.

Only the Python standard library is used. Serial ports are opened through
termios, so this runs on Linux and macOS.
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import select
import shlex
import subprocess
import sys
import termios
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(TOOLS_DIR)
RECORDER = os.path.join(TOOLS_DIR, "bench_record.py")
G0_EXAMPLE = os.path.join(REPO_DIR, "examples", "stm32g0_tiol112")
REFERENCE_IDENTITY = os.path.join(
    REPO_DIR, "examples", "reference_device", "reference_device.c"
)
DEFAULT_G0_FLASH = 'openocd -f interface/stlink.cfg -f target/stm32g0x.cfg -c "program {elf} verify reset exit"'
GATEWAY = re.compile(r"iolinki-gw/1 [0-9]+ ([0-9a-f]{4}) ([0-9a-f]{8}) (?:[0-9a-f]+|-)")
MIN_CAPTURE = 10


class BenchError(Exception):
    """A condition under which nothing may be recorded."""


def _run(cmd, cwd=None, env=None):
    print("+ " + cmd, flush=True)
    result = subprocess.run(cmd, shell=True, cwd=cwd, env=env, check=False)
    if result.returncode != 0:
        raise BenchError("command failed (%d): %s" % (result.returncode, cmd))


def _sha256_file(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def clean_commit(repo):
    """Return HEAD of @p repo; refuse a dirty tree so the commit names the firmware."""
    status = subprocess.run(
        ["git", "-C", repo, "status", "--porcelain", "--untracked-files=no"],
        capture_output=True,
        text=True,
        check=False,
    )
    if status.returncode != 0:
        raise BenchError("%s is not a git checkout" % repo)
    if status.stdout.strip():
        raise BenchError(
            "%s has uncommitted changes; commit or stash them first" % repo
        )
    head = subprocess.run(
        ["git", "-C", repo, "rev-parse", "HEAD"],
        capture_output=True,
        text=True,
        check=True,
    )
    return head.stdout.strip()


def reference_identity(path=REFERENCE_IDENTITY):
    """Return (vendor_id, device_id) as 4 and 8 lowercase hex digits from the source."""
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    found = {}
    for name in ("vendor_id", "device_id"):
        match = re.search(r"\.%s\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*," % name, text)
        if match is None:
            raise BenchError("cannot find .%s in %s" % (name, path))
        found[name] = int(match.group(1), 0)
    return "%04x" % found["vendor_id"], "%08x" % found["device_id"]


def open_serial(path, baud):
    """Open @p path raw at @p baud, 8 data bits, no echo, non-blocking."""
    speed = getattr(termios, "B%d" % baud, None)
    if speed is None:
        raise BenchError("unsupported baud rate %d" % baud)
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as exc:
        raise BenchError("cannot open %s: %s" % (path, exc)) from exc
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = speed
    attrs[5] = speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)
    return fd


def capture(fd, seconds, stop=None):
    """Read from @p fd for up to @p seconds; echo it; stop early when stop(text) is true."""
    chunks = []
    deadline = time.monotonic() + seconds
    try:
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                break
            ready, _, _ = select.select([fd], [], [], min(left, 0.2))
            if not ready:
                continue
            try:
                data = os.read(fd, 4096)
            except BlockingIOError:
                continue
            except OSError:
                break
            if not data:
                continue
            text = data.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
            chunks.append(text)
            if stop is not None and stop("".join(chunks)):
                break
    except KeyboardInterrupt:
        print("\n(capture stopped by operator)", flush=True)
    return "".join(chunks)


def mentions_identity(text, vendor_id, device_id):
    """True when @p text shows both IDs as hex, with or without 0x and leading zeros."""
    lowered = text.lower()
    for value in (vendor_id, device_id):
        short = value.lstrip("0") or "0"
        pattern = r"(?<![0-9a-f])(?:0x)?0*%s(?![0-9a-f])" % re.escape(short)
        if re.search(pattern, lowered) is None:
            return False
    return True


def matching_gateway_line(text, vendor_id, device_id):
    """Return (line for this identity or None, list of all gateway lines seen)."""
    seen = []
    for raw in text.splitlines():
        match = GATEWAY.search(raw)
        if match is None:
            continue
        line = match.group(0)
        seen.append(line)
        if match.group(1) == vendor_id and match.group(2) == device_id:
            return line, seen
    return None, seen


def new_run_dir(base, role):
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    path = os.path.join(base, "%s-%s" % (stamp, role))
    os.makedirs(path, exist_ok=False)
    return path


def store_capture(run_dir, text, meta):
    if len(text.strip()) < MIN_CAPTURE:
        raise BenchError("no usable serial output was captured; nothing recorded")
    raw = os.path.join(run_dir, "capture.log")
    with open(raw, "w", encoding="utf-8") as handle:
        handle.write(text)
    meta["capture_sha256"] = hashlib.sha256(text.encode("utf-8")).hexdigest()
    meta["capture_bytes"] = len(text.encode("utf-8"))
    with open(os.path.join(run_dir, "run.json"), "w", encoding="utf-8") as handle:
        json.dump(meta, handle, indent=2)
        handle.write("\n")


def record(run_dir, fields):
    cmd = [sys.executable, RECORDER, "--output", os.path.join(run_dir, "record.json")]
    for key in (
        "role",
        "result",
        "commit",
        "counterpart",
        "vendor_id",
        "device_id",
        "notes",
    ):
        cmd += ["--" + key.replace("_", "-"), fields[key]]
    for key in ("gateway_line", "master_log"):
        if fields.get(key) is not None:
            cmd += ["--" + key.replace("_", "-"), fields[key]]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise BenchError("bench_record.py refused: " + result.stderr.strip())
    return os.path.join(run_dir, "record.json")


def run_device_bench(args):
    role = "device-on-commercial-master"
    commit = clean_commit(REPO_DIR)
    vendor_id, device_id = reference_identity()
    for var in ("STM32_CMSIS_ROOT", "CMSIS_CORE_ROOT"):
        if not os.environ.get(var):
            raise BenchError(
                "%s must be set; see examples/stm32g0_tiol112/README.md" % var
            )
    run_dir = new_run_dir(args.out_dir, role)
    build_dir = os.path.join(run_dir, "build")
    env = dict(os.environ, BUILD_DIR=build_dir)
    _run("bash " + shlex.quote(os.path.join(G0_EXAMPLE, "build.sh")), env=env)
    elf = os.path.join(build_dir, "reference-device.elf")
    if not os.path.isfile(elf):
        raise BenchError("build did not produce " + elf)
    hexfile = os.path.join(build_dir, "reference-device.hex")
    flash_cmd = args.flash_cmd.format(elf=shlex.quote(elf), hex=shlex.quote(hexfile))
    fd = open_serial(args.master_serial, args.master_baud)
    try:
        _run(flash_cmd)
        print(
            "Capturing %s for %d s. Let the master reach OPERATE and exchange "
            "process data; Ctrl-C ends early."
            % (args.master_serial, args.capture_seconds),
            flush=True,
        )
        text = capture(fd, args.capture_seconds)
    finally:
        os.close(fd)
    meta = {
        "role": role,
        "commit": commit,
        "firmware": os.path.relpath(elf, run_dir),
        "firmware_sha256": _sha256_file(elf),
        "flash_cmd": flash_cmd,
        "serial": args.master_serial,
        "baud": args.master_baud,
        "vendor_id": vendor_id,
        "device_id": device_id,
    }
    store_capture(run_dir, text, meta)
    if args.result == "pass" and not mentions_identity(text, vendor_id, device_id):
        raise BenchError(
            "the master log does not show VendorID %s and DeviceID %s; refusing a pass "
            "(capture kept in %s)" % (vendor_id, device_id, run_dir)
        )
    return record(
        run_dir,
        {
            "role": role,
            "result": args.result,
            "commit": commit,
            "counterpart": args.counterpart,
            "vendor_id": vendor_id,
            "device_id": device_id,
            "notes": args.notes,
            "master_log": text.strip(),
        },
    )


def run_sensor_bench(args):
    role = "commercial-sensor-on-our-master"
    vendor_id = args.vendor_id.lower()
    device_id = args.device_id.lower()
    if re.fullmatch(r"[0-9a-f]{4}", vendor_id) is None:
        raise BenchError(
            "--vendor-id is 4 hex digits from the sensor datasheet or IODD"
        )
    if re.fullmatch(r"[0-9a-f]{8}", device_id) is None:
        raise BenchError(
            "--device-id is 8 hex digits from the sensor datasheet or IODD"
        )
    master_dir = os.path.abspath(args.master_dir)
    commit = clean_commit(master_dir)
    run_dir = new_run_dir(args.out_dir, role)
    _run(args.build_cmd, cwd=master_dir)
    fd = open_serial(args.console_serial, args.console_baud)
    try:
        _run(args.flash_cmd, cwd=master_dir)
        print(
            "Waiting up to %d s on %s for an iolinki-gw/1 line with %s %s."
            % (args.timeout, args.console_serial, vendor_id, device_id),
            flush=True,
        )
        text = capture(
            fd,
            args.timeout,
            stop=lambda seen: (
                matching_gateway_line(seen, vendor_id, device_id)[0] is not None
            ),
        )
    finally:
        os.close(fd)
    meta = {
        "role": role,
        "commit": commit,
        "master_dir": master_dir,
        "build_cmd": args.build_cmd,
        "flash_cmd": args.flash_cmd,
        "serial": args.console_serial,
        "baud": args.console_baud,
        "vendor_id": vendor_id,
        "device_id": device_id,
    }
    store_capture(run_dir, text, meta)
    line, seen = matching_gateway_line(text, vendor_id, device_id)
    fields = {
        "role": role,
        "commit": commit,
        "counterpart": args.counterpart,
        "vendor_id": vendor_id,
        "device_id": device_id,
        "notes": args.notes,
    }
    if line is not None:
        fields.update(result="pass", gateway_line=line)
        return record(run_dir, fields)
    reason = (
        "gateway lines for another identity: " + "; ".join(seen)
        if seen
        else "no iolinki-gw/1 line was printed"
    )
    if not args.record_failure:
        raise BenchError(
            "%s; nothing recorded (capture kept in %s, --record-failure records a fail)"
            % (reason, run_dir)
        )
    fields.update(result="fail", master_log=text.strip())
    return record(run_dir, fields)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="bench", required=True)

    dev = sub.add_parser(
        "device-on-commercial-master", help="iolinki device firmware bench"
    )
    dev.add_argument(
        "--master-serial", required=True, help="serial port of the master's log"
    )
    dev.add_argument("--master-baud", type=int, default=115200)
    dev.add_argument("--capture-seconds", type=int, default=60)
    dev.add_argument("--counterpart", default="P-NUCLEO-IOM01M1")
    dev.add_argument("--result", required=True, choices=("pass", "fail"))
    dev.add_argument("--notes", required=True, help="what the operator observed")
    dev.add_argument(
        "--flash-cmd",
        default=DEFAULT_G0_FLASH,
        help="flash command; {elf} and {hex} are replaced (default: OpenOCD + ST-LINK)",
    )
    dev.add_argument("--out-dir", default=os.path.join(REPO_DIR, "bench-results"))

    sen = sub.add_parser("commercial-sensor-on-our-master", help="iolinki-master bench")
    sen.add_argument(
        "--master-dir", required=True, help="iolinki-master checkout that is built"
    )
    sen.add_argument(
        "--build-cmd", required=True, help="builds the out-of-tree master firmware"
    )
    sen.add_argument("--flash-cmd", required=True, help="flashes that firmware")
    sen.add_argument(
        "--console-serial", required=True, help="port that prints the gateway line"
    )
    sen.add_argument("--console-baud", type=int, default=115200)
    sen.add_argument(
        "--counterpart", required=True, help="commercial sensor part number"
    )
    sen.add_argument("--vendor-id", required=True)
    sen.add_argument("--device-id", required=True)
    sen.add_argument("--notes", required=True, help="what the operator observed")
    sen.add_argument("--timeout", type=int, default=120)
    sen.add_argument("--record-failure", action="store_true")
    sen.add_argument("--out-dir", default=os.path.join(REPO_DIR, "bench-results"))

    args = parser.parse_args(argv)
    try:
        if args.bench == "device-on-commercial-master":
            path = run_device_bench(args)
        else:
            path = run_sensor_bench(args)
    except BenchError as exc:
        print("bench_run: " + str(exc), file=sys.stderr)
        return 2
    print("recorded " + path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
