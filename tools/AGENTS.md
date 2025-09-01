# Hornet Tools — Agent Summary (for future AI edits)
**Keep changes lean. Preserve contracts. Don’t add features without a diagnosed bug.**

## Purpose
Demo UI for Hornet internals:
- UDP metrics (lossy, high-rate)
- TCP JSONL events (console + reliable)
- SSE to browser served by minimal Python sidecar

## Files (in `tools/`)
- `sidecar.py` — aiohttp server: serves UI, SSE (`/stream`), and static `/assets/`; bridges UDP+TCP to SSE.
- `live_status.html` — single-file UI (HTML+CSS+JS). No build step.
- `mock_hornet.py` — test producer: TCP JSONL server + UDP metrics loop.

### Run
```bash
python3 mock_hornet.py
python3 sidecar.py
# open http://127.0.0.1:8645/
```

## Network & endpoints
| Direction | Transport | Host:Port       | Format                                         |
|-----------|-----------|-----------------|------------------------------------------------|
| Mock → Sidecar | UDP       | 127.0.0.1:9999 | JSON **object per datagram** (metrics)         |
| Mock → Sidecar | TCP       | 127.0.0.1:8650 | **JSONL**: one JSON per line (console/reliable)|
| Browser ← Sidecar | HTTP      | 127.0.0.1:8645 | UI page + static `/assets/`                     |
| Browser ← Sidecar | SSE       | `/stream`        | `event: console|reliable|metrics`               |

**Static assets**: `/assets/` maps to the same folder as `sidecar.py` (banner: `/assets/hornet.png`).

## Data formats

### UDP metrics (JSON object)
```json
{
  "headers_validated": <number>,      // legacy key "headers_total" is accepted
  "blocks_validated": <number>,
  "headers_rate_hz": <number>,        // optional
  "peers": <number>,                  // optional
  "phase": 0|1|2                      // optional: 0=Headers,1=Blocks,2=Done
}
```
Notes:
- UI progress = `blocks_validated / headers_validated`.
- Numbers may be strings or numbers; UI normalizes (prefer numbers).

### TCP JSONL — console
```json
{"t":"console","msg":"INFO connected to 12 peers"}
```
Console color substrings (case-sensitive): `DEBUG`(grey), `INFO`(ink), `WARN`(yellow), `ERROR`(red).

### TCP JSONL — reliable events
```json
{"t":"reliable","kind":0|1|2|3,"msg":"..."}
```
Kinds: 0=Error, 1=Warning, 2=Info, 3=State.

## UI behavior (`live_status.html`)

### Ordering
- `const TOP_NEWEST = false` (default): newest at **bottom** (console-style). If `true`, newest at top. Auto-scrolls to active edge.

### “Recent” highlight
- Only the newest inserted line gets `.recent` briefly. Buffer stores non-`recent` markup to avoid re-flashing on rewrites.

### Progress
- Track must expand to cell: `.kv .progress{ width:100% }` and wrapper cell uses `style="flex:1"`.
- Progress & label un-dim when metrics arrive; staleness keyed per packet.

### Staleness
- Values + labels dim via `.stale` when a field is missing in the **latest** metrics packet.
- Pause button toggles dropping updates and applies `.stale` to key labels/values.

### Numbers
```css
.num{ font-variant-numeric: tabular-nums; text-align:right; }
.kv > div.num{ justify-content:flex-end; } /* kv cells are flex */
```

### Console markup
- Each line: `<span class="log-line log-*>` where `*` is `debug|info|warn|error`.

### Banner
- Placed **above** header chips.
- `.banner{ height:200px; background:url('/assets/hornet.png') center/cover no-repeat }`

## Sidecar notes (`sidecar.py`)
- Routes: `/` (serves `live_status.html` from sidecar directory), `/stream` (SSE), `/assets/` (static from sidecar directory).
- UDP handler expects **one JSON object per datagram**; ignores malformed.
- TCP client reconnect loop to 127.0.0.1:8650; forwards `t`-typed JSONL (`console`, `reliable`, `metrics`).
- **Graceful shutdown**: a single `_trip()` sets a stop future if not done; background tasks are cancelled & awaited; `runner.cleanup()` runs.

## Minimal debug checklist
```bash
# SSE up?
curl -i http://127.0.0.1:8645/stream | head -n5

# Metrics flowing?
curl -s http://127.0.0.1:8645/stream | awk '/^event: metrics/{getline; sub(/^data: /,""); print; if(++n==3) exit}'

# Banner reachable?
curl -I http://127.0.0.1:8645/assets/hornet.png | head -n1

# Ports on macOS
lsof -nP -iTCP:8645 -sTCP:LISTEN
lsof -nP -iTCP:8650 -sTCP:LISTEN
lsof -nP -iUDP:9999
```

## Mock expectations
- UDP: emit metrics JSON at a steady cadence (e.g., 4 Hz) containing `headers_validated` and `blocks_validated`.
- TCP: rotate console messages containing `DEBUG/INFO/WARN/ERROR`; emit occasional `reliable` events.

## Invariants
- `TOP_NEWEST` remains; default false.
- SSE event names: `metrics`, `console`, `reliable`.
- Key names as specified above.
- `/assets/` serves from sidecar directory.

> If a requested change violates an invariant, propose the *smallest* alternative and verify with a one-liner smoke test (e.g., `curl /stream`, `curl /assets/hornet.png`).
