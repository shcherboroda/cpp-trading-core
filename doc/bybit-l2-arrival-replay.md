# Bybit L2 arrival/burst replay

## Terms

- **Test stand**: the standalone replay program used to run the same load
  profile repeatedly. The source code calls it a `harness`; this note uses the
  plainer term *test stand*.
- **Batch**: several messages assigned the same virtual arrival time. The
  source code and CSV retain `burst` for a stable machine-readable name.
- **Unfinished work at the next batch**: the queue delay on the first message
  of a batch. It means the prior batch was not fully processed before the next
  one arrived. The code/CSV call this `burst-start queue delay`.

## Question

Can the current single-threaded bounded-decoder path drain a controlled stream
of Bybit L2 frames before the next burst arrives, or does backlog persist and
justify a reader-to-engine queue experiment?

## Model and limits

`trading_bench_bybit_l2_arrival_replay` replays the fixed 1,000-frame BTCUSDT
corpus. The subscription acknowledgement is ignored; each run processes 999
orderbook frames per corpus pass. For every valid frame it measures actual
bounded-decode plus orderbook-apply service time, then feeds that duration to
a virtual single-server queue.

`burst-size` frames arrive as one batch at the same virtual timestamp. The
first frame of the next batch arrives exactly `burst-gap-ns` later. The test stand never sleeps,
contacts the network, or claims to emulate a kernel receive queue. Its purpose
is deterministic capacity modelling from measured local service time, without
WSL scheduler wake-up noise in the arrival schedule.

The key metric is **unfinished work at the next batch**: queue delay on the
first frame of a batch. A non-zero value means work from the prior batch was
not drained. Queue delay inside a batch is expected even when the system
catches up before the next batch.

## Reproduction

```bash
./scripts/build_release.sh
CPU=0 ./scripts/collect_bybit_l2_arrival_replay.sh \
  results/bybit-l2-corpus-20260808T120000Z/frames.ndjson 7
```

The collector emits one compact CSV row per run/profile plus environment
metadata. Default profiles are burst sizes 1, 8, and 16 with inter-burst gaps
of 1 ms, 100 us, and 10 us.

## Initial CPU-0 result

Seven runs per profile, 20 corpus repeats per run, were collected in
`results/bybit-l2-arrival-replay-20260816T092345Z/`. Median service p50 across
profiles was 0.877–0.952 us.

| Burst size | Gap | Median burst-start p99 | Interpretation |
| ---: | ---: | ---: | --- |
| 8 | 1 ms | 0 ns | Previous burst consistently drained. |
| 16 | 1 ms | 0 ns | Previous burst consistently drained. |
| 8 | 100 us | 0 ns | No persistent backlog at p99; rare max spillover is retained in raw CSV. |
| 16 | 100 us | 0 ns | No persistent backlog at p99; rare max spillover is retained in raw CSV. |
| 8 | 10 us | 722.505 us | Tail work causes persistent spillover. |
| 16 | 10 us | 9.339 ms | Offered burst load exceeds this single-server model's capacity. |

Steady one-frame profiles at 10 us had burst-start p99 of zero but occasional
0.064–1.052 ms maxima across runs. These rare excursions must not be relabeled
as sustained overload; snapshot work and WSL scheduling remain contributors.

## Decision

The current depth-50 single-symbol live observations do not yet justify an
SPSC pipeline. The harness now provides its admission test: compare a queue
variant only under profiles with non-zero burst-start p99, and report both
end-to-end virtual completion and queue spillover. A future multi-symbol
corpus can reuse the same model with a real captured arrival profile.
