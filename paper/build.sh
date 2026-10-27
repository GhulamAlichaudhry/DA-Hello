#!/usr/bin/env bash
# Full pdflatex + bibtex + pdflatex x2 build for DA-HELLO
set -euo pipefail
cd "$(dirname "$0")"
pdflatex -interaction=nonstopmode main
bibtex main || true
pdflatex -interaction=nonstopmode main
pdflatex -interaction=nonstopmode main
pdfinfo main.pdf | grep -i Pages || true
echo "Built: $(pwd)/main.pdf"
sha256sum main.pdf
