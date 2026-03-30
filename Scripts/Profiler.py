#!/usr/bin/env python3
import argparse
import bisect
import re
import shutil
import subprocess
from pathlib import Path

PROF_RE = re.compile(
    r"Profiler:\s+\d+\)\s+(0x[0-9a-fA-F]+):\s+(\d+)\s+\((\d+)\s+calls,\s+avg\s+(\d+)\s+per\s+call\)"
)
SYM_RE = re.compile(r"^([0-9a-fA-F]+)\s+\S\s+(.+)$")


def parse_profiler(path: Path):
    rows = []
    for line in path.read_text(errors="replace").splitlines():
        m = PROF_RE.search(line)
        if m:
            addr, total, calls, avg = m.groups()
            rows.append({
                "addr": int(addr, 16),
                "total": int(total),
                "calls": int(calls),
                "avg": int(avg),
            })
    return rows


def parse_symbols(path: Path):
    pairs = []
    for line in path.read_text(errors="replace").splitlines():
        m = SYM_RE.match(line.strip())
        if m:
            addr_hex, name = m.groups()
            pairs.append((int(addr_hex, 16), name))
    pairs.sort()
    return [p[0] for p in pairs], [p[1] for p in pairs]


def lookup_symbol(addr, sym_addrs, sym_names):
    i = bisect.bisect_right(sym_addrs, addr) - 1
    if i < 0:
        return f"0x{addr:x}"
    base = sym_addrs[i]
    name = sym_names[i]
    off = addr - base
    return name if off == 0 else f"{name}+0x{off:x}"


def sanitize_folded_name(name: str):
    return name.replace(";", ":").replace("\n", " ").strip()


def find_flamegraph(explicit: str | None):
    candidates = []
    if explicit:
        candidates.append(Path(explicit))

    script_dir = Path(__file__).resolve().parent
    candidates.extend([
        Path("FlameGraph/flamegraph.pl"),
        script_dir / "FlameGraph" / "flamegraph.pl",
        script_dir / "flamegraph.pl",
        ])

    for p in candidates:
        if p.exists():
            return str(p)

    path_hit = shutil.which("flamegraph.pl")
    if path_hit:
        return path_hit

    return None


def convert_value(value_ticks: int, time_unit: str, tsc_hz: float | None) -> int:
    if time_unit == "ticks":
        return value_ticks

    if tsc_hz is None or tsc_hz <= 0:
        raise SystemExit("--tsc-hz is required and must be > 0 when --time-unit ms is used")

    ms = (value_ticks / tsc_hz) * 1000.0
    return max(1, int(round(ms * 1000.0)))  # flamegraph.pl expects integer weights


def write_folded(rows, sym_addrs, sym_names, out_path: Path, metric: str,
                 time_unit: str, tsc_hz: float | None, prefixes: list[str]):
    kept = 0
    skipped = 0
    prefix_tuple = tuple(prefixes)

    with out_path.open("w") as f:
        for row in rows:
            raw_val = row[metric]
            if raw_val <= 0:
                continue

            sym = sanitize_folded_name(lookup_symbol(row["addr"], sym_addrs, sym_names))

            if prefix_tuple and not sym.startswith(prefix_tuple):
                skipped += 1
                continue

            val = convert_value(raw_val, time_unit, tsc_hz)
            f.write(f"{sym} {val}\n")
            kept += 1

    print(f"Wrote {kept} entries to {out_path} ({skipped} filtered out)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("profiler_log", type=Path, help="Profiler log text file")
    ap.add_argument("symbol_file", type=Path, help="Symbol file")
    ap.add_argument("--metric", choices=["avg", "total"], default="avg",
                    help="Use avg or total from the profiler log")
    ap.add_argument("--time-unit", choices=["ticks", "ms"], default="ticks",
                    help="Output units for graph widths")
    ap.add_argument("--tsc-hz", type=float, default=None,
                    help="TSC frequency in Hz, required for --time-unit ms")
    ap.add_argument("--symbol-prefix", action="append", default=[],
                    help="Only include symbols starting with this prefix; can be passed multiple times")
    ap.add_argument("--kernel-only", action="store_true",
                    help="Shortcut for --symbol-prefix kernel::")
    ap.add_argument("--uacpi-kernel-only", action="store_true",
                    help="Shortcut for --symbol-prefix uacpi_kernel_")
    ap.add_argument("--folded", type=Path, default=Path("profiler.folded"),
                    help="Output folded file")
    ap.add_argument("--svg", type=Path, default=Path("profiler.svg"),
                    help="Output SVG flame graph")
    ap.add_argument("--flamegraph-pl", default=None,
                    help="Path to flamegraph.pl")
    ap.add_argument("--title", default="HorizonOS profiler",
                    help="Base title for the graph")
    ap.add_argument("--no-svg", action="store_true",
                    help="Only write the folded file, skip SVG generation")
    args = ap.parse_args()

    rows = parse_profiler(args.profiler_log)
    if not rows:
        raise SystemExit("No profiler entries found.")

    sym_addrs, sym_names = parse_symbols(args.symbol_file)
    if not sym_addrs:
        raise SystemExit("No symbols found.")

    prefixes = list(args.symbol_prefix)
    if args.kernel_only:
        prefixes.append("kernel::")
    if args.uacpi_kernel_only:
        prefixes.append("uacpi_kernel_")

    write_folded(
        rows,
        sym_addrs,
        sym_names,
        args.folded,
        args.metric,
        args.time_unit,
        args.tsc_hz,
        prefixes,
    )

    if args.no_svg:
        return

    flamegraph = find_flamegraph(args.flamegraph_pl)
    if not flamegraph:
        raise SystemExit(
            "Could not find flamegraph.pl.\n"
            "Install it with:\n"
            "  git clone https://github.com/brendangregg/FlameGraph.git\n"
            "Then rerun with either:\n"
            "  --flamegraph-pl FlameGraph/flamegraph.pl\n"
            "or put flamegraph.pl on your PATH."
        )

    countname = "tsc ticks" if args.time_unit == "ticks" else "ms x1000"
    unit_suffix = "TSC ticks" if args.time_unit == "ticks" else "ms"

    title = f"{args.title} ({args.metric} in {unit_suffix})"
    if prefixes:
        title += " [" + ", ".join(prefixes) + "]"

    cmd = [
        flamegraph,
        "--title", title,
        "--countname", countname,
        str(args.folded),
    ]

    with args.svg.open("w") as out:
        subprocess.run(cmd, stdout=out, check=True)

    print(f"Wrote {args.svg}")


if __name__ == "__main__":
    main()