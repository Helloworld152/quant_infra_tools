#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import csv
import io
import re
from collections import OrderedDict
from dataclasses import dataclass
from decimal import Decimal
from pathlib import Path


OPTION_PRODUCT_TYPES = {"2", "6"}
USAGE_TEXT = """正确用法:
  build-meta:
    python3 instrument_meta_tool.py build-meta --csv <原始CSV> --output <product_meta.csv>

  compare:
    python3 instrument_meta_tool.py compare --reference <参考文件> --meta <product_meta.csv> --report <差异报告>

  build-and-compare:
    python3 instrument_meta_tool.py build-and-compare --csv <原始CSV> --reference <参考文件> --output <product_meta.csv> --report <差异报告>
"""


@dataclass(frozen=True)
class MetaRow:
    key: str
    multiplier: str
    tick: str


class FriendlyArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.print_usage()
        print(f"\n参数错误: {message}\n")
        print(USAGE_TEXT)
        raise SystemExit(2)


def read_csv_text(path: Path) -> str:
    raw = path.read_bytes()
    return raw.decode("utf-8", errors="ignore").replace("\x00", "")


def format_decimal(value: str) -> str:
    num = Decimal(value.strip())
    if num == num.to_integral():
        return str(num.quantize(Decimal("1.0")))
    return format(num.normalize(), "f")


def normalize_decimal(value: str) -> str:
    return format(Decimal(value.strip()).normalize(), "f")


def future_key(contract_code: str) -> str | None:
    match = re.match(r"^([A-Za-z]+)\d+", contract_code)
    return match.group(1) if match else None


def option_key(contract_code: str) -> str | None:
    dashed = re.match(r"^([A-Za-z]+)\d+-([CP])-\d+$", contract_code)
    if dashed:
        suffix = "call" if dashed.group(2) == "C" else "put"
        return f"{dashed.group(1)}{suffix}"

    compact = re.match(r"^([A-Za-z]+)\d+([CP])\d+$", contract_code)
    if compact:
        suffix = "call" if compact.group(2) == "C" else "put"
        return f"{compact.group(1)}{suffix}"

    return None


def build_meta_rows(csv_path: Path) -> OrderedDict[str, MetaRow]:
    reader = csv.DictReader(io.StringIO(read_csv_text(csv_path)))
    result: OrderedDict[str, MetaRow] = OrderedDict()

    for row in reader:
        contract_code = (row.get("合约代码") or "").strip()
        product_type = (row.get("产品类型") or "").strip()
        multiplier = (row.get("合约数量乘数") or "").strip()
        tick = (row.get("最小变动价位") or "").strip()
        if not contract_code or not multiplier or not tick:
            continue

        key = None
        if product_type == "1":
            key = future_key(contract_code)
        elif product_type in OPTION_PRODUCT_TYPES:
            key = option_key(contract_code)

        if not key or key in result:
            continue

        result[key] = MetaRow(
            key=key,
            multiplier=format_decimal(multiplier),
            tick=format_decimal(tick),
        )

    return result


def write_meta(rows: OrderedDict[str, MetaRow], output_path: Path) -> None:
    with output_path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.writer(fh)
        for row in rows.values():
            writer.writerow([row.key, row.multiplier, row.tick])


def read_meta_csv(path: Path) -> dict[str, tuple[str, str]]:
    result: dict[str, tuple[str, str]] = {}
    with path.open("r", encoding="utf-8", newline="") as fh:
        for row in csv.reader(fh):
            if len(row) < 3:
                continue
            result[row[0].strip()] = (
                normalize_decimal(row[1]),
                normalize_decimal(row[2]),
            )
    return result


def read_reference_file(path: Path) -> dict[str, tuple[str, str]]:
    result: dict[str, tuple[str, str]] = {}
    with path.open("r", encoding="utf-8", errors="ignore") as fh:
        for raw_line in fh:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            left = line.split("|", 1)[0].strip()
            parts = [part.strip() for part in left.split(",")]
            if len(parts) < 3:
                continue
            result[parts[0]] = (
                normalize_decimal(parts[1]),
                normalize_decimal(parts[2]),
            )
    return result


