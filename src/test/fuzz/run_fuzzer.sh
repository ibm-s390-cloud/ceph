#!/bin/bash
# Helper script to run fuzzing tests
# Copyright (C) 2024 IBM Corporation

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/../../../build}"

# Default values
FUZZER=""
DURATION=3600
JOBS=4
WORKERS=4
MAX_LEN=65536
TIMEOUT=10
RSS_LIMIT=2048

usage() {
    cat << EOF
Usage: $0 [OPTIONS] <fuzzer_name>

Run a libFuzzer fuzzing test.

OPTIONS:
    -d, --duration SECONDS    Fuzzing duration in seconds (default: 3600)
    -j, --jobs N             Number of parallel jobs (default: 4)
    -w, --workers N          Number of worker processes (default: 4)
    -l, --max-len N          Maximum input length (default: 65536)
    -t, --timeout N          Timeout per input in seconds (default: 10)
    -m, --memory N           RSS limit in MB (default: 2048)
    -h, --help               Show this help message

FUZZER NAMES:
    fuzz_bufferlist          Buffer operations fuzzer
    fuzz_encode_decode       Encoding/decoding fuzzer
    fuzz_crc32c              CRC32C consistency fuzzer
    fuzz_msgr_frames         Messenger frames fuzzer
    fuzz_rgw_xml             RGW XML parser fuzzer

EXAMPLES:
    # Run buffer fuzzer for 1 hour
    $0 fuzz_bufferlist

    # Run encoding fuzzer for 2 hours with 8 workers
    $0 -d 7200 -w 8 fuzz_encode_decode

    # Run CRC fuzzer with custom memory limit
    $0 -m 4096 fuzz_crc32c

EOF
    exit 1
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--duration)
            DURATION="$2"
            shift 2
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -w|--workers)
            WORKERS="$2"
            shift 2
            ;;
        -l|--max-len)
            MAX_LEN="$2"
            shift 2
            ;;
        -t|--timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        -m|--memory)
            RSS_LIMIT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            if [[ -z "$FUZZER" ]]; then
                FUZZER="$1"
            else
                echo "Error: Unknown option: $1"
                usage
            fi
            shift
            ;;
    esac
done

if [[ -z "$FUZZER" ]]; then
    echo "Error: Fuzzer name required"
    usage
fi

# Determine corpus directory
CORPUS_DIR=""
case "$FUZZER" in
    fuzz_bufferlist)
        CORPUS_DIR="${SCRIPT_DIR}/buffer/corpus"
        ;;
    fuzz_encode_decode)
        CORPUS_DIR="${SCRIPT_DIR}/encoding/corpus"
        ;;
    fuzz_crc32c)
        CORPUS_DIR="${SCRIPT_DIR}/crc32/corpus"
        ;;
    fuzz_msgr_frames)
        CORPUS_DIR="${SCRIPT_DIR}/msgr/corpus"
        ;;
    fuzz_rgw_xml)
        CORPUS_DIR="${SCRIPT_DIR}/rgw/corpus"
        ;;
    *)
        echo "Error: Unknown fuzzer: $FUZZER"
        usage
        ;;
esac

# Check if fuzzer binary exists
FUZZER_BIN="${BUILD_DIR}/bin/${FUZZER}"
if [[ ! -f "$FUZZER_BIN" ]]; then
    echo "Error: Fuzzer binary not found: $FUZZER_BIN"
    echo "Please build with: cmake -DWITH_FUZZING=ON && make $FUZZER"
    exit 1
fi

# Create corpus directory if it doesn't exist
mkdir -p "$CORPUS_DIR"

# Create artifacts directory
ARTIFACTS_DIR="${SCRIPT_DIR}/artifacts/${FUZZER}"
mkdir -p "$ARTIFACTS_DIR"

echo "=========================================="
echo "Running Fuzzer: $FUZZER"
echo "=========================================="
echo "Duration:     ${DURATION}s"
echo "Jobs:         $JOBS"
echo "Workers:      $WORKERS"
echo "Max Length:   $MAX_LEN"
echo "Timeout:      ${TIMEOUT}s"
echo "Memory Limit: ${RSS_LIMIT}MB"
echo "Corpus:       $CORPUS_DIR"
echo "Artifacts:    $ARTIFACTS_DIR"
echo "=========================================="
echo ""

# Check for dictionary
DICT_FILE=""
if [[ "$FUZZER" == "fuzz_encode_decode" ]]; then
    DICT_FILE="${SCRIPT_DIR}/encoding/encoding.dict"
    if [[ -f "$DICT_FILE" ]]; then
        echo "Using dictionary: $DICT_FILE"
    fi
fi

# Run fuzzer
DICT_ARG=""
if [[ -n "$DICT_FILE" && -f "$DICT_FILE" ]]; then
    DICT_ARG="-dict=$DICT_FILE"
fi

"$FUZZER_BIN" \
    -max_total_time="$DURATION" \
    -jobs="$JOBS" \
    -workers="$WORKERS" \
    -max_len="$MAX_LEN" \
    -timeout="$TIMEOUT" \
    -rss_limit_mb="$RSS_LIMIT" \
    -artifact_prefix="$ARTIFACTS_DIR/" \
    -print_final_stats=1 \
    $DICT_ARG \
    "$CORPUS_DIR"

echo ""
echo "=========================================="
echo "Fuzzing completed!"
echo "Corpus: $CORPUS_DIR"
echo "Artifacts: $ARTIFACTS_DIR"
echo "=========================================="
