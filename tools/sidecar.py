#!/usr/bin/env python3
# Hornet demo sidecar (lean): SSE server + TCP JSONL client + UDP metrics + static /assets/
import asyncio, json, os, signal, sys
from aiohttp import web

TCP_JSON_HOST, TCP_JSON_PORT = "127.0.0.1", 8650
UDP_METRICS_HOST, UDP_METRICS_PORT = "127.0.0.1", 9999
HTTP_HOST, HTTP_PORT = "127.0.0.1", 8645

SUBS: set[web.StreamResponse] = set()

async def broadcast_async(event: str, payload):
    if not SUBS: return
    data = json.dumps(payload, separators=(",", ":"))
    msg = f"event: {event}\ndata: {data}\n\n".encode("utf-8")
    dead = []
    for resp in list(SUBS):
        try:
            await resp.write(msg)
        except Exception:
            dead.append(resp)
    for d in dead:
        SUBS.discard(d)

class UdpMetrics(asyncio.DatagramProtocol):
    def datagram_received(self, data, addr):
        try:
            s = data.decode("utf-8", "ignore").strip()
            if not s: return
            obj = json.loads(s)
            asyncio.create_task(broadcast_async("metrics", obj))
        except Exception:
            pass

async def udp_metrics_server(loop) -> bool:
    try:
        await loop.create_datagram_endpoint(lambda: UdpMetrics(),
                                            local_addr=(UDP_METRICS_HOST, UDP_METRICS_PORT))
        return True
    except OSError as e:
        print(f"[sidecar] unable to bind UDP metrics {UDP_METRICS_HOST}:{UDP_METRICS_PORT} ({e}); continuing without UDP",
              file=sys.stderr, flush=True)
        return False

async def tcp_jsonl_client():
    while True:
        try:
            reader, writer = await asyncio.open_connection(TCP_JSON_HOST, TCP_JSON_PORT)
            while True:
                line = await reader.readline()
                if not line: break
                s = line.decode("utf-8","ignore").strip()
                if not s: continue
                try:
                    obj = json.loads(s)
                except Exception:
                    await broadcast_async("console", {"msg": s})
                    continue
                t = obj.get("t")
                if t == "console":
                    await broadcast_async("console", {"msg": obj.get("msg","")})
                elif t == "reliable":
                    await broadcast_async("reliable", obj)
                elif t == "metrics":
                    await broadcast_async("metrics", obj)
                else:
                    await broadcast_async("console", {"msg": s})
            writer.close(); await writer.wait_closed()
        except (ConnectionRefusedError, OSError):
            await asyncio.sleep(0.5)
        except asyncio.CancelledError:
            return
        await asyncio.sleep(0.5)

async def index_handler(request: web.Request):
    base = os.path.dirname(os.path.abspath(__file__))
    html_path = os.path.join(base, "live_status.html")
    if not os.path.exists(html_path):
        html_path = os.path.abspath("live_status.html")
    return web.FileResponse(html_path)

async def sse_handler(request: web.Request):
    resp = web.StreamResponse(status=200, reason="OK", headers={
        "Content-Type": "text/event-stream",
        "Cache-Control": "no-cache",
        "Connection": "keep-alive",
        "X-Accel-Buffering": "no",
        "Access-Control-Allow-Origin": "*",
    })
    await resp.prepare(request)
    SUBS.add(resp)
    try:
        await resp.write(b": ok\n\n")
        while True:
            await asyncio.sleep(3600)
    except asyncio.CancelledError:
        pass
    finally:
        SUBS.discard(resp)
    return resp

async def pause_api(request: web.Request):  return web.Response(text="OK")
async def resume_api(request: web.Request): return web.Response(text="OK")
async def pid_api(request: web.Request):    return web.json_response({"pid": os.getpid()})

async def main():
    loop = asyncio.get_running_loop()
    tasks = [asyncio.create_task(tcp_jsonl_client())]
    udp_ok = await udp_metrics_server(loop)

    app = web.Application()
    app.router.add_get("/", index_handler)
    app.router.add_get("/stream", sse_handler)
    app.router.add_post("/api/pause", pause_api)
    app.router.add_post("/api/resume", resume_api)
    app.router.add_get("/api/pid", pid_api)

    # serve static assets (banner image, etc.) from this folder under /assets/
    base = os.path.dirname(os.path.abspath(__file__))
    app.router.add_static("/assets/", base, show_index=False)

    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, HTTP_HOST, HTTP_PORT)
    try:
        await site.start()
    except OSError as e:
        print(f"[sidecar] unable to bind HTTP {HTTP_HOST}:{HTTP_PORT} ({e}); exiting",
              file=sys.stderr, flush=True)
        await runner.cleanup()
        for t in tasks: t.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        return

    udp_info = (f"UDP {UDP_METRICS_HOST}:{UDP_METRICS_PORT}" if udp_ok else "UDP metrics disabled")
    print(f"[sidecar] HUD http://{HTTP_HOST}:{HTTP_PORT}/  |  TCP JSONL {TCP_JSON_HOST}:{TCP_JSON_PORT}  |  {udp_info}", file=sys.stderr)

    stop = loop.create_future()
    def _trip():
        if not stop.done():
            stop.set_result(True)
    try:
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, _trip)
    except NotImplementedError:
        for sig in (signal.SIGINT, signal.SIGTERM):
            signal.signal(sig, lambda s, f: _trip())
    await stop
    for t in tasks: t.cancel()
    await asyncio.gather(*tasks, return_exceptions=True)
    await runner.cleanup()

if __name__ == "__main__":
    asyncio.run(main())
