#!/usr/bin/env python3
# Fixed SSE broadcast: await StreamResponse.write so events actually flush.

import asyncio, json, os, sys, signal, time
from aiohttp import web

TCP_JSON_HOST, TCP_JSON_PORT = "127.0.0.1", 8650   # merged console+reliable feed
UDP_METRICS_HOST, UDP_METRICS_PORT = "127.0.0.1", 9999
HTTP_HOST, HTTP_PORT = "127.0.0.1", 8645

HORNET_BASENAME = "hornetnode"
PID_REFRESH_SEC = 5.0

SUBS = set()
RELIABLE_RING = []
RING_MAX = 200
HORNET_PID = None

def _env_pid():
    v = os.environ.get("HORNET_PID")
    if not v: return None
    try: return int(v)
    except: return None

def _pidfile_pid():
    pf = os.environ.get("HORNET_PIDFILE")
    if not pf: return None
    try:
        with open(pf, "r") as f:
            return int(f.read().strip())
    except:
        return None

def _psutil_scan_pid():
    try:
        import psutil
    except ImportError:
        return None
    candidates = []
    now = time.time()
    for p in psutil.process_iter(attrs=["pid","name","cmdline","create_time"]):
        try:
            name = (p.info.get("name") or "").lower()
            cmd  = " ".join(p.info.get("cmdline") or []).lower()
            if HORNET_BASENAME in name or HORNET_BASENAME in cmd:
                ct = p.info.get("create_time") or now
                candidates.append((ct, p.info["pid"]))
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    if not candidates: return None
    candidates.sort(key=lambda t: t[0], reverse=True)
    return candidates[0][1]

async def _pgrep_scan_pid():
    try:
        proc = await asyncio.create_subprocess_exec(
            "pgrep","-f",HORNET_BASENAME,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL
        )
        out,_ = await proc.communicate()
        if proc.returncode != 0 or not out: return None
        pids = [int(x) for x in out.decode().strip().splitlines() if x.strip().isdigit()]
        if not pids: return None
        # try newest by /proc starttime (Linux); else pick max pid
        def _start_ticks(pid:int)->int:
            try:
                with open(f"/proc/{pid}/stat","r") as f:
                    return int(f.read().split()[21])
            except Exception:
                return -1
        scored = [( _start_ticks(pid), pid) for pid in pids]
        scored.sort(key=lambda x: x[0], reverse=True)
        return scored[0][1] if scored and scored[0][0] >= 0 else max(pids)
    except Exception:
        return None

async def _discover_pid_periodically():
    global HORNET_PID
    fixed = _env_pid() or _pidfile_pid()
    if fixed is not None:
        HORNET_PID = fixed
        return
    prev = None
    while True:
        pid = _psutil_scan_pid()
        if pid is None:
            pid = await _pgrep_scan_pid()
        if pid != prev and pid is not None:
            HORNET_PID = pid
            prev = pid
            print(f"[sidecar] auto-detected hornetnode PID = {pid}", file=sys.stderr, flush=True)
        await asyncio.sleep(PID_REFRESH_SEC)

async def sse_handler(request: web.Request):
    resp = web.StreamResponse(
        status=200, reason="OK",
        headers={"Content-Type":"text/event-stream",
                 "Cache-Control":"no-cache",
                 "Access-Control-Allow-Origin":"*"})
    await resp.prepare(request)
    # replay reliable ring
    for s in RELIABLE_RING[-RING_MAX:]:
        await resp.write(f"event: reliable\ndata: {s}\n\n".encode())
    SUBS.add(resp)
    try:
        while True:
            await asyncio.sleep(60)
    except asyncio.CancelledError:
        pass
    finally:
        SUBS.discard(resp)
    return resp

async def broadcast_async(event: str, obj_or_json):
    """Async write to all subscribers; safely prunes dead ones."""
    if isinstance(obj_or_json, (dict, list)):
        data = json.dumps(obj_or_json, separators=(',',':'))
    else:
        data = str(obj_or_json)
    frame = f"event: {event}\n" + "data: " + data + "\n\n"
    dead = []
    for s in list(SUBS):
        try:
            await s.write(frame.encode())
        except Exception:
            dead.append(s)
    for d in dead:
        SUBS.discard(d)

