# Test fixtures

Fixtures are generated, not committed. `scripts/make-fixtures.sh` writes small
BAMs to a temporary directory:

```sh
./scripts/make-fixtures.sh /tmp/husk-fixtures
```

The set covers flags, CIGAR operations, tags, missing and malformed `RG`,
multi-sample headers, empty BAMs, unsorted input, unmapped reads, and a
long CIGAR stored as `CG:B:I`.

CTest calls the generator from `tests/integration/run.sh`, so the repository
stays free of binary test data.
