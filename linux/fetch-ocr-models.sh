#!/bin/bash
# =============================================================================
# Fetch the default PP-OCRv5 mobile recognition models (ONNX) for the Linux OCR
# backend. OCR Phase 4B (PaddleOCR via ONNX Runtime).
# =============================================================================
# Downloads pre-converted PP-OCRv5 mobile *recognition* models from the
# RapidAI/RapidOCR model repository (ModelScope, with a HuggingFace mirror),
# verifies each against a pinned SHA256, and lays them out at:
#
#   linux/ocr-models/latin_rec.onnx     (default; Latin / en-US)
#   linux/ocr-models/ch_rec.onnx        (Chinese + English + Japanese + Traditional)
#   linux/ocr-models/korean_rec.onnx    (Korean)
#
# These RapidOCR models embed their character dictionary inside the ONNX file
# (custom metadata key "character"), so NO separate dict files are needed --
# PaddleOcrEngine reads the dict straight from the model metadata.
#
# Default bundled set per QA Q5.3 Option A. Detection is skipped entirely
# (OcrLineGrouper already segments lines/cells -- QA Q5.2). latin_rec.onnx is
# the mandatory default that gates SPEEDYNOTE_ENABLE_PADDLE_OCR; ch/korean are
# optional (the engine still loads with only latin present).
#
# Usage (from the SpeedyNote project root):
#   ./linux/fetch-ocr-models.sh
#
# To refresh: delete linux/ocr-models/ and re-run.
# =============================================================================
set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/ocr-models"

MS_BASE="https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.8.0/onnx/PP-OCRv5/rec"
HF_BASE="https://huggingface.co/RapidAI/RapidOCR/resolve/v3.8.0/onnx/PP-OCRv5/rec"

# local name | remote file | SHA256
MODELS=(
    "latin_rec.onnx|latin_PP-OCRv5_rec_mobile.onnx|b20bd37c168a570f583afbc8cd7925603890efbcdc000a59e22c269d160b5f5a"
    "ch_rec.onnx|ch_PP-OCRv5_rec_mobile.onnx|5825fc7ebf84ae7a412be049820b4d86d77620f204a041697b0494669b1742c5"
    "korean_rec.onnx|korean_PP-OCRv5_rec_mobile.onnx|cd6e2ea50f6943ca7271eb8c56a877a5a90720b7047fe9c41a2e541a25773c9b"
)

command -v curl     >/dev/null 2>&1 || { echo -e "${RED}Error: curl not found.${NC}"; exit 1; }
command -v sha256sum>/dev/null 2>&1 || { echo -e "${RED}Error: sha256sum not found.${NC}"; exit 1; }

mkdir -p "${OUTPUT_DIR}"

# Clean up the in-flight download if any step aborts (set -e / failed mirror).
# A successful mv leaves nothing behind, so this is a no-op on the happy path.
tmp=""
trap 'rm -f "${tmp:-}"' EXIT

verify() { # path expected_sha
    local got
    got="$(sha256sum "$1" | awk '{print $1}')"
    [ "${got}" = "$2" ]
}

fetched=0; skipped=0
for entry in "${MODELS[@]}"; do
    IFS='|' read -r local_name remote_file sha <<< "${entry}"
    dest="${OUTPUT_DIR}/${local_name}"

    if [ -f "${dest}" ] && verify "${dest}" "${sha}"; then
        echo -e "${YELLOW}skip ${local_name} (present, checksum ok)${NC}"
        skipped=$((skipped + 1))
        continue
    fi

    echo -e "${CYAN}fetch ${local_name} <- ${remote_file}${NC}"
    tmp="$(mktemp)"
    if ! curl -fSL --retry 3 -o "${tmp}" "${MS_BASE}/${remote_file}"; then
        echo -e "${YELLOW}  ModelScope failed; trying HuggingFace mirror...${NC}"
        curl -fSL --retry 3 -o "${tmp}" "${HF_BASE}/${remote_file}"
    fi

    if ! verify "${tmp}" "${sha}"; then
        echo -e "${RED}Error: SHA256 mismatch for ${local_name}${NC}"
        echo "  expected ${sha}"
        echo "  got      $(sha256sum "${tmp}" | awk '{print $1}')"
        rm -f "${tmp}"
        exit 1
    fi
    mv "${tmp}" "${dest}"
    echo -e "${GREEN}  ok ($(du -h "${dest}" | awk '{print $1}'))${NC}"
    fetched=$((fetched + 1))
done

echo ""
if [ ! -f "${OUTPUT_DIR}/latin_rec.onnx" ]; then
    echo -e "${RED}Error: latin_rec.onnx (the mandatory default) is missing.${NC}"
    exit 1
fi
echo -e "${GREEN}Done! ${fetched} fetched, ${skipped} already present in:${NC}"
echo "  ${OUTPUT_DIR}"
echo -e "${GREEN}Next: run ./linux/fetch-onnxruntime.sh (if not done), then reconfigure CMake.${NC}"
