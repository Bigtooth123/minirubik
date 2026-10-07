#!/usr/bin/env python3

import argparse
import csv
import hashlib
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


EXPECTED_STATE_COUNT = 2644
EXPECTED_LENGTH = 11
CSV_FIELDS = (
    "state",
    "instructions",
    "x30",
    "x31",
    "status",
    "source_sha256",
)
INPUT_PATTERN = re.compile(
    r'(input_state:\s*\n\s*\.string\s+)"[1-7]{7}[1-3]{7}"'
)
STATE_PATTERN = re.compile(r"[1-7]{7}[1-3]{7}")


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Measure every distance-11 state with the RV32I solver on "
            "Ripes RV32_ISS."
        )
    )
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--assembly", type=Path, required=True)
    parser.add_argument("--ripes", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--limit", type=int, default=50_000_000)
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--timeout-ms", type=int, default=120_000)
    parser.add_argument(
        "--no-resume",
        action="store_true",
        help="discard prior results for the current source",
    )
    return parser.parse_args()


def load_states(solver):
    completed = subprocess.run(
        [str(solver.resolve()), "--list-distance-11"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )

    if completed.returncode != 0:
        raise RuntimeError(
            "distance-11 enumeration failed:\n" + completed.stderr
        )

    states = completed.stdout.splitlines()

    if len(states) != EXPECTED_STATE_COUNT:
        raise RuntimeError(
            f"expected {EXPECTED_STATE_COUNT} distance-11 states, "
            f"got {len(states)}"
        )

    if len(set(states)) != len(states):
        raise RuntimeError("distance-11 enumeration contains duplicates")

    for state in states:
        if not STATE_PATTERN.fullmatch(state):
            raise RuntimeError(f"invalid enumerated state: {state!r}")

    return states


def extract_last_integer(output, patterns, name):
    values = []

    for pattern in patterns:
        values.extend(re.findall(pattern, output, flags=re.IGNORECASE))

    if not values:
        raise RuntimeError(f"Ripes output does not contain {name}:\n{output}")

    return int(values[-1].replace(",", ""))


def parse_ripes_output(output):
    instructions = extract_last_integer(
        output,
        (
            r"([0-9][0-9,]*)\s+instructions retired",
            r'"#\s*instructions retired"\s*:\s*([0-9][0-9,]*)',
            r'"instructions retired"\s*:\s*([0-9][0-9,]*)',
            r'"instructions_retired"\s*:\s*([0-9][0-9,]*)',
        ),
        "retired instruction count",
    )
    x30 = extract_last_integer(
        output, (r'"x30"\s*:\s*([0-9]+)',), "x30"
    )
    x31 = extract_last_integer(
        output, (r'"x31"\s*:\s*([0-9]+)',), "x31"
    )
    return instructions, x30, x31


def source_for_state(template, state):
    source, replacements = INPUT_PATTERN.subn(
        lambda match: match.group(1) + f'"{state}"', template, count=1
    )

    if replacements != 1:
        raise RuntimeError(
            "could not replace exactly one input_state string in assembly"
        )

    return source


def measure_state(state, template, temporary_directory, args):
    source_path = Path(temporary_directory) / f"{state}.s"
    source_path.write_text(source_for_state(template, state), encoding="utf-8")

    environment = os.environ.copy()
    environment["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    command = [
        str(args.ripes.resolve()),
        "--mode",
        "cli",
        "--src",
        str(source_path),
        "-t",
        "asm",
        "--proc",
        "RV32_ISS",
        "--timeout",
        str(args.timeout_ms),
        "--iret",
        "--regs",
        "--json",
    ]

    try:
        completed = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
            env=environment,
            timeout=args.timeout_ms / 1000.0 + 30.0,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"Ripes timed out for {state}") from error
    finally:
        source_path.unlink(missing_ok=True)

    if completed.returncode != 0:
        raise RuntimeError(
            f"Ripes failed for {state} with status {completed.returncode}:\n"
            + completed.stdout
        )

    instructions, x30, x31 = parse_ripes_output(completed.stdout)

    if x30 != EXPECTED_LENGTH or x31 != 1:
        status = "wrong-result"
    elif instructions > args.limit:
        status = "over-limit"
    else:
        status = "pass"

    return {
        "state": state,
        "instructions": instructions,
        "x30": x30,
        "x31": x31,
        "status": status,
    }


def load_cached_results(path, source_hash, limit):
    results = {}

    if not path.exists():
        return results

    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)

        if tuple(reader.fieldnames or ()) != CSV_FIELDS:
            raise RuntimeError(f"unexpected CSV header in {path}")

        for row in reader:
            if row["source_sha256"] != source_hash:
                continue

            state = row["state"]

            if not STATE_PATTERN.fullmatch(state):
                continue

            instructions = int(row["instructions"])
            x30 = int(row["x30"])
            x31 = int(row["x31"])

            if x30 != EXPECTED_LENGTH or x31 != 1:
                status = "wrong-result"
            elif instructions > limit:
                status = "over-limit"
            else:
                status = "pass"

            results[state] = {
                "state": state,
                "instructions": instructions,
                "x30": x30,
                "x31": x31,
                "status": status,
            }

    return results


