import csv, requests, time

FINNHUB_KEY = "your_key"

with open("nasdaq.txt") as f:
    rows = list(csv.DictReader(f, delimiter="|"))

# Filter out test issues, ETFs, garbage
stocks = [r for r in rows 
          if r["Test Issue"] == "N" 
          and r["ETF"] == "N"
          and r["Financial Status"] == "N"]

results = []
for r in stocks:
    sym = r["Symbol"].strip()
    name = r["Security Name"].strip()
    # Optionally hit Finnhub for sector
    # profile = requests.get(f"https://finnhub.io/api/v1/stock/profile2?symbol={sym}&token={FINNHUB_KEY}").json()
    # sector = profile.get("finnhubIndustry", "")
    results.append({"symbol": sym, "name": name})
    
# Write sorted binary blob: 6-byte symbol (null padded) + 32-byte name = 38 bytes/record
with open("nasdaq_tickers.bin", "wb") as f:
    for r in sorted(results, key=lambda x: x["symbol"]):
        sym = r["symbol"].encode().ljust(6, b'\x00')[:6]
        name = r["name"].encode().ljust(32, b'\x00')[:32]
        f.write(sym + name)
