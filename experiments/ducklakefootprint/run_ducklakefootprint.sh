#!/usr/bin/env bash
# ==============================================================================
# DuckLake Footprint Benchmark Runner Script
#
# This script runs the DuckDB + DuckLake metadata footprint experiment across
# different column counts (10, 100, 1000, 10000).
#
# For each column count:
#   1. Installs prerequisites (PostgreSQL, DuckDB, Python dependencies) on Ubuntu 22.
#   2. Creates a brand new, isolated PostgreSQL cluster.
#   3. Runs ducklakefootprint.py (100,000 row inserts, 1 row per parquet file).
#   4. Measures PostgreSQL database storage size (excluding WAL/server logs) in bytes.
#   5. Appends the footprint result to footprint_result.txt.
#   6. Completely destroys the PostgreSQL instance and temporary data files.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULT_FILE="${SCRIPT_DIR}/footprint_result.txt"
PYTHON_SCRIPT="${SCRIPT_DIR}/ducklakefootprint.py"

# Default configuration
BASE_DATA_DIR="/mydata"
COLUMN_COUNTS=(10 100 1000 10000)
NUM_ROWS=100000
PG_PORT=5433
PG_USER="postgres"
DB_NAME="ducklakedb"
UNINSTALL_PG_AT_END=false
GENERATOR="sql"

# ------------------------------------------------------------------------------
# Usage / Help
# ------------------------------------------------------------------------------
usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Options:
  -d, --data-dir DIR           Base directory for PostgreSQL data and Parquet files (default: /mydata)
  -c, --columns "10 100 ..."   Space-separated list of column counts (default: "10 100 1000 10000")
  -n, --rows NUM               Number of rows to insert per test (default: 100000)
  -p, --port PORT              Port for temporary PostgreSQL instance (default: 5433)
  -g, --generator [sql|python] Random generator to use: 'sql' or 'python' (default: sql)
  --uninstall-pg               Uninstall PostgreSQL packages when completely finished
  -h, --help                   Show this help message
EOF
    exit 0
}

# Parse command line options
while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--data-dir)
            BASE_DATA_DIR="$2"
            shift 2
            ;;
        -c|--columns)
            # shellcheck disable=SC2206
            COLUMN_COUNTS=($2)
            shift 2
            ;;
        -n|--rows)
            NUM_ROWS="$2"
            shift 2
            ;;
        -p|--port)
            PG_PORT="$2"
            shift 2
            ;;
        -g|--generator)
            GENERATOR="$2"
            shift 2
            ;;
        --uninstall-pg)
            UNINSTALL_PG_AT_END=true
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "[ERROR] Unknown option: $1" >&2
            usage
            ;;
    esac
done

echo "================================================================================"
echo " Starting DuckLake Footprint Benchmark Suite"
echo " Base Data Dir:    ${BASE_DATA_DIR}"
echo " Target Columns:   ${COLUMN_COUNTS[*]}"
echo " Rows per run:     ${NUM_ROWS}"
echo " Temporary Port:   ${PG_PORT}"
echo " Results output:   ${RESULT_FILE}"
echo "================================================================================"