def build_compare_report(
    reference: dict[str, tuple[str, str]],
    current: dict[str, tuple[str, str]],
) -> tuple[list[str], list[str], list[str]]:
    mismatch: list[str] = []
    missing_in_current: list[str] = []
    missing_in_reference: list[str] = []

    for key in sorted(reference):
        if key not in current:
            missing_in_current.append(key)
            continue
        if reference[key] != current[key]:
            ref_multiplier, ref_tick = reference[key]
            cur_multiplier, cur_tick = current[key]
            mismatch.append(
                f"{key}: auto={ref_multiplier},{ref_tick} csv={cur_multiplier},{cur_tick}"
            )

    for key in sorted(current):
        if key not in reference:
            missing_in_reference.append(key)

    return mismatch, missing_in_current, missing_in_reference


def write_compare_report(
    report_path: Path,
    mismatch: list[str],
    missing_in_current: list[str],
    missing_in_reference: list[str],
) -> None:
    lines = ["不一致项（合约乘数或最小变动单位）"]
    lines.extend(mismatch or ["无"])
    lines.append("")
    lines.append("参考文件中存在，但 product_meta.csv 中不存在")
    lines.extend(missing_in_current or ["无"])
    lines.append("")
    lines.append("product_meta.csv 中存在，但参考文件中不存在")
    lines.extend(missing_in_reference or ["无"])
    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def cmd_build_meta(args: argparse.Namespace) -> int:
    rows = build_meta_rows(args.csv)
    write_meta(rows, args.output)
    print(f"生成完成: {args.output}")
    print(f"条数: {len(rows)}")
    return 0


def cmd_compare(args: argparse.Namespace) -> int:
    reference = read_reference_file(args.reference)
    current = read_meta_csv(args.meta)
    mismatch, missing_in_current, missing_in_reference = build_compare_report(
        reference, current
    )
    write_compare_report(
        args.report, mismatch, missing_in_current, missing_in_reference
    )
    print(f"报告已生成: {args.report}")
    print(f"数值不一致: {len(mismatch)}")
    print(f"参考文件缺失于 meta: {len(missing_in_current)}")
    print(f"meta 多出于参考文件: {len(missing_in_reference)}")
    return 0


def cmd_build_and_compare(args: argparse.Namespace) -> int:
    rows = build_meta_rows(args.csv)
    write_meta(rows, args.output)
    reference = read_reference_file(args.reference)
    current = {
        row.key: (normalize_decimal(row.multiplier), normalize_decimal(row.tick))
        for row in rows.values()
    }
    mismatch, missing_in_current, missing_in_reference = build_compare_report(
        reference, current
    )
    write_compare_report(
        args.report, mismatch, missing_in_current, missing_in_reference
    )
    print(f"生成完成: {args.output}")
    print(f"报告已生成: {args.report}")
    print(f"条数: {len(rows)}")
    print(f"数值不一致: {len(mismatch)}")
    print(f"参考文件缺失于 meta: {len(missing_in_current)}")
    print(f"meta 多出于参考文件: {len(missing_in_reference)}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = FriendlyArgumentParser(
        description="从 CTP 合约 CSV 提取 product meta，并与参考文件做对比。",
        epilog=USAGE_TEXT,
        formatter_class=argparse.RawTextHelpFormatter,
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_meta_parser = subparsers.add_parser(
        "build-meta", help="从原始 CSV 生成 product_meta.csv"
    )
    build_meta_parser.add_argument("--csv", type=Path, required=True, help="原始合约 CSV")
    build_meta_parser.add_argument(
        "--output", type=Path, required=True, help="输出的 product meta CSV"
    )
    build_meta_parser.set_defaults(func=cmd_build_meta)

    compare_parser = subparsers.add_parser(
        "compare", help="对比参考文件与 product_meta.csv"
    )
    compare_parser.add_argument("--reference", type=Path, required=True, help="参考文件路径")
    compare_parser.add_argument("--meta", type=Path, required=True, help="product meta CSV 路径")
    compare_parser.add_argument("--report", type=Path, required=True, help="差异报告输出路径")
    compare_parser.set_defaults(func=cmd_compare)

    build_and_compare_parser = subparsers.add_parser(
        "build-and-compare", help="先生成 product meta，再直接对比参考文件"
    )
    build_and_compare_parser.add_argument(
        "--csv", type=Path, required=True, help="原始合约 CSV"
    )
    build_and_compare_parser.add_argument(
        "--reference", type=Path, required=True, help="参考文件路径"
    )
    build_and_compare_parser.add_argument(
        "--output", type=Path, required=True, help="输出的 product meta CSV"
    )
    build_and_compare_parser.add_argument(
        "--report", type=Path, required=True, help="差异报告输出路径"
    )
    build_and_compare_parser.set_defaults(func=cmd_build_and_compare)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
