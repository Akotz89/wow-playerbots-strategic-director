# Playerbots strategic director pure core

This is the implementation-neutral CPI-207 decision kernel. It is intentionally
separate from AzerothCore while CPI-191 owns the active server build and live realm.

The core provides:

- atomic, owner-scoped activity reservations so solo objectives, quest groups,
  LFG, guild work, and world PvP cannot claim the same character concurrently;
- hard phase, level, group, earned-source, capability, and retry-window gates;
- transparent fixed-integer scoring;
- persistent-failure backoff semantics;
- exact-context character/cohort/global failure aggregation with versioned decay;
- deterministic, input-order-independent selection;
- complete evaluation details for `why` and replay tooling.

The route layer adds immutable route identity, hard and any-of prerequisites,
exclusive alternatives, explicit skip semantics, objective clusters, typed content
references, and authoritative quest-history synchronization with a missing-
prerequisite repair frontier. Plan decisions explicitly distinguish a selected
guide objective, safe stock-New-RPG fallback, and deference to existing control.
An exact-identity active native quest preserves a valid persisted skip while still
seeding repair for its missing prerequisites; the skipped objective stays hidden.
A valid skipped exclusive branch satisfies dependents only when its explicit
`skippedSatisfiesDependents` flag is true, independently of a completed sibling;
the sibling still prevents pursuing that branch.

The earned-gear layer is designed to bind a reverse source index to the exact
content-graph digest and evaluates quest, drop, vendor, craft, PvP, and dungeon opportunities
using Playerbots-supplied upgrade utility, rational acquisition probability,
expected attempts/time, cost, party readiness, replacement horizon, risk,
contention, and profession/dependency value. Its outputs are pursue/defer/abandon
explanations only; it has no item-granting API.

Caller-created source vectors are available only through the explicitly named
`UnverifiedForTesting` factory. The normal planner rejects them with
`unverified_source_index`, even if they repeat a legitimate graph digest. Only the
private `CanonicalItemSourceLoader` seam can create a verified index. Its concrete
offline loader validates pinned projections while retaining an unverified index;
`CanonicalGearPlanningService::PlanOffline` evaluates that validated offline
capsule. The accepted effective-dataset registry is empty, so exact frozen-realm
projection acceptance remains open CPI-211 work. Offline fixture assertions do
not establish realm truth.

The package also contains the versioned persistent domain schemas, replay
envelopes, route catalogue schema, and the deterministic normalized-extract to
`content-graph-v1` compiler. The compiler enforces typed endpoint closure,
bounded condition graphs, availability invariants, canonical ordering, and
byte-stable SHA-256 output. The exact MySQL/DBC extractor remains gated on the
CPI-191 frozen-candidate handoff. A passing offline planner test is not exact-realm
index acceptance.

The decision kernel has no filesystem, database, networking, clock, random,
Playerbots or AzerothCore dependency. The replay/persistence library additionally
uses Boost.JSON, OpenSSL SHA-256 and POSIX filesystem calls for the optional local
offline plan store described below. Neither layer grants items or mutates a character.

After CPI-191 hands off, this code will move behind a thin adapter in
`mod-playerbots/src/Guide/`. The first integrated version cannot be a clean sibling
module because the exact Playerbots pin does not expose public registration for AI
creator contexts.

## Local offline plan restoration

`pbguide/gear_plan_json.hpp` provides `GearPlanToJson` and `GearPlanFromJson`.
The inverse returns every typed plan field after strict duplicate-key, integer,
schema, link, eligibility, score and deterministic selection validation. Policy
versions and full identities retain the nonempty strings supported by the policy
constructor, including custom versions over 160 characters; their bound is the
complete JSON document rather than the source-revision string limit.

`pbguide/gear_plan_store.hpp` provides the concrete `OfflineGearPlanStore` API.
Provision an absolute store directory owned by the current UID with mode 0700;
for example, inside a caller-owned private run directory:

```bash
install -d -m 0700 /absolute/private/run/gear-plans
```

Then save through the real loader and planner:

```cpp
pbguide::OfflineGearPlanStore store("/absolute/private/run/gear-plans");
auto saved = store.PlanAndSaveOfflineSources(graphBytes, manifestBytes,
    projectionBytes, expectedPins, snapshot, resources, targets, policy);
```

In a fresh process, independently obtain current source bytes, expected pins,
snapshot identity and validated policy, then load the historical record:

```cpp
auto loaded = pbguide::CanonicalItemSourceLoader::LoadOffline(
    currentGraphBytes, currentManifestBytes, currentProjectionBytes, expectedPins);
auto restored = store.Load(expectedPlanId, loaded, currentSnapshot, currentPolicy);
```

The full envelope includes OfflineContract, all five source pins, blocked reasons,
all plan members and a deterministic corruption checksum. The complete encoded
record, including metadata and checksum, is bounded to 16 MiB, shared with the
strict JSON parser; target/evaluation limits also apply. The checksum is not
writer authentication. Store files must be current-UID 0600 regular single-link
files; symlink path components, hardlinks, special files and broad permissions are
rejected. The store writes unique same-directory 0600 temporary files, fsyncs,
renames atomically, fsyncs the directory and verifies typed readback.

`GearPlanStoreError::Code()` distinguishes `not_found`, `invalid_record`,
`invalid_context`, `unsafe_store`, `io_error` and `commit_uncertain`. Filesystem
failures include bounded operation names and numeric errno without paths or
payloads. Structural symlink/not-directory/missing-required-directory failures are
unsafe; environmental errors such as EIO or descriptor exhaustion remain I/O
errors. A failure before rename leaves the existing record intact. After rename,
a failed flush or readback reports `commit_uncertain` and preserves its original
cause: perform a new normal `Load` and validate the actual committed record.
There is no automatic corrupt-record reset, temporary-file promotion or TTL.

The loaded result is a historical offline decision, even when its identities match
current inputs. Current resources, inventory, capabilities, phase and other native
execution conditions must be revalidated or replanned before executing it. Frozen
capsule source identity and immutable blocked flags are checked without recomputing
mutable historical facts. The store neither restores a verified source index nor
establishes effective realm acceptance, concurrent-writer coordination, hostile
same-UID isolation or physical power-loss recovery.

## Build and test

Do not run this build while another AzerothCore build is active on the host.

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Schema validation additionally uses `requirements-test.txt`. All checked-in tests
are offline with respect to the realm: they do not start services or connect to a
database.

`sql/playerbots/001_guide_failure_memory.*.sql` is an unapplied migration contract.
It must not be applied until CPI-212 adds exact-pin prepared statements and passes
the isolated migration/startup/restart/rollback rehearsal after CPI-191 handoff.
