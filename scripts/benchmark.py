#!/usr/bin/env python3
"""Benchmark husk against samtools view -b and report throughput/RSS."""

from __future__ import annotations

import argparse
import json
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


TIME_BIN = next(
    (path for path in (Path("/usr/bin/time"), Path("/bin/time")) if path.is_file()),
    None,
)


def run_once(command: list[str], time_file: Path | None) -> float:
    start = time.perf_counter()
    if time_file is not None and TIME_BIN is not None:
        subprocess.run(
            [str(TIME_BIN), "-v" if platform.system() == "Linux" else "-l",
             "-o", str(time_file), *command],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    else:
        subprocess.run(
            command,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    return time.perf_counter() - start


def peak_rss_mib(time_file: Path) -> float | None:
    if not time_file.is_file():
        return None
    text = time_file.read_text(errors="replace")
    if platform.system() == "Linux":
        match = re.search(r"Maximum resident set size \(kbytes\): (\d+)", text)
        if match:
            return int(match.group(1)) / 1024.0
    else:
        match = re.search(r"(\d+)\s+maximum resident set size", text)
        if match:
            return int(match.group(1)) / (1024.0 * 1024.0)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bam", required=True)
    parser.add_argument("--husk", default="build/husk")
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--output-json")
    args = parser.parse_args()

    if shutil.which("samtools") is None:
        raise SystemExit("samtools is required")
    if not Path(args.husk).is_file():
        raise SystemExit(f"husk binary not found: {args.husk}")
    if args.runs <= 0:
        raise SystemExit("--runs must be positive")

    input_size = Path(args.bam).stat().st_size
    reads = int(
        subprocess.check_output(
            ["samtools", "view", "-c", args.bam], text=True
        ).strip()
    )

    work = Path(tempfile.mkdtemp(prefix="husk-benchmark-"))
    time_file = work / "time.txt"
    results: dict[str, dict[str, object]] = {}

    commands = {
        "husk": (
            [
                args.husk,
                "--threads",
                str(args.threads),
                "--force",
                args.bam,
                str(work / "husk.bam"),
            ],
            work / "husk.bam",
        ),
        "samtools": (
            [
                "samtools",
                "view",
                "--no-PG",
                "-@",
                str(args.threads),
                "-b",
                "-o",
                str(work / "samtools.bam"),
                args.bam,
            ],
            work / "samtools.bam",
        ),
    }

    try:
        for name, (command, output) in commands.items():
            for _ in range(1):
                run_once(command, None)
            times = []
            rss = None
            for _ in range(args.runs):
                if output.exists():
                    output.unlink()
                if time_file.exists():
                    time_file.unlink()
                times.append(run_once(command, time_file))
                current_rss = peak_rss_mib(time_file)
                if current_rss is not None:
                    rss = max(rss or 0.0, current_rss)
            results[name] = {
                "mean_seconds": statistics.mean(times),
                "median_seconds": statistics.median(times),
                "min_seconds": min(times),
                "peak_rss_mib": rss,
            }
    finally:
        shutil.rmtree(work, ignore_errors=True)

    for result in results.values():
        seconds = float(result["median_seconds"])
        result["input_mib_per_second"] = (
            input_size / (1024.0 * 1024.0) / seconds if seconds else None
        )
        result["reads_per_second"] = reads / seconds if seconds else None

    ratio = (
        float(results["husk"]["median_seconds"])
        / float(results["samtools"]["median_seconds"])
        if results["samtools"]["median_seconds"]
        else None
    )

    print(f"input: {args.bam} ({input_size / (1024.0 * 1024.0):.2f} MiB, "
          f"{reads} reads)")
    print()
    print("| Tool | Median s | MiB/s | Reads/s | Peak RSS MiB |")
    print("|---|---:|---:|---:|---:|")
    for name, result in results.items():
        rss = result["peak_rss_mib"]
        print(
            f"| {name} | {result['median_seconds']:.3f} | "
            f"{result['input_mib_per_second']:.1f} | "
            f"{result['reads_per_second']:.0f} | "
            f"{rss:.1f} |" if rss is not None else
            f"| {name} | {result['median_seconds']:.3f} | "
            f"{result['input_mib_per_second']:.1f} | "
            f"{result['reads_per_second']:.0f} | n/a |"
        )
    if ratio is not None:
        print()
        print(f"husk / samtools median time ratio: {ratio:.2f}x")
        if ratio > 1.20:
            print("warning: husk is more than 20% slower than samtools")

    if args.output_json:
        Path(args.output_json).write_text(
            json.dumps(
                {
                    "input": args.bam,
                    "input_bytes": input_size,
                    "reads": reads,
                    "threads": args.threads,
                    "runs": args.runs,
                    "results": results,
                    "ratio": ratio,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