# ------------------------------------------------------------------------------
# 1. Environment & Dependency Setup (Ubuntu 22)
# ------------------------------------------------------------------------------
setup_prerequisites() {
    echo "[Setup] Checking and installing prerequisites for Ubuntu 22..."

    # Ensure apt packages are installed
    local NEED_APT=false
    if ! command -v initdb >/dev/null 2>&1 && ! ls /usr/lib/postgresql/*/bin/initdb >/dev/null 2>&1; then
        NEED_APT=true
    fi
    if ! command -v python3 >/dev/null 2>&1 || ! command -v pip3 >/dev/null 2>&1; then
        NEED_APT=true
    fi

    if [ "$NEED_APT" = true ]; then
        echo "[Setup] Installing PostgreSQL and Python dependencies via apt..."
        if [ "$(id -u)" -eq 0 ]; then
            apt-get update -qq
            DEBIAN_FRONTEND=noninteractive apt-get install -y -qq postgresql postgresql-contrib libpq-dev python3 python3-pip python3-dev
        elif command -v sudo >/dev/null 2>&1; then
            sudo apt-get update -qq
            sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq postgresql postgresql-contrib libpq-dev python3 python3-pip python3-dev
        else
            echo "[WARNING] Neither root nor sudo available. Assuming tools are pre-installed."
        fi
    fi

    # Locate PostgreSQL binaries (Ubuntu 22 places them in /usr/lib/postgresql/<ver>/bin)
    if ! command -v initdb >/dev/null 2>&1; then
        local PG_BIN
        PG_BIN=$(ls -d /usr/lib/postgresql/*/bin 2>/dev/null | sort -V | tail -n 1)
        if [ -n "$PG_BIN" ] && [ -d "$PG_BIN" ]; then
            echo "[Setup] Adding PostgreSQL bin directory to PATH: ${PG_BIN}"
            export PATH="${PG_BIN}:${PATH}"
        fi
    fi

    # Verify PostgreSQL binaries
    if ! command -v initdb >/dev/null 2>&1 || ! command -v pg_ctl >/dev/null 2>&1; then
        echo "[ERROR] 'initdb' or 'pg_ctl' not found in PATH." >&2
        exit 1
    fi

    # Ensure duckdb python package is installed
    if ! python3 -c "import duckdb" >/dev/null 2>&1; then
        echo "[Setup] Installing duckdb Python package..."
        pip3 install duckdb --quiet
    fi

    # Prepare base data directory (e.g., /mydata on CloudLab)
    if [ ! -d "${BASE_DATA_DIR}" ]; then
        echo "[Setup] Creating base data directory at ${BASE_DATA_DIR}..."
        if [ "$(id -u)" -eq 0 ]; then
            mkdir -p "${BASE_DATA_DIR}"
        elif command -v sudo >/dev/null 2>&1; then
            sudo mkdir -p "${BASE_DATA_DIR}"
            sudo chown -R "$(id -u):$(id -g)" "${BASE_DATA_DIR}"
        else
            mkdir -p "${BASE_DATA_DIR}" 2>/dev/null || true
        fi
    fi

    if [ -d "${BASE_DATA_DIR}" ]; then
        if [ ! -w "${BASE_DATA_DIR}" ]; then
            echo "[Setup] Ensuring write permissions on ${BASE_DATA_DIR}..."
            if command -v sudo >/dev/null 2>&1; then
                sudo chown -R "$(id -u):$(id -g)" "${BASE_DATA_DIR}"
            fi
        fi
    else
        echo "[WARNING] Base data directory '${BASE_DATA_DIR}' could not be created/accessed. Falling back to /tmp."
        BASE_DATA_DIR="/tmp"
    fi

    echo "[Setup] Prerequisites ready. Python: $(python3 -V), DuckDB: $(python3 -c 'import duckdb; print(duckdb.__version__)'), PostgreSQL: $(initdb --version), Data Dir: ${BASE_DATA_DIR}"
}

# ------------------------------------------------------------------------------
# Pre-Experiment Cleanup
# ------------------------------------------------------------------------------
cleanup_pre_experiment() {
    echo "[Cleanup] Performing pre-experiment cleanup of stale processes and files..."
    # 1. Stop any stale postgres processes running on PG_PORT or associated with ducklake clusters
    if pgrep -f "postgres.*-p ${PG_PORT}" >/dev/null 2>&1; then
        echo "[Cleanup] Stopping lingering PostgreSQL processes on port ${PG_PORT}..."
        pkill -f "postgres.*-p ${PG_PORT}" 2>/dev/null || true
        sleep 1
    fi

    # Check for any postgres processes running on PGDATA directories in BASE_DATA_DIR
    for d in "${BASE_DATA_DIR}"/pgdata_ducklake_*; do
        if [ -d "$d" ]; then
            echo "[Cleanup] Stopping PostgreSQL cluster at $d..."
            pg_ctl -D "$d" -m immediate stop 2>/dev/null || true
        fi
    done

    # 2. Clean up old temporary cluster directories and Parquet data folders under BASE_DATA_DIR
    echo "[Cleanup] Removing stale pgdata and parquet directories under ${BASE_DATA_DIR}..."
    rm -rf "${BASE_DATA_DIR}"/pgdata_ducklake_* 2>/dev/null || true
    rm -rf "${BASE_DATA_DIR}"/ducklake_data_* 2>/dev/null || true

    # Clean up stale unix sockets in /tmp if any
    rm -f "/tmp/.s.PGSQL.${PG_PORT}"* 2>/dev/null || true

    echo "[Cleanup] Pre-experiment cleanup finished."
}

