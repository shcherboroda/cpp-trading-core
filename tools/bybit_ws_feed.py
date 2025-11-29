#!/usr/bin/env python3
import asyncio
import json
import sys
import time

import websockets  # pip install websockets

WS_URL = "wss://stream.bybit.com/v5/public/spot"  # check Bybit docs if needed
SYMBOL = "BTCUSDT"
CHANNEL = f"publicTrade.{SYMBOL}"

PRICE_SCALE = 100      # price_int = int(price * PRICE_SCALE)
QTY_SCALE   = 1000     # qty_int   = int(qty * QTY_SCALE)


async def main():
    async with websockets.connect(WS_URL) as ws:
        sub_msg = {"op": "subscribe", "args": [CHANNEL]}
        await ws.send(json.dumps(sub_msg))

        async for raw in ws:
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue

            if msg.get("topic") != CHANNEL:
                continue

            data = msg.get("data") or []
            for trade in data:
                # Bybit trade fields names can vary a bit.
                # Check Bybit docs and adjust if needed.
                side_str = str(trade.get("S", trade.get("s", "")))
                price_str = str(trade.get("p", "0"))
                qty_str = str(trade.get("v", trade.get("q", "0")))
                ts_ms = int(trade.get("T", int(time.time() * 1000)))

                try:
                    price = float(price_str)
                    qty = float(qty_str)
                except ValueError:
                    continue

                side_char = "B" if side_str.lower().startswith("b") else "S"
                price_int = int(price * PRICE_SCALE)
                qty_int = int(qty * QTY_SCALE)

                # For now we use ts_ms as "ts_ns" scaled by 1e6
                ts_ns = ts_ms * 1_000_000

                line = f"{ts_ns},T,{side_char},{price_int},{qty_int}\n"
                sys.stdout.write(line)
                sys.stdout.flush()


if __name__ == "__main__":
    asyncio.run(main())
