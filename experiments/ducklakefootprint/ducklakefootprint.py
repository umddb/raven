#!/usr/bin/env python3
"""
DuckLake Footprint Experiment Script

This script benchmarks metadata storage overhead in PostgreSQL when using DuckDB + DuckLake.
For a given number of integer columns, it creates 'table1' and inserts N rows (default: 100,000)
such that each insert produces exactly one parquet file and commits immediately.
It verifies that DuckDB data inlining is disabled and DuckLake tracks file-level statistics.
"""

import argparse
import glob
import os
import random
import sys
import time
from pathlib import Path

# Force unbuffered/line-buffered stdout and stderr so logs appear immediately even when piped (e.g. through tee)
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(line_buffering=True)
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(line_buffering=True)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run DuckDB + DuckLake footprint experiment with PostgreSQL metadata catalog."
    )
    parser.add_argument(
        "columns",
        type=int,
        nargs="?",
        default=None,
        help="Number of integer columns for table1 (positional).",
    )
    parser.add_argument(
        "-c",
        "--columns",
        dest="flag_columns",
        type=int,
        default=None,
        help="Number of integer columns for table1 (named flag).",
    )
    parser.add_argument(
        "-n",
        "--rows",
        type=int,
        default=100000,
        help="Number of randomly generated rows to insert (default: 100000).",
    )
    parser.add_argument(
        "--db-name",
        type=str,
        default="ducklakedb",
        help="PostgreSQL catalog database name (default: ducklakedb).",
    )
    parser.add_argument(
        "--db-host",
        type=str,
        default="127.0.0.1",
        help="PostgreSQL host (default: 127.0.0.1).",
    )
    parser.add_argument(
        "--db-port",
        type=int,
        default=5432,
        help="PostgreSQL port (default: 5432).",
    )
    parser.add_argument(
        "--db-user",
        type=str,
        default="postgres",
        help="PostgreSQL user (default: postgres).",
    )
    parser.add_argument(
        "--db-password",
        type=str,
        default="",
        help="PostgreSQL password (default: empty).",
    )
    parser.add_argument(
        "--data-path",
        type=str,
        default="/mydata/ducklake_data" if os.path.isdir("/mydata") else "/tmp/ducklake_data",
        help="Directory path where Parquet data files will be written (default: /mydata/ducklake_data if /mydata exists, else /tmp/ducklake_data).",
    )
    parser.add_argument(
        "--ducklake-dir",
        type=str,
        default="/home/keonwoo/research/ducklake",
        help="Path to local DuckLake repository for fallback extension discovery (default: /home/keonwoo/research/ducklake).",
    )
    parser.add_argument(
        "--generator",
        type=str,
        choices=["sql", "python"],
        default="sql",
        help="Random number generator mechanism: 'sql' (fast DuckDB native random, recommended for >=1000 cols) or 'python' (Python random.randint). Default: sql.",
    )
    parser.add_argument(
        "--log-interval",
        type=int,
        default=10000,
        help="Print progress interval in rows (default: 10000).",
    )
    parser.add_argument(
        "--skip-verify",
        action="store_true",
        help="Skip final verification of file count and statistics.",
    )

    args = parser.parse_args()

    # Determine num_columns from either positional or named flag
    num_cols = args.flag_columns if args.flag_columns is not None else args.columns
    if num_cols is None:
        parser.error("Number of columns must be specified (e.g., `python ducklakefootprint.py 100` or `-c 100`).")
    if num_cols <= 0:
        parser.error("Number of columns must be greater than 0.")
    args.num_columns = num_cols

    return args


