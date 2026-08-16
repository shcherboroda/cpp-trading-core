# Bybit L2 specialized decoder experiment

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
- **One-pass bounded decoder:** `decode_bybit_l2_bounded_one_pass` validates
  the same envelope while locating top-level fields in one traversal, then
  reuses the bounded level-array parser. It is experimental; set
  `BYBIT_L2_DECODER=one-pass` to select it for a controlled comparison.

The bounded decoder is deliberately not a general JSON parser. It supports the
known Bybit L2 schema and does not claim to accept arbitrary JSON encodings or
future undocumented protocol variants. The SAX decoder remains available as a
reference implementation.

## Correctness checks

- Unit tests cover snapshot and delta messages, zero-quantity deletion levels,
  subscription acknowledgement handling, malformed levels, metadata extraction
  and fixed-point ticks.
- `trading_bench_bybit_l2_replay ... verify-bounded` compares SAX, bounded and
  one-pass bounded decoding frame by frame.
- The verifier passed on a captured corpus of 1,000 sequential public
  BTCUSDT frames: one subscription acknowledgement, one snapshot and 998
  deltas. For every frame, parse success, `topic`, `type`, `ts`, `cts`, bids
  and asks matched.
- A public 20-WebSocket-message smoke run after live adoption processed one
  snapshot and 18 deltas without decoder errors. This is a functional smoke
  test, not a latency measurement.
- The three decoders matched on a second, fresh 200-frame capture (one
  acknowledgement, one snapshot, 198 deltas). An ABBA series of six live
  connections also processed that message mix with both bounded variants and
  no decode errors.

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

## One-pass follow-up

The original bounded parser scanned the top-level message more than once. A
diagnostic breakdown ranked that envelope work above level-array parsing, so a
one-pass candidate was tested without relaxing validation.

Two complementary comparisons used the fresh 200-frame capture. Both ran on
Linux CPU 0 in Release mode and alternated order as bounded → one-pass →
one-pass → bounded → bounded → one-pass.

| Input cadence | Runs/variant | Repeats/run | Bounded median total p50/p99 | One-pass median total p50/p99 | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| Hot fixed replay | 30 | 100 | 0.955 / 2.326 us | 0.795 / 2.307 us | -17% / -1% |
| Replay with 20 ms inter-frame pause | 3 | 2 | 9.912 / 40.192 us | 10.400 / 38.216 us | +5% / -5% |

The paced replay is the more relevant local CPU result for a depth-50 feed:
the pause allows instruction and data caches to cool between messages. It does
not show a stable one-pass win after strict structural validation was added, so
the accepted bounded decoder remains the live default. Six real WebSocket smoke
runs were correct, but their per-run handler medians were not used as a speed
comparison: live update sizes and host/network state differed between
connections.

Raw result directories:

- `results/bybit-l2-replay-20260816T121110Z/`
- `results/bybit-l2-replay-20260816T121111Z/`
- `results/bybit-l2-paced-abba-20260816T121149034744304Z/`

### Post-merge replication

After the review fixes, the merged `main` was retested with seven paced runs
per variant (same 200-frame capture, CPU 0, two repeats, 20-ms inter-frame
pause). The median total p50/p99 was 8.790/22.848 microseconds for bounded and
8.892/26.134 microseconds for one-pass. This replication confirms the earlier
decision: one-pass is not a speed improvement under the more realistic cadence
and remains experimental.

Raw result directory: `results/bybit-l2-paced-abba-main-20260816T122947382496280Z/`.

## Decision and limitations

The bounded decoder remains the current Bybit L2 live implementation. The
one-pass variant reproduced the parser output on two captured corpora, but is
not adopted because the paced replay does not establish a speed improvement
after its strict structural checks. It remains available behind
`BYBIT_L2_DECODER=one-pass` for further experiments.

The numbers exclude network delivery and WebSocket read wait. WSL2 scheduler,
frequency, background activity and virtualized CPU topology can affect absolute
timing. Treat the measurements as a controlled local comparison, not as a
universal latency limit.
