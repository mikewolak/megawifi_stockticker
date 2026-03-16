"""
conv.py — build nasdaq_tickers.bin from NASDAQ + NYSE/AMEX/Arca listings.

Sources (fetched live from NASDAQ trader FTP):
  nasdaqlisted.txt  — NASDAQ Global Select, Global Market, Capital Market
  otherlisted.txt   — NYSE (N), AMEX (A), NYSE Arca (P), BATS (Z)

Record layout (38 bytes, sorted by symbol):
  offset 0..5   char symbol[6]   null-padded
  offset 6..37  char name[32]    truncated, null-padded

Run: python3 conv.py
"""

import csv
import io
import urllib.request

NASDAQ_URL = "ftp://ftp.nasdaqtrader.com/SymbolDirectory/nasdaqlisted.txt"
OTHER_URL  = "ftp://ftp.nasdaqtrader.com/SymbolDirectory/otherlisted.txt"
OUT_FILE   = "nasdaq_tickers.bin"

results = {}   # symbol -> name  (dict deduplicates)

# ── NASDAQ listed ────────────────────────────────────────────────────────────
print("Fetching nasdaqlisted.txt ...")
with urllib.request.urlopen(NASDAQ_URL) as r:
    text = r.read().decode("utf-8")

for row in csv.DictReader(io.StringIO(text), delimiter="|"):
    if row.get("Test Issue") != "N": continue
    if row.get("ETF")        != "N": continue
    sym  = row["Symbol"].strip()
    name = row["Security Name"].strip()
    if not sym or sym.startswith("^"): continue
    results[sym] = name

print(f"  {len(results)} NASDAQ stocks")

# ── Other listed (NYSE / AMEX / Arca / BATS) ────────────────────────────────
print("Fetching otherlisted.txt ...")
with urllib.request.urlopen(OTHER_URL) as r:
    text = r.read().decode("utf-8")

before = len(results)
for row in csv.DictReader(io.StringIO(text), delimiter="|"):
    if row.get("Test Issue") != "N": continue
    if row.get("ETF")        != "N": continue
    sym  = row["ACT Symbol"].strip()
    name = row["Security Name"].strip()
    if not sym or sym.startswith("^"): continue
    if sym not in results:             # NASDAQ takes precedence on duplicates
        results[sym] = name

print(f"  {len(results) - before} NYSE/AMEX/Arca stocks added")

# ── Write binary blob ────────────────────────────────────────────────────────
sorted_syms = sorted(results.keys())
print(f"Writing {len(sorted_syms)} records → {OUT_FILE}")

with open(OUT_FILE, "wb") as f:
    for sym in sorted_syms:
        sb = sym.encode().ljust(6, b"\x00")[:6]
        nb = results[sym].encode().ljust(32, b"\x00")[:32]
        f.write(sb + nb)

print(f"Done — {len(sorted_syms) * 38} bytes ({len(sorted_syms) * 38 / 1024:.1f} KB)")
