#!/usr/bin/env python3
import asyncio, json, random, time, sys

TCP_HOST, TCP_PORT = "127.0.0.1", 8650
UDP_HOST, UDP_PORT = "127.0.0.1", 9999

# ---- TCP JSONL server (console + reliable) ----
async def handle_tcp(reader, writer):
    peer = writer.get_extra_info('peername')
    print(f"[mock] TCP client connected: {peer}", file=sys.stderr, flush=True)
    tick = 0
    try:
        while True:
            tick += 1
            # Console message: cycle levels DEBUG -> INFO -> WARN -> ERROR
            level = (tick % 4)
            if level == 0:
                line = f"DEBUG starting tick {tick}"
            elif level == 1:
                line = f"INFO heartbeat tick {tick}"
            elif level == 2:
                line = f"WARN simulated lag on peer {tick % 8}"
            else:
                line = f"ERROR failed to parse block {100000 + tick} header"
            msg = {"t":"console","msg": line}
            writer.write((json.dumps(msg)+"\n").encode())

            # Reliable event every 5 ticks
            if tick % 5 == 0:
                rel = {
                    "t":"reliable","ts":int(time.time()*1000),
                    "kind": random.choice([0,2,3]),  # 0=Error,2=Info,3=State
                    "code": 0,
                    "msg": random.choice(["Phase -> Headers","Phase -> Blocks","Demo event"]),
                    "ctx": {"note":"mock"}
                }
                writer.write((json.dumps(rel)+"\n").encode())

            await writer.drain()
            await asyncio.sleep(1.0)
    except asyncio.CancelledError:
        pass
    except Exception as e:
        print(f"[mock] TCP client error: {e}", file=sys.stderr, flush=True)
    finally:
        try: writer.close(); await writer.wait_closed()
        except: pass
        print(f"[mock] TCP client disconnected: {peer}", file=sys.stderr, flush=True)

async def start_tcp():
    server = await asyncio.start_server(handle_tcp, TCP_HOST, TCP_PORT)
    addrs = ", ".join(str(s.getsockname()) for s in server.sockets)
    print(f"[mock] TCP JSONL serving on {addrs}", file=sys.stderr, flush=True)
    async with server:
        await server.serve_forever()

# ---- UDP metrics sender ----
async def udp_metrics():
    # Create a connected UDP transport for easy sendto()
    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(
        lambda: asyncio.DatagramProtocol(), remote_addr=(UDP_HOST, UDP_PORT)
    )
    headers = 100_000
    blocks = 0
    phase = 0  # 0=Headers, 1=Blocks, 2=Done
    last_switch = time.time()

    print(f"[mock] UDP metrics -> {(UDP_HOST, UDP_PORT)}", file=sys.stderr, flush=True)

    try:
        while True:
            # Simulate work and occasional phase change
            now = time.time()
            headers += random.randint(200, 400)
            blocks  += random.randint(50, 150)
            rate    = random.uniform(200, 600)
            peers   = random.randint(1, 5)

            if now - last_switch > 15 and phase < 2:
                phase += 1
                last_switch = now

            obj = {
                "t":"metrics","phase":phase,
                "headers_total":headers,
                "blocks_validated":blocks,
                "peers":peers,
                "headers_rate_hz":rate
            }
            data = json.dumps(obj, separators=(',',':')).encode()
            transport.sendto(data)
            await asyncio.sleep(1.0)
    except asyncio.CancelledError:
        pass
    finally:
        transport.close()

async def main():
    # Run TCP server and UDP metrics concurrently
    await asyncio.gather(start_tcp(), udp_metrics())

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