def print_result(prefix, completed_count, total, result, maximum):
    max_instructions, max_state = maximum
    print(
        f"[{prefix} {completed_count}/{total}] {result['state']} "
        f"instructions={result['instructions']} status={result['status']} "
        f"max={max_instructions} max_state={max_state}",
        flush=True,
    )


def summarize(states, results, limit):
    measured = [results[state] for state in states if state in results]

    if not measured:
        return 1

    maximum = max(measured, key=lambda result: result["instructions"])
    minimum = min(measured, key=lambda result: result["instructions"])
    average = sum(result["instructions"] for result in measured) // len(measured)
    failures = [result for result in measured if result["status"] != "pass"]

    print()
    print(f"states measured: {len(measured)}/{len(states)}")
    print(
        f"minimum instructions: {minimum['instructions']} "
        f"({minimum['state']})"
    )
    print(f"average instructions: {average}")
    print(
        f"maximum instructions: {maximum['instructions']} "
        f"({maximum['state']})"
    )
    print(f"instruction limit: {limit}")
    print(f"failed states: {len(failures)}")

    return 0 if len(measured) == len(states) and not failures else 1


def main():
    args = parse_args()

    if args.limit <= 0 or args.jobs <= 0 or args.timeout_ms <= 0:
        raise RuntimeError("limit, jobs, and timeout must be positive")

    states = load_states(args.solver)
    template_bytes = args.assembly.read_bytes()
    template = template_bytes.decode("utf-8")
    source_hash = hashlib.sha256(template_bytes).hexdigest()

    args.results.parent.mkdir(parents=True, exist_ok=True)

    if args.no_resume and args.results.exists():
        args.results.unlink()

    results = load_cached_results(args.results, source_hash, args.limit)
    results = {state: results[state] for state in states if state in results}
    pending = [state for state in states if state not in results]
    maximum = (0, "-")
    completed_count = 0

    print(
        f"distance-11 states: {len(states)}, cached: {len(results)}, "
        f"pending: {len(pending)}, jobs: {args.jobs}, limit: {args.limit}",
        flush=True,
    )

    for state in states:
        if state not in results:
            continue

        result = results[state]
        completed_count += 1

        if result["instructions"] > maximum[0]:
            maximum = (result["instructions"], state)

        print_result("cached", completed_count, len(states), result, maximum)

    new_file = not args.results.exists()

    with args.results.open("a", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)

        if new_file:
            writer.writeheader()
            stream.flush()

        def store_result(result):
            nonlocal completed_count, maximum

            result["source_sha256"] = source_hash
            writer.writerow(result)
            stream.flush()
            results[result["state"]] = result
            completed_count += 1

            if result["instructions"] > maximum[0]:
                maximum = (result["instructions"], result["state"])

            print_result(
                "tested", completed_count, len(states), result, maximum
            )

        with tempfile.TemporaryDirectory(
            prefix="minirubik-distance11-"
        ) as temporary_directory:
            if args.jobs == 1:
                for state in pending:
                    try:
                        result = measure_state(
                            state, template, temporary_directory, args
                        )
                    except Exception as error:
                        raise RuntimeError(
                            f"measurement failed for {state}: {error}"
                        ) from error

                    store_result(result)
            else:
                executor = ThreadPoolExecutor(max_workers=args.jobs)
                future_to_state = {
                    executor.submit(
                        measure_state,
                        state,
                        template,
                        temporary_directory,
                        args,
                    ): state
                    for state in pending
                }

                try:
                    for future in as_completed(future_to_state):
                        state = future_to_state[future]

                        try:
                            result = future.result()
                        except Exception as error:
                            raise RuntimeError(
                                f"measurement failed for {state}: {error}"
                            ) from error

                        store_result(result)
                finally:
                    executor.shutdown(wait=True, cancel_futures=True)

    return summarize(states, results, args.limit)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, UnicodeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(2)