# ------------------------------------------------------------------------------
# Initialize Results File
# ------------------------------------------------------------------------------
init_results_file() {
    mkdir -p "$(dirname "${RESULT_FILE}")"
    if [ ! -f "${RESULT_FILE}" ]; then
        cat <<EOF > "${RESULT_FILE}"
# ==============================================================================
# DuckLake PostgreSQL Metadata Footprint Benchmark
# Generated: $(date -u +"%Y-%m-%d %H:%M:%SZ")
# Benchmark: 1 row per Parquet file (data inlining disabled, file stats enabled)
# Total rows per run: ${NUM_ROWS}
# ==============================================================================
columns,rows,postgres_data_storage_bytes,human_readable_size
EOF
    fi
}

# ------------------------------------------------------------------------------
# Run Benchmark for a Single Column Count
# ------------------------------------------------------------------------------
run_single_experiment() {
    local COLS="$1"
    echo ""
    echo "================================================================================"
    echo ">>> Starting Experiment: ${COLS} Columns, ${NUM_ROWS} Rows <<<"
    echo "================================================================================"

    local PGDATA_DIR
    PGDATA_DIR=$(mktemp -d -p "${BASE_DATA_DIR}" pgdata_ducklake_${COLS}cols_XXXXXX)
    local DATA_PATH="${BASE_DATA_DIR}/ducklake_data_${COLS}cols_$$"
    rm -rf "${DATA_PATH}"
    mkdir -p "${DATA_PATH}"

    # Trap INT/TERM signals to ensure PostgreSQL and temporary data get cleaned up if interrupted
    cleanup_experiment() {
        echo ""
        echo "[Interrupt] Caught signal! Stopping PostgreSQL and cleaning up temporary directories..."
        pg_ctl -D "${PGDATA_DIR}" -m immediate stop 2>/dev/null || true
        rm -rf "${PGDATA_DIR}" "${DATA_PATH}"
        echo "[Interrupt] Cleanup complete."
        exit 130
    }
    trap cleanup_experiment INT TERM

    echo "[PostgreSQL] Creating fresh isolated PostgreSQL cluster at: ${PGDATA_DIR}..."
    chmod 700 "${PGDATA_DIR}"
    initdb -D "${PGDATA_DIR}" \
           --auth-local=trust \
           --auth-host=trust \
           --username="${PG_USER}" \
           --no-instructions > /dev/null

    # Configure postgresql.conf for isolated benchmark
    cat <<EOF >> "${PGDATA_DIR}/postgresql.conf"
listen_addresses = '127.0.0.1'
port = ${PG_PORT}
unix_socket_directories = '/tmp'
max_connections = 100
shared_buffers = 1GB
work_mem = 64MB
# Disable sync commits and fsync for rapid benchmarking without modifying table data storage
synchronous_commit = off
fsync = off
checkpoint_timeout = 1h
max_wal_size = 20GB
EOF

    echo "[PostgreSQL] Starting PostgreSQL server on port ${PG_PORT}..."
    if ! pg_ctl -D "${PGDATA_DIR}" -l "${PGDATA_DIR}/postgresql.log" -o "-p ${PG_PORT}" -w start; then
        echo "[ERROR] pg_ctl failed to start server. Dumping ${PGDATA_DIR}/postgresql.log:" >&2
        cat "${PGDATA_DIR}/postgresql.log" >&2 || true
        exit 1
    fi

    # Wait for PostgreSQL to be ready
    local RETRIES=30
    until pg_isready -h 127.0.0.1 -p "${PG_PORT}" -U "${PG_USER}" >/dev/null 2>&1 || [ $RETRIES -eq 0 ]; do
        sleep 1
        RETRIES=$((RETRIES - 1))
    done

    if [ $RETRIES -eq 0 ]; then
        echo "[ERROR] PostgreSQL server failed to start in time. Check log: ${PGDATA_DIR}/postgresql.log" >&2
        cat "${PGDATA_DIR}/postgresql.log" >&2
        pg_ctl -D "${PGDATA_DIR}" -m immediate stop || true
        rm -rf "${PGDATA_DIR}" "${DATA_PATH}"
        exit 1
    fi

    echo "[PostgreSQL] Creating database '${DB_NAME}'..."
    createdb -h 127.0.0.1 -p "${PG_PORT}" -U "${PG_USER}" "${DB_NAME}"

    echo "[Experiment] Running ducklakefootprint.py for ${COLS} columns..."
    PYTHONUNBUFFERED=1 python3 -u "${PYTHON_SCRIPT}" \
        --columns "${COLS}" \
        --rows "${NUM_ROWS}" \
        --db-name "${DB_NAME}" \
        --db-port "${PG_PORT}" \
        --db-user "${PG_USER}" \
        --data-path "${DATA_PATH}" \
        --generator "${GENERATOR}"

    # Measure total footprint of PostgreSQL database (excluding WAL and server logs)
    echo "[Measurement] Measuring PostgreSQL data footprint for '${DB_NAME}'..."
    local SIZE_BYTES
    SIZE_BYTES=$(psql -h 127.0.0.1 -p "${PG_PORT}" -U "${PG_USER}" -d postgres -t -A -c "SELECT pg_database_size('${DB_NAME}');")
    local SIZE_PRETTY
    SIZE_PRETTY=$(psql -h 127.0.0.1 -p "${PG_PORT}" -U "${PG_USER}" -d postgres -t -A -c "SELECT pg_size_pretty(pg_database_size('${DB_NAME}'));")

    echo "--------------------------------------------------------------------------------"
    echo " Result for ${COLS} Columns:"
    echo "   Database Storage Footprint: ${SIZE_BYTES} bytes (${SIZE_PRETTY})"
    echo "--------------------------------------------------------------------------------"

    # Per-table size breakdown in PostgreSQL
    echo "[Measurement] Table-level storage breakdown:"
    psql -h 127.0.0.1 -p "${PG_PORT}" -U "${PG_USER}" -d "${DB_NAME}" -c "
        SELECT relname AS table_name,
               pg_total_relation_size(c.oid) AS total_bytes,
               pg_size_pretty(pg_total_relation_size(c.oid)) AS pretty_size
        FROM pg_class c
        JOIN pg_namespace n ON n.oid = c.relnamespace
        WHERE nspname = 'public'
        ORDER BY pg_total_relation_size(c.oid) DESC;
    "

    # Record result to footprint_result.txt
    echo "${COLS},${NUM_ROWS},${SIZE_BYTES},${SIZE_PRETTY}" >> "${RESULT_FILE}"
    echo "[Result] Appended result to ${RESULT_FILE}"

    # Destroy the PostgreSQL instance and data
    echo "[Teardown] Stopping and destroying PostgreSQL instance at ${PGDATA_DIR}..."
    trap - INT TERM
    pg_ctl -D "${PGDATA_DIR}" -m immediate stop
    rm -rf "${PGDATA_DIR}"
    rm -rf "${DATA_PATH}"
    echo "[Teardown] Cleaned up PostgreSQL instance and Parquet data files successfully."
}

# ------------------------------------------------------------------------------
# Main Execution Flow
# ------------------------------------------------------------------------------
setup_prerequisites
cleanup_pre_experiment
init_results_file

for COLS in "${COLUMN_COUNTS[@]}"; do
    run_single_experiment "${COLS}"
done

echo ""
echo "================================================================================"
echo " All experiments finished!"
echo " Results Summary from ${RESULT_FILE}:"
echo "================================================================================"
cat "${RESULT_FILE}"

if [ "${UNINSTALL_PG_AT_END}" = true ]; then
    echo "[Teardown] Uninstalling PostgreSQL packages as requested..."
    if [ "$(id -u)" -eq 0 ]; then
        apt-get remove --purge -y postgresql postgresql-contrib
    elif command -v sudo >/dev/null 2>&1; then
        sudo apt-get remove --purge -y postgresql postgresql-contrib
    fi
fi
echo "================================================================================"