def setup_duckdb_and_ducklake(args):
    """
    Initializes DuckDB connection, loads extensions (postgres, ducklake),
    disables data inlining, and attaches to PostgreSQL catalog.
    """
    try:
        import duckdb
    except ImportError:
        print("[ERROR] duckdb Python library is not installed. Run `pip install duckdb`.", file=sys.stderr)
        sys.exit(1)

    print(f"[DuckDB] Initializing DuckDB connection (v{duckdb.__version__})...")
    conn = duckdb.connect()

    # Load PostgreSQL extension
    print("[DuckDB] Installing and loading postgres extension...")
    try:
        conn.execute("INSTALL postgres;")
    except Exception as e:
        print(f"[DuckDB] Notice during INSTALL postgres: {e}")
    try:
        conn.execute("LOAD postgres;")
    except Exception as e:
        print(f"[DuckDB] Notice: LOAD postgres failed ({e}), attempting postgres_scanner...")
        try:
            conn.execute("INSTALL postgres_scanner;")
            conn.execute("LOAD postgres_scanner;")
        except Exception as e2:
            print(f"[ERROR] Could not load postgres or postgres_scanner extension: {e2}", file=sys.stderr)
            sys.exit(1)

    # Load DuckLake extension
    print("[DuckDB] Installing and loading ducklake extension...")
    ducklake_loaded = False
    for install_cmd in ["INSTALL ducklake;", "FORCE INSTALL ducklake FROM core_nightly;"]:
        try:
            conn.execute(install_cmd)
            conn.execute("LOAD ducklake;")
            ducklake_loaded = True
            print(f"[DuckDB] Successfully loaded ducklake using: {install_cmd}")
            break
        except Exception as e:
            print(f"[DuckDB] Notice: {install_cmd} failed: {e}")

    if not ducklake_loaded:
        # Check if local build exists in ducklake repo
        potential_build_paths = [
            os.path.join(args.ducklake_dir, "build/release/extension/ducklake/ducklake.duckdb_extension"),
            os.path.join(args.ducklake_dir, "build/release/ducklake.duckdb_extension"),
        ]
        for path in potential_build_paths:
            if os.path.isfile(path):
                print(f"[DuckDB] Attempting to load locally built extension from: {path}")
                try:
                    conn.execute(f"LOAD '{path}';")
                    ducklake_loaded = True
                    break
                except Exception as e:
                    print(f"[DuckDB] Failed to load local extension: {e}")

    if not ducklake_loaded:
        print("[ERROR] Failed to load ducklake extension in DuckDB. Please ensure ducklake extension is installed or built.", file=sys.stderr)
        sys.exit(1)

    # Clean/prepare Parquet data directory
    os.makedirs(args.data_path, exist_ok=True)

    # 1. Disable data inlining globally in DuckLake before attaching
    print("[DuckLake] Setting ducklake_default_data_inlining_row_limit = 0 to disable inlining...")
    conn.execute("SET ducklake_default_data_inlining_row_limit = 0;")

    # 2. Construct connection string and ATTACH DuckLake database
    # Format: ducklake:postgres:dbname=... host=... port=... user=...
    conn_params = [
        f"dbname={args.db_name}",
        f"host={args.db_host}",
        f"port={args.db_port}",
        f"user={args.db_user}",
    ]
    if args.db_password:
        conn_params.append(f"password={args.db_password}")

    pg_conn_str = " ".join(conn_params)
    data_path_abs = os.path.abspath(args.data_path)
    attach_query = f"ATTACH 'ducklake:postgres:{pg_conn_str}' AS ducklake (DATA_PATH '{data_path_abs}', DATA_INLINING_ROW_LIMIT 0);"

    print(f"[DuckLake] Attaching catalog with query: {attach_query}")
    conn.execute(attach_query)

    # 3. Explicitly set inlining option to 0 on the attached catalog
    try:
        conn.execute("CALL ducklake.set_option('data_inlining_row_limit', 0);")
    except Exception as e:
        print(f"[DuckLake] Notice setting option via CALL: {e}")

    # 4. Verify inlining option & ensure statistics are active
    try:
        options = conn.execute("SELECT option_name, value FROM ducklake.options() WHERE option_name IN ('data_inlining_row_limit', 'skip_stats_columns');").fetchall()
        print(f"[DuckLake] Configured options: {options}")
    except Exception as e:
        print(f"[DuckLake] Notice checking options: {e}")

    return conn


def create_table(conn, num_columns):
    """
    Creates table1 in ducklake with num_columns integer fields.
    """
    print(f"[DuckLake] Creating table 'ducklake.table1' with {num_columns} integer columns...")
    conn.execute("DROP TABLE IF EXISTS ducklake.table1;")

    # Define columns: col0 INT, col1 INT, ..., col{N-1} INT
    col_defs = ", ".join([f"col{i} INTEGER" for i in range(num_columns)])
    create_sql = f"CREATE TABLE ducklake.table1 ({col_defs});"
    conn.execute(create_sql)
    print(f"[DuckLake] Table 'table1' created successfully.")


