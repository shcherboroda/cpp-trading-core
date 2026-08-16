# Bybit L2 bounded decoder experiment

## Question

Can a decoder specialized for the documented Bybit L2 orderbook message shape
reduce CPU-side handler latency compared with the existing `nlohmann::json`
SAX decoder, without changing decoded orderbook data?

This is a local C++ experiment. It does not measure exchange-to-client network
latency and does not make a production-performance claim.

## Variants

- **SAX baseline:** `decode_bybit_l2` uses `nlohmann::json::sax_parse` and
  materializes the fields required by the live L2 handler.
- **Bounded decoder:** `decode_bybit_l2_bounded` accepts only the documented
  Bybit orderbook envelope. It validates `topic`, `type`, optional `ts`/`cts`,
  and the `data.b`/`data.a` level arrays, then converts price and quantity
  strings directly to fixed-point integers.

The bounded decoder is deliberately not a general JSON parser. It supports the
known Bybit L2 schema and does not claim to accept arbitrary JSON encodings or
future undocumented protocol variants. The SAX decoder remains available as a
reference implementation.

## Correctness checks

- Unit tests cover snapshot and delta messages, zero-quantity deletion levels,
  subscription acknowledgement handling, malformed levels, metadata extraction
  and fixed-point ticks.
- `trading_bench_bybit_l2_replay ... verify-bounded` compares SAX and bounded
  decoding frame by frame.
- The verifier passed on a captured corpus of 1,000 sequential public
  BTCUSDT frames: one subscription acknowledgement, one snapshot and 998
  deltas. For every frame, parse success, `topic`, `type`, `ts`, `cts`, bids
  and asks matched.
- A public 20-WebSocket-message smoke run after live adoption processed one
  snapshot and 18 deltas without decoder errors. This is a functional smoke
  test, not a latency measurement.

## Methodology

- Corpus: `results/bybit-l2-corpus-20260808T120000Z/frames.ndjson`.
- Each benchmark invocation replays the corpus 20 times; each variant therefore
  processes 19,980 valid L2 frames. Capture I/O is excluded.
- Build: Release (`-O3 -DNDEBUG`), GCC 15.2.0, WSL2 on an Intel Core i7-1255U.
- CPU affinity: Linux vCPU 0. WSL virtual CPU numbering does not identify
  P-core versus E-core type.
- Quiet-session replication: two 15-run blocks in opposite order, SAX →
  bounded and bounded → SAX. The table aggregates all 30 runs per variant.

Reproduce the quiet comparison with:

```bash
./scripts/build_release.sh
VARIANTS='direct-topic bounded' CPU=0 \
  ./scripts/collect_bybit_l2_replay.sh \
  results/bybit-l2-corpus-20260808T120000Z/frames.ndjson 15
VARIANTS='bounded direct-topic' CPU=0 \
  ./scripts/collect_bybit_l2_replay.sh \
  results/bybit-l2-corpus-20260808T120000Z/frames.ndjson 15
```

## Results

| Decoder | Runs | Frames/run | Median total p50 | Range of run p50 | Median total p99 | Range of run p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SAX | 30 | 19,980 | 1.738 us | 1.406–2.484 us | 5.684 us | 4.626–8.954 us |
| bounded | 30 | 19,980 | 0.800 us | 0.673–1.133 us | 3.250 us | 2.688–4.688 us |

On this fixed corpus, the bounded decoder lowers the median p50 by 54% and the
median p99 by 43%. The p99 ranges overlap slightly, so the evidence supports a
substantial median improvement but not a claim that every bounded run has a
better tail than every SAX run.

Raw result directories:

- `results/bybit-l2-replay-20260810T125802Z/`
- `results/bybit-l2-replay-20260810T125811Z/`

## Decision and limitations

The bounded decoder is used by the current Bybit L2 live application because
it reproduced the SAX result on the captured corpus and showed a large,
order-balanced local improvement. It remains specific to this documented input
format.

The numbers exclude network delivery and WebSocket read wait. WSL2 scheduler,
frequency, background activity and virtualized CPU topology can affect absolute
timing. Treat the measurements as a controlled local comparison, not as a
universal latency limit.
