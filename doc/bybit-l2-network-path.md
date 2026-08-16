# Bybit L2 network-path observations

## Scope

This note separates local CPU-side processing from network delivery. It does
not claim exchange-to-client latency: the local system clock and exchange
timestamps are not synchronized, and WSL2 adds scheduler and virtualization
variance.

The live application prints `local_now_ms - msg.ts_ms` as a **clock-offset
sample**, not data latency. Public runs can produce negative values, confirming
that it cannot be interpreted as one-way latency without clock synchronization.

The current public WebSocket client uses a synchronous loop:

1. `ws.read()` waits for one WebSocket frame;
2. the frame is decoded and applied to the local book in the same thread;
3. only then does the next `ws.read()` begin.

The client records read wait and callback duration. It also records a
diagnostic buffered-read count: a completed `ws.read()` below 100 microseconds
is treated as likely already-buffered data, not a network-latency sample.

## Findings

Short public BTCUSDT depth-50 smoke runs observed both isolated reads and
bursts of already-buffered frames. In the initial three 30-message runs, the
number of sub-100-microsecond reads was 2, 12 and 13; the corresponding
longest bursts were the same length. A pinned repetition recorded bursts of
2, 5 and 9. This demonstrates delivery batching, but not sustained application
backlog.

The bounded decoder keeps typical local handler work in microseconds. The
observed bursts are therefore drained in tens to hundreds of microseconds in
this single-symbol depth-50 configuration. There is no evidence yet that a
reader thread plus an SPSC handoff would improve the current path; it would add
queueing latency and synchronization complexity.

For an exceptional repeated snapshot (for example after exchange-side
resynchronization), the local book now reconciles sorted unique levels without
clearing existing map nodes. On an actual 1,000-level-per-side capture, this
reduced the median of per-run `apply_snapshot` p50 values from 144.9 to 19.6
microseconds in a 7-run-per-variant ABBA comparison. It is not a
network-delivery improvement and has no effect on the delta path. Unsorted or
duplicate inputs use the previous clear-and-build behavior. The collector,
input capture and summary are retained in
`results/bybit-l2-resnapshot-20260816T121024431339597Z/`.

A post-merge replication on the same capture retained the result: the median
of per-run snapshot-apply p50 values was 125.8 microseconds for clear-and-build
and 17.9 microseconds for reconciliation (seven runs per variant). The raw
CSV, environment and summary are in
`results/bybit-l2-resnapshot-20260816T123210375156557Z/`.

## Repeated live depth probe

Three 30-message public runs per depth were collected with
`scripts/collect_bybit_ws_depth.sh`. The raw logs are retained in
`results/bybit-ws-depth-20260814T081058Z/`; they include the exact environment
and each program output.

- **Depth 1:** 29 snapshots and no deltas per run, two levels per snapshot,
  0–1 buffered reads. It is a best-quote feed, not an L2 replacement.
- **Depth 50:** one 100-level snapshot plus 28 deltas per run. Delta batch p50
  was 1–2 levels and handler p50 was 5.5–6.0 microseconds. Each run saw only
  1–2 buffered reads.
- **Depth 1,000:** one 2,000-level snapshot plus 28 deltas per run. Delta batch
  p50 was 6–10 levels and the runs saw 10–15 buffered reads in one initial
  streak. Handler p50 was 7.6–11.0 microseconds. The reported handler p99
  includes the single large snapshot, so it is not a delta tail comparison.

This supports keeping the synchronous depth-50 design for the current
single-symbol workload. Depth 1,000 is viable when the additional market depth
is needed, but it has a larger startup/burst cost and should not be selected as
a latency optimization.

## Subscription depth is a workload trade-off

Bybit documents spot orderbook depths 1, 50, 200 and 1,000, with different
push frequencies. A one-run depth 1/50/1,000 smoke probe confirmed that all
three subscriptions work with the current decoder:

- depth 1 emits snapshots only and contains only best bid/ask; it is not a
  replacement for a depth-50 L2 book;
- depth 50 is the current live default and emits snapshot plus deltas;
- depth 1,000 produces a 2,000-level initial snapshot and more pronounced
  burst behaviour, while small deltas remain cheap locally.

These are exploratory live observations, not a controlled latency comparison.
The collector below preserves raw output for future repeated runs.

```bash
./scripts/build_release.sh
./scripts/collect_bybit_ws_depth.sh 3 30
```

The live application accepts optional positional arguments:

```text
trading_bybit_ws_orderbook_live [symbol] [max_messages] [capture_path] [depth] [trace_path]
```

`BYBIT_L2_DECODER=one-pass` selects the experimental parser; the validated
bounded decoder is the default. The optional trace file records
post-read local timing for diagnosis and should not be used as a low-overhead
latency benchmark.

## Rejected and deferred changes

- **REST polling:** the REST endpoint returns snapshots only, so it is not a
  live replacement for WebSocket deltas.
- **TCP_NODELAY:** an ABBA-style pinned depth-50 probe (three 30-message runs
  in each off/on/on/off block) found no stable callback benefit. Its expected
  scope is small outbound client writes rather than inbound exchange delivery;
  the temporary option was removed. Raw logs:
  `results/bybit-ws-depth-20260814T083337Z/`, `...083343Z/`, `...083348Z/`,
  and `...083354Z/`.
- **WebSocket per-message deflate:** Bybit negotiated
  `permessage-deflate; server_no_context_takeover; client_no_context_takeover`.
  A pinned depth-1,000 off/on/on/off probe did not show a stable local callback
  improvement; callback p50 was dominated by live message mix and varied
  within both configurations. The client cannot observe compressed wire bytes
  after Beast inflates the frame, so this test does not quantify bandwidth
  saved. Keep compression disabled for the latency-oriented default; revisit
  only if a bandwidth-constrained deployment can measure bytes at the socket
  or host interface. Raw logs: `results/bybit-ws-depth-20260814T083705Z/`,
  `...083716Z/`, `...083728Z/`, and `...083740Z/`.
- **Heartbeat:** Bybit recommends a 20-second ping, but adding it safely to
  the current blocking client requires an asynchronous Beast/Asio strand
  design. It is a reliability refactor, not a small network optimization.
- **Reader/engine split:** defer until a longer arrival/backlog capture shows
  sustained local backlog under a realistic multi-symbol or high-depth load.
  The current SPSC benchmark establishes that queue capacity and handoff have
  workload-dependent tail costs; the single-symbol depth-50 live runs do not
  meet the admission criterion for adding that cost.

## Next experiment

For a genuine multi-symbol/high-depth case, collect repeated arrival/backlog
traces first. Add a reader/engine split only if decoder-plus-apply time
approaches the inter-burst interval or the buffered streak keeps growing. Then
compare it with the synchronous path using the same captured frames and an
explicit timestamp from successful read through book application.