def broadcast(event: str, obj_or_json):
    """Schedule broadcast without awaiting (usable from any context)."""
    asyncio.get_running_loop().create_task(broadcast_async(event, obj_or_json))

async def tcp_jsonl_client():
    while True:
        try:
            reader, writer = await asyncio.open_connection(TCP_JSON_HOST, TCP_JSON_PORT)
            while True:
                line = await reader.readline()
                if not line: break
                s = line.decode('utf-8','ignore').strip()
                if not s: continue
                try:
                    obj = json.loads(s)
                except Exception:
                    await broadcast_async('console', {"msg": s})
                    continue
                t = obj.get('t')
                if t == 'console':
                    await broadcast_async('console', {"msg": obj.get('msg','')})
                elif t == 'reliable':
                    packed = json.dumps(obj, separators=(',',':'))
                    RELIABLE_RING.append(packed)
                    if len(RELIABLE_RING) > RING_MAX:
                        del RELIABLE_RING[0:len(RELIABLE_RING)-RING_MAX]
                    await broadcast_async('reliable', packed)
                elif t == 'metrics':
                    await broadcast_async('metrics', obj)
                else:
                    await broadcast_async('console', {"msg": s})
            writer.close(); await writer.wait_closed()
        except (ConnectionRefusedError, OSError):
            await asyncio.sleep(0.5)
        except asyncio.CancelledError:
            return
        await asyncio.sleep(0.5)

class UdpMetrics(asyncio.DatagramProtocol):
    def datagram_received(self, data, addr):
        try:
            obj = json.loads(data.decode('utf-8','ignore'))
            if obj.get('t') == 'metrics':
                # schedule async broadcast
                broadcast('metrics', obj)
        except Exception:
            pass

async def udp_metrics_server(loop):
    await loop.create_datagram_endpoint(lambda: UdpMetrics(),
                                        local_addr=(UDP_METRICS_HOST, UDP_METRICS_PORT))

async def index_handler(request: web.Request):
    base = os.path.dirname(os.path.abspath(__file__))
    html_path = os.path.join(base, 'live_status.html')
    if not os.path.exists(html_path):
        html_path = os.path.abspath('live_status.html')
    return web.FileResponse(html_path)

async def send_signal(sig):
    global HORNET_PID
    if HORNET_PID is None:
        return False, "hornet PID not found (set HORNET_PID / HORNET_PIDFILE, or ensure process name contains 'hornetnode')"
    try:
        os.kill(HORNET_PID, sig)
        return True, None
    except ProcessLookupError:
        return False, f"process {HORNET_PID} not found"
    except PermissionError:
        return False, "permission denied sending signal"
    except Exception as e:
        return False, str(e)

async def pause_api(request: web.Request):
    ok, err = await send_signal(signal.SIGSTOP)
    return web.json_response({"ok": ok, "error": err})

async def resume_api(request: web.Request):
    ok, err = await send_signal(signal.SIGCONT)
    return web.json_response({"ok": ok, "error": err})

async def pid_api(request: web.Request):
    return web.json_response({"pid": HORNET_PID})

async def main():
    loop = asyncio.get_running_loop()
    asyncio.create_task(_discover_pid_periodically())
    asyncio.create_task(tcp_jsonl_client())
    await udp_metrics_server(loop)

    app = web.Application()
    app.router.add_get('/', index_handler)
    app.router.add_get('/stream', sse_handler)
    app.router.add_post('/api/pause', pause_api)
    app.router.add_post('/api/resume', resume_api)
    app.router.add_get('/api/pid', pid_api)

    runner = web.AppRunner(app); await runner.setup()
    site = web.TCPSite(runner, HTTP_HOST, HTTP_PORT); await site.start()
    print(f"[sidecar] HUD http://{HTTP_HOST}:{HTTP_PORT}/  |  TCP JSONL {TCP_JSON_HOST}:{TCP_JSON_PORT}  |  UDP {UDP_METRICS_HOST}:{UDP_METRICS_PORT}", file=sys.stderr)

    stop = asyncio.Future()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, stop.cancel)
        except NotImplementedError:
            pass
    try:
        await stop
    except asyncio.CancelledError:
        pass

if __name__ == '__main__':
    try:
        import aiohttp  # ensure installed
    except Exception:
        pass
    asyncio.run(main())