def insert_rows(conn, num_columns, num_rows, generator_type="sql", log_interval=10000):
    """
    Inserts num_rows randomly generated rows one by one.
    Each insert is executed inside an explicit transaction (BEGIN TRANSACTION; ... COMMIT;)
    and with data inlining disabled, ensuring DuckDB produces exactly one Parquet file per row
    and DuckLake records file-level column statistics in PostgreSQL for every row.
    """
    print(f"[DuckLake] Starting insertion of {num_rows:,} rows ({num_columns} columns each, generator={generator_type})...")
    print(f"[DuckLake] Each insert is committed individually to guarantee 1 Parquet file per row.")

    start_time = time.time()
    last_log_time = start_time

    if generator_type == "sql":
        # Fast native SQL random expression evaluated directly in DuckDB
        # Generates random integers in range [-1,000,000,000, 1,000,000,000]
        select_expr = ", ".join(
            [f"CAST(floor(random() * 2000000000.0 - 1000000000.0) AS INTEGER)" for _ in range(num_columns)]
        )
        insert_stmt = f"INSERT INTO ducklake.table1 SELECT {select_expr};"

        for row_idx in range(1, num_rows + 1):
            conn.execute("BEGIN TRANSACTION;")
            conn.execute(insert_stmt)
            conn.execute("COMMIT;")

            if row_idx % log_interval == 0 or row_idx == num_rows:
                now = time.time()
                elapsed = now - start_time
                rate = row_idx / elapsed if elapsed > 0 else 0
                remaining = (num_rows - row_idx) / rate if rate > 0 else 0
                print(
                    f"  -> Inserted {row_idx:,}/{num_rows:,} rows "
                    f"({row_idx/num_rows*100:.1f}%) | "
                    f"Rate: {rate:.1f} rows/s | "
                    f"Elapsed: {elapsed:.1f}s | "
                    f"ETA: {remaining:.1f}s"
                )
    else:
        # Python random generator with parameterized query
        placeholders = ", ".join(["?"] * num_columns)
        insert_stmt = f"INSERT INTO ducklake.table1 VALUES ({placeholders});"

        for row_idx in range(1, num_rows + 1):
            row_data = [random.randint(-1000000000, 1000000000) for _ in range(num_columns)]
            conn.execute("BEGIN TRANSACTION;")
            conn.execute(insert_stmt, row_data)
            conn.execute("COMMIT;")

            if row_idx % log_interval == 0 or row_idx == num_rows:
                now = time.time()
                elapsed = now - start_time
                rate = row_idx / elapsed if elapsed > 0 else 0
                remaining = (num_rows - row_idx) / rate if rate > 0 else 0
                print(
                    f"  -> Inserted {row_idx:,}/{num_rows:,} rows "
                    f"({row_idx/num_rows*100:.1f}%) | "
                    f"Rate: {rate:.1f} rows/s | "
                    f"Elapsed: {elapsed:.1f}s | "
                    f"ETA: {remaining:.1f}s"
                )

    total_time = time.time() - start_time
    avg_rate = num_rows / total_time if total_time > 0 else 0
    print(f"[DuckLake] Insertion completed in {total_time:.2f}s (Average rate: {avg_rate:.1f} rows/s).")


def verify_experiment(conn, data_path, num_columns, num_rows):
    """
    Verifies that:
    1. Exactly num_rows Parquet data files exist in data_path.
    2. DuckLake table contains exactly num_rows.
    3. File-level statistics exist for all files and columns.
    """
    print("\n" + "=" * 60)
    print("[Verification] Verifying experiment invariants...")
    print("=" * 60)

    # 1. Count parquet files on disk
    parquet_files = glob.glob(os.path.join(data_path, "**", "*.parquet"), recursive=True)
    num_parquet_files = len(parquet_files)
    print(f"[Verification] Parquet data files on disk: {num_parquet_files:,} (Expected: {num_rows:,})")

    # 2. Check row count in table1
    try:
        actual_rows = conn.execute("SELECT count(*) FROM ducklake.table1;").fetchone()[0]
        print(f"[Verification] Table row count: {actual_rows:,} (Expected: {num_rows:,})")
    except Exception as e:
        print(f"[Verification] Could not query table row count: {e}")
        actual_rows = None

    # 3. Check DuckLake data_file records
    try:
        registered_files = conn.execute("SELECT count(*) FROM ducklake.files('table1');").fetchone()[0]
        print(f"[Verification] DuckLake registered data files: {registered_files:,} (Expected: {num_rows:,})")
    except Exception as e:
        print(f"[Verification] Could not query ducklake.files('table1'): {e}")

    # 4. Check file-level statistics tracking in PostgreSQL metadata catalog
    try:
        # Check ducklake_file_column_stats in metadata catalog
        stats_count = conn.execute("SELECT count(*) FROM dl.ducklake_file_column_stats;").fetchone()[0]
        expected_stats = num_rows * num_columns
        print(f"[Verification] File column statistics records: {stats_count:,} (Expected: {expected_stats:,})")
    except Exception:
        try:
            # Query table_column_stats function
            stats_sample = conn.execute("SELECT * FROM ducklake.table_column_stats('table1') LIMIT 5;").fetchall()
            print(f"[Verification] Table column stats sample: {stats_sample}")
        except Exception as e2:
            print(f"[Verification] Notice checking file column stats: {e2}")

    # Summary check
    if num_parquet_files == num_rows:
        print(f"[Verification SUCCESS] Invariant hold: exactly 1 Parquet data file was created per row insert.")
    else:
        print(f"[Verification WARNING] Parquet file count ({num_parquet_files}) does not match row count ({num_rows}).")

    if actual_rows == num_rows:
        print(f"[Verification SUCCESS] Table row count matches inserted count ({num_rows:,}).")
    print("=" * 60 + "\n")


def main():
    args = parse_args()
    print("=" * 70)
    print(" DuckLake Footprint Experiment")
    print(f" Columns:        {args.num_columns}")
    print(f" Rows:           {args.rows:,}")
    print(f" DB Name:        {args.db_name}")
    print(f" DB Host/Port:   {args.db_host}:{args.db_port}")
    print(f" Data Path:      {args.data_path}")
    print(f" Generator:      {args.generator}")
    print("=" * 70)

    conn = setup_duckdb_and_ducklake(args)
    create_table(conn, args.num_columns)
    insert_rows(conn, args.num_columns, args.rows, generator_type=args.generator, log_interval=args.log_interval)

    if not args.skip_verify:
        verify_experiment(conn, args.data_path, args.num_columns, args.rows)

    conn.close()
    print("[DuckLake] Experiment completed successfully.")


if __name__ == "__main__":
    main()
