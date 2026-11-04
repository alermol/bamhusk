#!/usr/bin/env python3
"""Generate a deterministic-shape BAM for benchmarks.

The sequence and quality bytes come from the OS random source so the output
does not compress unrealistically well. The record layout is deterministic.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys


def random_bytes(size: int) -> bytes:
    return os.urandom(size)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    parser.add_argument("--reads", type=int, default=100_000)
    parser.add_argument("--read-length", type=int, default=100)
    args = parser.parse_args()

    if args.reads <= 0 or args.read_length <= 0:
        raise SystemExit("--reads and --read-length must be positive")

    base_table = bytes.maketrans(
        bytes(range(256)),
        bytes(b"ACGT"[index % 4] for index in range(256)),
    )
    quality_table = bytes.maketrans(
        bytes(range(256)),
        bytes(33 + index % 41 for index in range(256)),
    )
    reference_length = args.reads * args.read_length + 1000

    header = (
        "@HD\tVN:1.6\tSO:coordinate\n"
        f"@SQ\tSN:chr1\tLN:{reference_length}\n"
        "@RG\tID:s1\tSM:benchmark\n"
    ).encode()

    try:
        process = subprocess.Popen(
            ["samtools", "view", "--no-PG", "-b", "-o", args.output, "-"],
            stdin=subprocess.PIPE,
        )
    except FileNotFoundError:
        raise SystemExit("samtools is required") from None

    assert process.stdin is not None
    process.stdin.write(header)

    random_buffer = b""
    quality_buffer = b""

    def take(buffer: bytes, size: int) -> tuple[bytes, bytes]:
        while len(buffer) < size:
            buffer += random_bytes(max(size, 1 << 20))
        return buffer[:size], buffer[size:]

    for index in range(args.reads):
        sequence_bytes, random_buffer = take(
            random_buffer, args.read_length
        )
        quality_bytes, quality_buffer = take(
            quality_buffer, args.read_length
        )
        sequence = sequence_bytes.translate(base_table)
        quality = quality_bytes.translate(quality_table)
        line = (
            f"read{index}\t0\tchr1\t{index * args.read_length + 1}\t60\t"
            f"{args.read_length}M\t*\t0\t0\t"
        ).encode() + sequence + b"\t" + quality + b"\tRG:Z:s1\n"
        process.stdin.write(line)

    process.stdin.close()
    status = process.wait()
    if status != 0:
        return status
    print(f"{args.output}\t{os.path.getsize(args.output)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
