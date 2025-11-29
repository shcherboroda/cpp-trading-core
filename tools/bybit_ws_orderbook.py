import asyncio
import json
import sys
import signal

import websockets  # pip install websockets (в твоём venv уже стоит)

WS_URL = "wss://stream.bybit.com/v5/public/spot"
SYMBOL = "BTCUSDT"
CHANNEL = f"orderbook.50.{SYMBOL}"

# If the pipe is closed, just terminate the process instead of raising BrokenPipe
signal.signal(signal.SIGPIPE, signal.SIG_DFL)


async def main() -> None:
    async with websockets.connect(WS_URL) as ws:
        sub_msg = {"op": "subscribe", "args": [CHANNEL]}
        await ws.send(json.dumps(sub_msg))

        async for raw in ws:
            line = raw.strip() + "\n"
            try:
                sys.stdout.write(line)
                sys.stdout.flush()
            except BrokenPipeError:
                # Consumer (C++ process) closed the pipe.
                # Exit gracefully without traceback.
                return


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except BrokenPipeError:
        # Extra safety, in case something leaks out of main()
        pass
