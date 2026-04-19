#!/usr/bin/env python3
"""
Run the ROSQL query suite against the demo-agent Parquet output.

Usage:
    python3 assert_queries.py --data-dir /data --queries /tests/queries.yml

Exit 0 on full pass, 1 on any failure.
"""

import argparse
import glob
import os
import subprocess
import sys

import yaml


def find_session_dir(data_dir: str) -> str:
    pattern = os.path.join(data_dir, 'robotops_demo_agent', '*')
    sessions = sorted(glob.glob(pattern))
    if not sessions:
        print(f'[ERROR] No demo-agent session found under {data_dir}/robotops_demo_agent/')
        found = glob.glob(os.path.join(data_dir, '**'), recursive=True)[:20]
        print(f'        Files present: {found}')
        sys.exit(1)
    return sessions[-1]


def count_data_rows(output: str) -> int:
    """
    Heuristically count data rows in rosql tabular output.

    rosql prints a header line, a separator line, then data rows, then a footer
    like '(N rows)'. We count lines that are not headers, separators, or footers.
    """
    lines = output.strip().splitlines()
    data_rows = 0
    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue
        # Skip separator lines (all dashes/pluses)
        if all(c in '-+| ' for c in stripped):
            continue
        # Skip footer lines like "(5 rows)" or "(0 rows)"
        if stripped.startswith('(') and stripped.endswith(')'):
            continue
        # Skip header line (first non-separator line is the header)
        if data_rows == -1:
            data_rows = 0
            continue
        data_rows += 1
    return max(0, data_rows - 1)  # subtract the header row


def run_query(sql: str, session_dir: str) -> tuple[int, str, str]:
    """Run a rosql query and return (returncode, stdout, stderr)."""
    result = subprocess.run(
        ['rosql', 'query', sql, '--backend', 'parquet', '--url', session_dir],
        capture_output=True,
        text=True,
        timeout=30,
    )
    return result.returncode, result.stdout, result.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--data-dir', required=True)
    parser.add_argument('--queries', required=True)
    args = parser.parse_args()

    filter_mode = os.environ.get('E2E_FILTER_MODE', '0') == '1'

    with open(args.queries) as f:
        suite = yaml.safe_load(f)

    session_dir = find_session_dir(args.data_dir)
    print(f'[assert] Session: {session_dir}')
    print(f'[assert] Filter mode: {filter_mode}')
    print()

    passes = 0
    failures = 0
    skipped = 0

    for entry in suite['queries']:
        name = entry['name']
        sql = entry['sql']
        expect = entry['expect']
        desc = entry.get('description', '')

        # filter_mode_zero queries only assert ==0 rows when E2E_FILTER_MODE=1;
        # otherwise they are treated as >=0 (parse-only)
        effective_expect = expect
        if expect == 'filter_mode_zero':
            effective_expect = '==0' if filter_mode else '>=0'

        try:
            rc, stdout, stderr = run_query(sql, session_dir)
        except subprocess.TimeoutExpired:
            print(f'[FAIL] {name}: query timed out after 30s')
            print(f'       sql: {sql}')
            failures += 1
            continue

        if rc != 0:
            print(f'[FAIL] {name}: rosql exited {rc}')
            print(f'       sql:    {sql}')
            print(f'       stderr: {stderr.strip()}')
            failures += 1
            continue

        rows = count_data_rows(stdout)

        if effective_expect == '>=1' and rows < 1:
            print(f'[FAIL] {name}: expected >=1 rows, got {rows}')
            print(f'       sql:    {sql}')
            print(f'       desc:   {desc}')
            print(f'       output: {stdout.strip()[:300]}')
            failures += 1
        elif effective_expect == '==0' and rows != 0:
            print(f'[FAIL] {name}: expected ==0 rows (filter mode), got {rows}')
            print(f'       sql: {sql}')
            failures += 1
        else:
            status = 'PASS'
            print(f'[{status}] {name:<40} rows={rows:>4}   expect={effective_expect}   {desc}')
            passes += 1

    print()
    print(f'Results: {passes} passed, {failures} failed, {skipped} skipped')

    if failures > 0:
        sys.exit(1)


if __name__ == '__main__':
    main()
