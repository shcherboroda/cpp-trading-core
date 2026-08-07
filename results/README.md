# Benchmark Result Archive

Each directory contains raw CSV for every run plus the exact command,
environment, binary hash, source hashes, git status, and CMake cache settings.
Only these compact measurement records belong here; build directories and other
generated artifacts do not.

## Status

| Result directories | Status | Purpose |
| --- | --- | --- |
| `mt-baseline-20260805T100655Z`, `mt-baseline-20260805T100700Z` | historical baseline | First seven-run `pre-push` baseline and latency-off control. |
| `mt-baseline-20260805T100550Z`, `mt-baseline-20260805T100609Z` | smoke only | Collector validation; not used for comparison. |
| `mt-spsc-padded-indices-20260807T144424Z`, `mt-spsc-padded-indices-off-20260807T144428Z` | preliminary | Different-day comparison; retained, not used for conclusions. |
| `mt-spsc-{un,p}added-paired-*`, `mt-spsc-{un,p}added-reverse-*` | controlled comparison | Balanced order comparison of cache-line-separated SPSC indices. |
| `mt-backoff-{yield,pause}-{paired,reverse}-20260807T1459*` | controlled comparison | Balanced order comparison of `yield` and x86 `pause` queue backoff. |
| `mt-backoff-{yield,pause}-quiet-{paired,reverse}-20260807T1509*` | quiet replication | Same balanced backoff comparison after interactive desktop applications were closed. |
| `mt-affinity-{distinct,sibling}-{paired,reverse}-20260807T1518*` | controlled placement | Explicit producer/consumer thread pinning: `0,2` distinct WSL virtual cores versus `0,1` sibling vCPUs. |
| `mt-padding-{unpadded,padded}-affinity-{paired,reverse}-20260807T1524*` | controlled layout | Cache-line-separated indices with explicit `producer=0, consumer=2` pinning. |
| `mt-baseline-20260807T152956Z`, `...152959Z`, `...153002Z`, `...153005Z` | controlled ownership | ABBA copy-versus-move `TimedEvent` transfer with explicit `producer=0, consumer=2` pinning; copy is not beaten. |

The controlled comparison is four blocks of seven runs: unpadded → padded and
padded → unpadded for both `pre-push` and latency-off modes. See
`doc/performance.md` for the derived table and interpretation.
