#!/usr/bin/env python3
import asyncio, json, contextlib
from collections import deque
from pathlib import Path
from aiohttp import web, ClientConnectionError

TCP_PORT = 8646
HTTP_PORT = 8645
EVENTS = ["console", "reliable", "metrics", "clear"]
EV_CONSOLE, EV_RELIABLE, EV_METRICS, EV_CLEAR = range(4)
BASE_DIR = Path(__file__).resolve().parent

replay = deque(maxlen=512)
last_metrics: str | None = None
subscribers: set[asyncio.Queue[str]] = set()

def sse_frame(idx: int, obj: dict) -> str:
    return f"event: {EVENTS[idx]}\ndata: {json.dumps(obj, separators=(',',':'))}\n\n"

def event_index_for(p: dict) -> int:
    t = str(p.get("type", "")).lower()
    if t == "log":    return EV_CONSOLE
    if t == "event":  return EV_RELIABLE
    if t == "update": return EV_METRICS
    if t == "clear":  return EV_CLEAR
    return EV_CONSOLE  # default -> console

async def index_handler(_: web.Request) -> web.StreamResponse:
    return web.FileResponse(BASE_DIR / "live_status.html")

async def fanout(event_idx: int, obj: dict):
    frame = sse_frame(event_idx, obj)
    if event_idx in (EV_CONSOLE, EV_RELIABLE):
        replay.append(frame)
    else:
        global last_metrics
        last_metrics = frame
    for q in tuple(subscribers):
        try:
            q.put_nowait(frame)
        except asyncio.QueueFull:
            pass

async def sse(_: web.Request) -> web.StreamResponse:
    resp = web.StreamResponse(status=200, headers={
        "Content-Type": "text/event-stream",
        "Cache-Control": "no-cache",
        "Connection": "keep-alive",
        "X-Accel-Buffering": "no",
    })
    await resp.prepare(_)

    async def write(frame: str) -> bool:
        try:
            await resp.write(frame.encode())
            return True
        except (ClientConnectionError, ConnectionResetError, BrokenPipeError, RuntimeError):
            return False

    # Priming + initial state
    if not await write(": open\n\n"):
        return resp
    for f in replay:
        if not await write(f): return resp
    if last_metrics is not None:
        if not await write(last_metrics): return resp

    q: asyncio.Queue[str] = asyncio.Queue(maxsize=1024)
    subscribers.add(q)
    try:
        while True:
            frame = await q.get()
            if not await write(frame):  # client went away
                break
    except asyncio.CancelledError:
        pass
    finally:
        subscribers.discard(q)
        with contextlib.suppress(Exception):
            await resp.write_eof()
    return resp

async def tcp_handler(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
    try:
        # Clear all dashboards on new Hornet TCP session
        await fanout(EV_CLEAR, {})
        while not reader.at_eof():
            line = await reader.readline()
            if not line:
                break
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            await fanout(event_index_for(obj), obj)
    finally:
        writer.close()
        await writer.wait_closed()

async def main():
    app = web.Application()
    app.router.add_get("/stream", sse)
    app.router.add_get("/", index_handler)
    app.router.add_static("/assets/", BASE_DIR, name="assets")

    runner = web.AppRunner(app); await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", HTTP_PORT); await site.start()
    print(f"[HTTP] SSE at http://127.0.0.1:{HTTP_PORT}/stream", flush=True)

    server = await asyncio.start_server(tcp_handler, "127.0.0.1", TCP_PORT)
    print(f"[TCP] listening on 127.0.0.1:{TCP_PORT}", flush=True)
    async with server:
        await server.serve_forever()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
