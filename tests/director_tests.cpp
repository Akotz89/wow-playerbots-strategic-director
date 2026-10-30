#include "pbguide/activity_reservations.hpp"
#include "pbguide/director.hpp"
#include "pbguide/failure_memory.hpp"
#include "pbguide/gear_planner.hpp"
#include "pbguide/gear_plan_json.hpp"
#include "pbguide/route_graph.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/json.hpp>

namespace
{
int failures = 0;

void Check(bool condition, std::string const& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool HasReason(pbguide::CandidateEvaluation const& evaluation, std::string const& reason)
{
    return std::find(evaluation.rejectionReasons.begin(), evaluation.rejectionReasons.end(), reason) !=
        evaluation.rejectionReasons.end();
}

pbguide::BotProgressSnapshot BaseSnapshot()
{
    pbguide::BotProgressSnapshot snapshot;
    snapshot.characterId = 42;
    snapshot.level = 8;
    snapshot.phase = 1;
    snapshot.groupSize = 1;
    snapshot.mapId = 0;
    snapshot.zoneId = 12;
    snapshot.nowSeconds = 10'000;
    snapshot.coreRevision = "9fb906bb";
    snapshot.playerbotsRevision = "2f7d9f77";
    snapshot.contentRevision = "phase-1-19-v1";
    snapshot.contentGraphDigest = "1111111111111111111111111111111111111111111111111111111111111111";
    snapshot.routeId = "route:northshire-1-10";
    snapshot.routeRevision = "northshire-v1";
    snapshot.catalogueDigest = "0000000000000000000000000000000000000000000000000000000000000783";
    snapshot.profileId = "profile:natural";
    snapshot.profileVersion = "profile-v1";
    snapshot.capabilityFingerprint = "caps-v1";
    snapshot.populationAdmitted = true;
    snapshot.scheduleActive = true;
    snapshot.capabilities = {
        {"quest_kill", true},
        {"quest_gameobject", false},
        {"flight_path", true},
    };
    return snapshot;
}

pbguide::FailureEvidence Failure(
    pbguide::BotProgressSnapshot const& snapshot,
    std::string stableId,
    std::uint32_t failureCount,
    std::int64_t lastFailureSeconds,
    std::string classification,
    pbguide::EvidenceScope scope = pbguide::EvidenceScope::Character)
{
    pbguide::FailureEvidence failure;
    failure.stableId = std::move(stableId);
    failure.scope = scope;
    failure.characterId = snapshot.characterId;
    failure.phase = snapshot.phase;
    failure.mapId = snapshot.mapId;
    failure.zoneId = snapshot.zoneId;
    failure.groupSize = snapshot.groupSize;
    failure.failureCount = failureCount;
    failure.lastFailureSeconds = lastFailureSeconds;
    failure.classification = std::move(classification);
    failure.coreRevision = snapshot.coreRevision;
    failure.playerbotsRevision = snapshot.playerbotsRevision;
    failure.contentRevision = snapshot.contentRevision;
    failure.contentGraphDigest = snapshot.contentGraphDigest;
    failure.profileId = snapshot.profileId;
    failure.capabilityFingerprint = snapshot.capabilityFingerprint;
    return failure;
}

pbguide::RouteIdentity RouteIdentity()
{
    return {"route:northshire-1-10", "northshire-v1",
        "0000000000000000000000000000000000000000000000000000000000000783"};
}

pbguide::GuideProgress Progress()
{
    pbguide::GuideProgress progress;
    progress.identity = RouteIdentity();
    return progress;
}

pbguide::ObjectiveCandidate Candidate(std::string id);

pbguide::GuideStep QuestStep(
    std::string stableId,
    std::uint32_t questId,
    std::vector<std::string> prerequisites = {})
{
    auto objective = Candidate(stableId);
    return pbguide::GuideStep(std::move(stableId), std::move(objective), std::move(prerequisites), false,
        false, {{pbguide::ContentKind::Quest, questId}});
}

pbguide::ItemSourceOption ItemSource(
    std::string stableId,
    std::uint32_t itemId,
    pbguide::ItemSourceKind kind = pbguide::ItemSourceKind::QuestReward)
{
    pbguide::ItemSourceOption source;
    source.stableId = std::move(stableId);
    source.itemId = itemId;
    source.kind = kind;
    return source;
}

pbguide::ItemUpgradeTarget UpgradeTarget(
    std::uint32_t itemId,
    pbguide::Score score = 10,
    std::uint32_t replacementLevel = 10)
{
    pbguide::ItemUpgradeTarget target;
    target.stableId = "target:item-" + std::to_string(itemId) + "-slot-15";
    target.itemId = itemId;
    target.slot = 15;
    target.playerbotsUpgradeScore = score;
    target.expectedReplacementLevel = replacementLevel;
    return target;
}

pbguide::EarnedGearPlanner TestGearPlanner()
{
    return pbguide::EarnedGearPlanner({}, true);
}

pbguide::ObjectiveCandidate Candidate(std::string id)
{
    pbguide::ObjectiveCandidate candidate;
    candidate.stableId = std::move(id);
    candidate.kind = pbguide::ObjectiveKind::Quest;
    candidate.xpValue = 10;
    candidate.travelSeconds = 30;
    candidate.executionSeconds = 120;
    candidate.riskMilli = 100;
    return candidate;
}

void TestScoreAndExplanation()
{
    pbguide::StrategicGuideDirector director;
    auto candidate = Candidate("quest:7");
    candidate.unlockValue = 2;
    candidate.gearValue = 3;
    candidate.continuityValue = 1;

    auto const result = director.Evaluate(BaseSnapshot(), candidate);
    Check(result.eligible, "ordinary phase-legal quest should be eligible");
    Check(result.score.xp == 1'000, "XP contribution should be explicit");
    Check(result.score.unlocks == 400, "unlock contribution should be explicit");
    Check(result.score.gear == 450, "gear contribution should be explicit");
    Check(result.score.travel == -60, "travel cost should be explicit");
    Check(result.score.total == 1'710, "total should equal the documented integer formula");
}

void TestDeterministicTieBreakIsInputOrderIndependent()
{
    pbguide::StrategicGuideDirector director;
    std::vector<pbguide::ObjectiveCandidate> candidates = {Candidate("quest:z"), Candidate("quest:a")};
    auto const first = director.Select(BaseSnapshot(), candidates);
    std::reverse(candidates.begin(), candidates.end());
    auto const second = director.Select(BaseSnapshot(), candidates);

    Check(first.selectedStableId == std::optional<std::string>("quest:a"),
        "lexical stable ID should break equal-score ties");
    Check(first.selectedStableId == second.selectedStableId,
        "candidate input ordering must not affect selection");
}

void TestLegalityAndCapabilityGates()
{
    pbguide::StrategicGuideDirector director;
    auto synthetic = Candidate("item:synthetic");
    synthetic.kind = pbguide::ObjectiveKind::ItemSource;
    synthetic.earnedSource = false;
    auto futurePhase = Candidate("quest:future");
    futurePhase.minimumPhase = 2;
    auto unsupported = Candidate("quest:gameobject");
    unsupported.requiredCapabilities = {"quest_gameobject"};

    auto const decision = director.Select(BaseSnapshot(), {synthetic, futurePhase, unsupported});
    Check(!decision.selectedStableId.has_value(), "all illegal candidates should yield no plan");
    Check(decision.disposition == pbguide::PlanDisposition::StockNewRpgFallback,
        "an admitted safe bot with no guide-eligible objective should explicitly fall back to stock New RPG");
    Check(HasReason(decision.evaluations[0], "phase_out_of_range") ||
            HasReason(decision.evaluations[1], "phase_out_of_range") ||
            HasReason(decision.evaluations[2], "phase_out_of_range"),
        "future content should carry a phase rejection");
    bool sawSynthetic = false;
    bool sawCapability = false;
    for (auto const& evaluation : decision.evaluations)
    {
        sawSynthetic = sawSynthetic || HasReason(evaluation, "unearned_or_synthetic_source");
        sawCapability = sawCapability || HasReason(evaluation, "missing_capability:quest_gameobject");
    }
    Check(sawSynthetic, "synthetic rewards must be hard rejected");
    Check(sawCapability, "unsupported objective mechanics must be hard rejected");
}

void TestFailureBackoffAndPostBackoffPenalty()
{
    pbguide::StrategicGuideDirector director;
    auto snapshot = BaseSnapshot();
    auto candidate = Candidate("quest:stalled");
    auto failure = Failure(snapshot, "quest:stalled", 3, 9'000, "no_progress");

    snapshot.nowSeconds = 10'000;
    auto const blocked = director.Evaluate(snapshot, candidate, &failure);
    Check(!blocked.eligible, "third failure should still be in its exponential retry window");
    Check(blocked.retryAtSeconds == 10'200, "third failure delay should be 1,200 seconds");
    Check(HasReason(blocked, "failure_backoff_active"), "active retry window should be explained");

    snapshot.nowSeconds = 10'200;
    auto const eligible = director.Evaluate(snapshot, candidate, &failure);
    Check(eligible.eligible, "candidate should become eligible exactly at retry time");
    Check(eligible.score.priorFailures == -1'500, "prior failures should remain a selection cost");
}

void TestFailureEvidenceIsRevisionScoped()
{
    pbguide::StrategicGuideDirector director;
    auto snapshot = BaseSnapshot();
    snapshot.coreRevision = "core:new";
    snapshot.playerbotsRevision = "bots:new";
    snapshot.contentRevision = "phase:1";
    auto oldSnapshot = snapshot;
    oldSnapshot.coreRevision = "core:old";
    oldSnapshot.playerbotsRevision = "bots:old";
    auto oldFailure = Failure(oldSnapshot, "quest:stalled", 6, 9'999, "no_progress");

    auto const result = director.Evaluate(snapshot, Candidate("quest:stalled"), &oldFailure);
    Check(result.eligible, "failure evidence from a different executor revision must not block a retry");
    Check(result.score.priorFailures == 0, "stale revision failure must not penalize the score");
}

void TestSelectUsesLatestMatchingFailureEvidence()
{
    pbguide::StrategicGuideDirector director;
    auto snapshot = BaseSnapshot();
    snapshot.coreRevision = "core:new";
    snapshot.playerbotsRevision = "bots:new";
    snapshot.contentRevision = "phase:1";
    snapshot.nowSeconds = 10'000;
    std::vector<pbguide::FailureEvidence> evidence = {
        Failure(snapshot, "quest:stalled", 1, 9'000, "matching"),
    };
    auto oldSnapshot = snapshot;
    oldSnapshot.coreRevision = "core:old";
    oldSnapshot.playerbotsRevision = "bots:old";
    evidence.insert(evidence.begin(), Failure(oldSnapshot, "quest:stalled", 1, 9'990, "old_revision"));

    auto const decision = director.Select(snapshot, {Candidate("quest:stalled")}, evidence);
    Check(decision.selectedStableId == std::optional<std::string>("quest:stalled"),
        "newer evidence from another revision must not hide eligible matching-revision evidence");
    Check(decision.evaluations.front().score.priorFailures == -500,
        "matching-revision history should still contribute its documented cost");
}

void TestHumanControlAndUnsafeStatesSuspendGuide()
{
    pbguide::StrategicGuideDirector director;
    auto snapshot = BaseSnapshot();
    snapshot.hasHumanMaster = true;
    auto const human = director.Evaluate(snapshot, Candidate("quest:1"));
    Check(HasReason(human, "guide_control_suspended"), "human master should suspend guide control");
    auto const humanDecision = director.Select(snapshot, {Candidate("quest:1")});
    Check(humanDecision.disposition == pbguide::PlanDisposition::DeferExistingControl,
        "a human-controlled bot must defer to existing control rather than starting stock autonomous fallback");

    snapshot.hasHumanMaster = false;
    snapshot.inCombat = true;
    auto const combat = director.Evaluate(snapshot, Candidate("quest:1"));
    Check(HasReason(combat, "guide_control_suspended"), "combat should suspend replanning");
}

void TestAdmissionScheduleAndSnapshotIdentityGates()
{
    pbguide::StrategicGuideDirector director;
    auto snapshot = BaseSnapshot();
    snapshot.populationAdmitted = false;
    auto const notAdmitted = director.Evaluate(snapshot, Candidate("quest:1"));
    Check(HasReason(notAdmitted, "population_not_admitted"),
        "the strategic director must not bypass population admission");

    snapshot.populationAdmitted = true;
    snapshot.scheduleActive = false;
    auto const offSchedule = director.Evaluate(snapshot, Candidate("quest:1"));
    Check(HasReason(offSchedule, "schedule_inactive"),
        "the strategic director must not bypass the persistent cast schedule");

    snapshot.scheduleActive = true;
    snapshot.catalogueDigest[0] = 'G';
    auto const invalidIdentity = director.Evaluate(snapshot, Candidate("quest:1"));
    Check(HasReason(invalidIdentity, "invalid_snapshot_identity"),
        "route/catalogue identity must use a canonical lowercase SHA-256 digest");
}

void TestFailureEvidenceScopeAndCorruptionHandling()
{
    pbguide::StrategicGuideDirector director;
    auto snapshot = BaseSnapshot();
    auto candidate = Candidate("quest:scoped");

    auto anotherProfile = snapshot;
    anotherProfile.profileId = "profile:other";
    auto characterFailure = Failure(anotherProfile, candidate.stableId, 1, 9'000, "no_progress");
    characterFailure.characterId = snapshot.characterId;
    auto const profileChanged = director.Evaluate(snapshot, candidate, &characterFailure);
    Check(profileChanged.eligible && profileChanged.score.priorFailures == 0,
        "character failure evidence from another behavior profile must not leak into this profile");

    auto cohortFailure = Failure(snapshot, candidate.stableId, 1, 9'000, "mechanic_unsupported",
        pbguide::EvidenceScope::Cohort);
    cohortFailure.characterId = 999;
    auto const cohort = director.Evaluate(snapshot, candidate, &cohortFailure);
    Check(cohort.eligible && cohort.score.priorFailures == -500,
        "cohort evidence should transfer only within the same exact profile and execution context");

    auto otherZoneFailure = cohortFailure;
    otherZoneFailure.zoneId = 14;
    auto const differentZone = director.Evaluate(snapshot, candidate, &otherZoneFailure);
    Check(differentZone.eligible && differentZone.score.priorFailures == 0,
        "failure evidence from another zone must not poison a valid local objective");

    auto corrupt = Failure(snapshot, candidate.stableId, 1, 9'000, "no_progress");
    corrupt.capabilityFingerprint.clear();
    auto const decision = director.Select(snapshot, {candidate}, {corrupt});
    Check(!decision.selectedStableId.has_value() &&
            HasReason(decision.evaluations.front(), "invalid_failure_evidence"),
        "corrupt persistent evidence for an objective must fail closed and remain explainable");
}

void TestFailureDecayAndFutureEvidence()
{
    pbguide::StrategicGuideDirector director;
    auto snapshot = BaseSnapshot();
    snapshot.nowSeconds = 500'000;
    auto candidate = Candidate("quest:decay");
    auto failure = Failure(snapshot, candidate.stableId, 3,
        snapshot.nowSeconds - (2 * 86'400), "no_progress");

    auto const partiallyDecayed = director.Evaluate(snapshot, candidate, &failure);
    Check(partiallyDecayed.eligible && partiallyDecayed.score.priorFailures == -500,
        "one failure should remain after two daily decay intervals from a count of three");

    snapshot.nowSeconds += 86'400;
    auto const fullyDecayed = director.Evaluate(snapshot, candidate, &failure);
    Check(fullyDecayed.eligible && fullyDecayed.score.priorFailures == 0 &&
            fullyDecayed.retryAtSeconds == 0,
        "failure evidence should stop penalizing and backing off after its versioned decay horizon");

    auto future = Failure(snapshot, candidate.stableId, 1, snapshot.nowSeconds + 1, "no_progress");
    auto const invalidFuture = director.Evaluate(snapshot, candidate, &future);
    Check(!invalidFuture.eligible && HasReason(invalidFuture, "invalid_failure_evidence"),
        "future-dated failure evidence must fail closed instead of creating an unbounded retry window");
}

void TestFailureMemoryAggregationScopingAndLoadValidation()
{
    auto snapshot = BaseSnapshot();
    pbguide::FailureMemory memory;
    auto const first = memory.Record(snapshot, "quest:memory", "no_progress");
    Check(first.failureCount == 1, "the first exact-context failure observation should create count one");
    snapshot.nowSeconds += 10;
    auto const second = memory.Record(snapshot, "quest:memory", "path_unreachable");
    Check(second.failureCount == 2 && second.classification == "path_unreachable",
        "a later exact-context observation should increment and retain the latest classification");

    auto anotherCharacter = snapshot;
    anotherCharacter.characterId = 99;
    Check(memory.Applicable(anotherCharacter).empty(),
        "character-scoped failure memory must not cross character identity");

    static_cast<void>(
        memory.Record(snapshot, "quest:cohort", "mechanic_unsupported", pbguide::EvidenceScope::Cohort));
    auto cohortApplicable = memory.Applicable(anotherCharacter);
    Check(cohortApplicable.size() == 1 && cohortApplicable.front().stableId == "quest:cohort",
        "cohort failure memory should cross characters only in the same exact profile/context");
    anotherCharacter.profileId = "profile:other";
    Check(memory.Applicable(anotherCharacter).empty(),
        "cohort failure memory must not cross behavior profiles");

    anotherCharacter = snapshot;
    anotherCharacter.coreRevision = "core:new";
    Check(memory.Applicable(anotherCharacter).empty(),
        "failure memory must remain scoped to the exact executor revision");

    bool duplicateLoadRejected = false;
    try
    {
        pbguide::FailureMemory invalid({memory.Entries().front(), memory.Entries().front()});
        static_cast<void>(invalid);
    }
    catch (std::invalid_argument const&)
    {
        duplicateLoadRejected = true;
    }
    Check(duplicateLoadRejected, "duplicate persisted canonical failure keys must be rejected on load");

    bool decreasingTimeRejected = false;
    try
    {
        auto earlier = snapshot;
        earlier.nowSeconds -= 100;
        static_cast<void>(memory.Record(earlier, "quest:memory", "no_progress"));
    }
    catch (std::invalid_argument const&)
    {
        decreasingTimeRejected = true;
    }
    Check(decreasingTimeRejected, "failure observations must be monotonic inside one persisted context");
}

void TestInvalidInputsAndDuplicateIdsFailClosed()
{
    pbguide::StrategicGuideDirector director;
    auto negativeCost = Candidate("quest:negative");
    negativeCost.travelSeconds = -1;
    auto const invalid = director.Evaluate(BaseSnapshot(), negativeCost);
    Check(!invalid.eligible && HasReason(invalid, "invalid_candidate_data"),
        "negative costs must be rejected rather than increasing the score");

    auto malformedId = Candidate("missing-separator");
    auto const malformed = director.Evaluate(BaseSnapshot(), malformedId);
    Check(!malformed.eligible && HasReason(malformed, "invalid_stable_id"),
        "stable IDs that cannot round-trip through the schema must be rejected");

    auto duplicate = Candidate("quest:duplicate");
    auto const duplicates = director.Select(BaseSnapshot(), {duplicate, duplicate});
    Check(!duplicates.selectedStableId.has_value(), "duplicate objective IDs must not produce an ambiguous plan");
    Check(HasReason(duplicates.evaluations.front(), "duplicate_candidate_id") &&
            HasReason(duplicates.evaluations.back(), "duplicate_candidate_id"),
        "every duplicate candidate should explain the rejection");
}

void TestOverflowFailsClosed()
{
    pbguide::StrategicGuideDirector director;
    auto huge = Candidate("quest:huge");
    huge.xpValue = std::numeric_limits<pbguide::Score>::max();
    auto const evaluation = director.Evaluate(BaseSnapshot(), huge);
    Check(!evaluation.eligible && HasReason(evaluation, "score_overflow"),
        "score multiplication overflow must fail closed without signed undefined behavior");

    bool badPolicyRejected = false;
    try
    {
        pbguide::ScoringPolicy policy;
        policy.maximumBackoffDoublings = 63;
        pbguide::StrategicGuideDirector invalid(policy);
        static_cast<void>(invalid);
    }
    catch (std::invalid_argument const&)
    {
        badPolicyRejected = true;
    }
    Check(badPolicyRejected, "overflowing backoff policies should be rejected at construction");

    bool reservedPolicyRejected = false;
    try
    {
        pbguide::ScoringPolicy policy;
        policy.xpWeight = 101;
        pbguide::StrategicGuideDirector invalid(policy);
        static_cast<void>(invalid);
    }
    catch (std::invalid_argument const&)
    {
        reservedPolicyRejected = true;
    }
    Check(reservedPolicyRejected, "the cpi-207-v1 identity must bind one immutable set of constants");
}

void TestPartyRequirementAndGroupOpportunity()
{
    pbguide::StrategicGuideDirector director;
    auto dungeon = Candidate("dungeon:deadmines");
    dungeon.kind = pbguide::ObjectiveKind::Dungeon;
    dungeon.requiredGroupSize = 5;
    dungeon.groupSynergyValue = 20;

    auto snapshot = BaseSnapshot();
    auto const solo = director.Evaluate(snapshot, dungeon);
    Check(HasReason(solo, "party_not_ready"), "dungeon should not be selected for an incomplete party");

    snapshot.groupSize = 5;
    auto const party = director.Evaluate(snapshot, dungeon);
    Check(party.eligible, "role-ready adapter may expose a complete party candidate");
    Check(party.score.groupSynergy == 1'000, "group opportunity should have an explicit contribution");
}

void TestActivityReservationsAreAtomicAndDeterministic()
{
    pbguide::ActivityReservationBook reservations;
    pbguide::ActivityReservationRequest first{
        "quest_group:westfall-001",
        "director:ambient",
        pbguide::ActivityKind::OutdoorQuestGroup,
        {30, 10, 20},
        1'200,
    };
    auto const acquired = reservations.Acquire(first, 1'000);
    Check(acquired.acquired && reservations.Entries().size() == 3,
        "a valid activity must reserve every requested character atomically");

    pbguide::ActivityReservationRequest conflicting{
        "lfg:deadmines-001",
        "director:lfg",
        pbguide::ActivityKind::DungeonLfg,
        {40, 20},
        1'300,
    };
    auto const rejected = reservations.Acquire(conflicting, 1'001);
    Check(!rejected.acquired && rejected.rejectionReason == "character_activity_conflict" &&
            rejected.conflictingCharacterIds == std::vector<std::uint64_t>{20} &&
            !reservations.ActiveFor(40, 1'001).has_value(),
        "one busy character must reject the whole group without partially reserving free members");

    first.expiresAtSeconds = 1'400;
    auto const renewed = reservations.Acquire(first, 1'100);
    Check(renewed.acquired && reservations.ActiveFor(10, 1'201)->expiresAtSeconds == 1'400,
        "an exact owner/activity replay must be idempotent and may renew its reservation");

    auto const entries = reservations.Entries();
    Check(entries[0].characterId == 10 && entries[1].characterId == 20 && entries[2].characterId == 30,
        "persisted reservation output must use canonical character order");
}

void TestActivityReservationExpiryAndOwnership()
{
    pbguide::ActivityReservationBook reservations;
    pbguide::ActivityReservationRequest request{
        "guild_objective:supplies-001",
        "director:guild",
        pbguide::ActivityKind::GuildObjective,
        {7, 9},
        2'000,
    };
    Check(reservations.Acquire(request, 1'900).acquired, "valid guild activity should acquire its members");
    Check(reservations.Release("guild_objective:supplies-001", "director:other") == 0 &&
            !reservations.ReleaseCharacter(7, "guild_objective:supplies-001", "director:other"),
        "a different owner must not release another system's characters");
    Check(reservations.ReleaseCharacter(7, "guild_objective:supplies-001", "director:guild"),
        "the exact owner should be able to release one character cleanly");
    Check(reservations.Expire(2'000) == 1 && reservations.Entries().empty(),
        "reservations must expire exactly at their persisted deadline");

    request.activityId = "world_pvp:battle-001";
    request.ownerId = "director:pvp";
    request.kind = pbguide::ActivityKind::WorldPvp;
    request.characterIds = {9};
    request.expiresAtSeconds = 2'100;
    Check(reservations.Acquire(request, 2'000).acquired,
        "an expired character should be immediately available for a different activity");

    bool duplicateRejected = false;
    try
    {
        pbguide::ActivityReservation duplicate{
            9,
            "quest_group:first",
            "director:ambient",
            pbguide::ActivityKind::OutdoorQuestGroup,
            2'200,
        };
        pbguide::ActivityReservationBook invalid({duplicate, duplicate});
        static_cast<void>(invalid);
    }
    catch (std::invalid_argument const&)
    {
        duplicateRejected = true;
    }
    Check(duplicateRejected, "duplicate persisted character reservations must fail closed during restart loading");
}

void TestRoutePrerequisitesAndExplicitSkipSemantics()
{
    pbguide::GuideStep first{"quest:first", Candidate("quest:first"), {}, false, false, {}};
    pbguide::GuideStep optional{"quest:optional", Candidate("quest:optional"), {"quest:first"}, true, true, {}};
    pbguide::GuideStep final{"quest:final", Candidate("quest:final"), {"quest:optional"}, false, false, {}};
    pbguide::GuideRoute route(RouteIdentity(), {final, optional, first});

    auto progress = Progress();
    auto start = route.Resolve(progress);
    Check(start.ready.size() == 1 && start.ready.front().stableId == "quest:first",
        "only the prerequisite root should be ready initially");

    auto progressed = Progress();
    progressed.completed.insert("quest:first");
    auto middle = route.Resolve(progressed);
    Check(middle.ready.size() == 1 && middle.ready.front().stableId == "quest:optional",
        "completing a prerequisite should unlock its dependent");

    progressed.skipped.insert("quest:optional");
    auto skipped = route.Resolve(progressed);
    Check(skipped.ready.size() == 1 && skipped.ready.front().stableId == "quest:final",
        "only an explicitly skippable prerequisite should satisfy dependents when skipped");
}

void TestRouteRejectsMissingPrerequisitesAndCycles()
{
    bool missingRejected = false;
    try
    {
        pbguide::GuideStep bad{"quest:bad", Candidate("quest:bad"), {"quest:missing"}, false, false, {}};
        pbguide::GuideRoute route(RouteIdentity(), {bad});
        static_cast<void>(route);
    }
    catch (std::invalid_argument const&)
    {
        missingRejected = true;
    }
    Check(missingRejected, "route construction should reject missing prerequisite IDs");

    bool cycleRejected = false;
    try
    {
        pbguide::GuideStep a{"quest:a", Candidate("quest:a"), {"quest:b"}, false, false, {}};
        pbguide::GuideStep b{"quest:b", Candidate("quest:b"), {"quest:a"}, false, false, {}};
        pbguide::GuideRoute route(RouteIdentity(), {a, b});
        static_cast<void>(route);
    }
    catch (std::invalid_argument const&)
    {
        cycleRejected = true;
    }
    Check(cycleRejected, "route construction should reject prerequisite cycles");

    bool emptyRejected = false;
    try
    {
        pbguide::GuideRoute route(RouteIdentity(), {});
        static_cast<void>(route);
    }
    catch (std::invalid_argument const&)
    {
        emptyRejected = true;
    }
    Check(emptyRejected, "empty route catalogues should fail schema-equivalent validation");

    bool malformedIdentityRejected = false;
    try
    {
        auto identity = RouteIdentity();
        identity.catalogueDigest[0] = 'A';
        pbguide::GuideStep step{"quest:valid", Candidate("quest:valid"), {}, false, false, {}};
        pbguide::GuideRoute route(identity, {step});
        static_cast<void>(route);
    }
    catch (std::invalid_argument const&)
    {
        malformedIdentityRejected = true;
    }
    Check(malformedIdentityRejected, "route construction must reject noncanonical catalogue digests");
}

void TestRouteProgressRevisionMismatchFailsClosed()
{
    pbguide::GuideStep first{"quest:first", Candidate("quest:first"), {}, false, false, {}};
    pbguide::GuideRoute route(RouteIdentity(), {first});
    auto progress = Progress();
    progress.completed.insert("quest:from-old-catalogue");

    auto const unknown = route.Resolve(progress);
    Check(unknown.ready.empty(), "unknown persisted progress must not silently select a new route objective");
    Check(unknown.progressErrors.size() == 1 &&
            unknown.progressErrors.front() == "unknown_completed_step:quest:from-old-catalogue",
        "catalogue mismatch should have a deterministic synchronization error");

    progress.completed.clear();
    progress.completed.insert("quest:first");
    progress.skipped.insert("quest:first");
    auto const conflicting = route.Resolve(progress);
    Check(conflicting.ready.empty() && !conflicting.progressErrors.empty(),
        "a step cannot be both completed and skipped");

    auto mismatched = Progress();
    mismatched.identity.revision = "northshire-v2";
    auto const stale = route.Resolve(mismatched);
    Check(stale.ready.empty() && stale.progressErrors == std::vector<std::string>{"route_identity_mismatch"},
        "persisted progress from another route revision must fail closed before objective selection");

    auto invalidSkip = Progress();
    invalidSkip.skipped.insert("quest:first");
    auto const skipResult = route.Resolve(invalidSkip);
    Check(skipResult.ready.empty() &&
            skipResult.progressErrors == std::vector<std::string>{"non_skippable_step_skipped:quest:first"},
        "a route must explicitly authorize a step to be skipped");
}

void TestAlternativePrerequisitesExclusiveBranchesAndClusters()
{
    auto root = QuestStep("quest:root", 1);
    root.clusterId = "cluster:abbey";
    auto branchA = QuestStep("quest:branch-a", 2, {"quest:root"});
    branchA.exclusiveGroup = "exclusive:abbey-choice";
    branchA.clusterId = "cluster:abbey";
    auto branchB = QuestStep("quest:branch-b", 3, {"quest:root"});
    branchB.exclusiveGroup = "exclusive:abbey-choice";
    branchB.clusterId = "cluster:abbey";
    auto final = QuestStep("quest:after-choice", 4);
    final.prerequisiteAnyOf = {{"quest:branch-b", "quest:branch-a"}};
    final.clusterId = "cluster:village";
    pbguide::GuideRoute route(RouteIdentity(), {final, branchB, root, branchA});

    auto progress = Progress();
    progress.completed.insert("quest:root");
    auto const choices = route.Resolve(progress);
    Check(choices.ready.size() == 2 && choices.ready[0].stableId == "quest:branch-a" &&
            choices.ready[1].stableId == "quest:branch-b",
        "an unresolved exclusive branch should expose both deterministic alternatives");
    Check(choices.readyClusterByObjective.at("quest:branch-a") == "cluster:abbey",
        "ready objectives should retain their travel/objective cluster identity");

    progress.completed.insert("quest:branch-a");
    auto const selected = route.Resolve(progress);
    Check(selected.ready.size() == 1 && selected.ready.front().stableId == "quest:after-choice",
        "completing either any-of prerequisite should unlock its dependent");
    Check(selected.blockedReasons.at("quest:branch-b") ==
            std::vector<std::string>{"exclusive_branch_selected:quest:branch-a"},
        "the unselected exclusive alternative must be deterministically blocked");

    progress.completed.insert("quest:branch-b");
    auto const impossible = route.Resolve(progress);
    Check(!impossible.progressErrors.empty() && impossible.progressErrors.front() ==
            "multiple_exclusive_branches_completed:exclusive:abbey-choice",
        "authoritative history that violates an exclusive branch must not be silently normalized");
}

void TestAlternativePrerequisiteCycleValidationUsesAcyclicMember()
{
    auto a = QuestStep("quest:a", 301);
    a.prerequisiteAnyOf = {{"quest:b", "quest:c"}};
    auto b = QuestStep("quest:b", 302, {"quest:a"});
    auto c = QuestStep("quest:c", 303);
    pbguide::GuideRoute route(RouteIdentity(), {b, a, c});

    auto progress = Progress();
    auto resolution = route.Resolve(progress);
    Check(resolution.ready.size() == 1 && resolution.ready.front().stableId == "quest:c",
        "a free any-of alternative should keep the route satisfiable despite a cycle through another alternative");

    progress.completed.insert("quest:c");
    resolution = route.Resolve(progress);
    Check(resolution.ready.size() == 1 && resolution.ready.front().stableId == "quest:a",
        "completing the acyclic alternative should unlock the dependent step");

    progress.completed.insert("quest:a");
    resolution = route.Resolve(progress);
    Check(resolution.ready.size() == 1 && resolution.ready.front().stableId == "quest:b",
        "the dependent step should become ready after the any-of route is resolved");
}

void TestAuthoritativeQuestHistorySynchronizationAndRepair()
{
    auto first = QuestStep("quest:first", 101);
    auto second = QuestStep("quest:second", 102, {"quest:first"});
    auto third = QuestStep("quest:third", 103, {"quest:second"});
    pbguide::GuideRoute route(RouteIdentity(), {third, first, second});

    auto stale = Progress();
    stale.identity.revision = "old-route";
    stale.completed.insert("quest:unknown-old-step");
    pbguide::CharacterQuestHistory history;
    history.rewardedQuestIds.insert(103);
    auto const rebuilt = route.Synchronize(&stale, history);
    Check(rebuilt.rebuiltFromAuthoritativeHistory,
        "a stale route revision should rebuild from authoritative quest history, not carry unknown progress");
    Check(rebuilt.progress.completed == std::set<std::string>{"quest:third"},
        "rewarded quest history should reconstruct completed route steps");
    Check(rebuilt.repairRequiredStepIds == std::vector<std::string>({"quest:first", "quest:second"}),
        "synchronization should expose the complete missing hard-prerequisite closure");
    Check(rebuilt.repairReadyStepIds == std::vector<std::string>{"quest:first"},
        "repair should begin at the earliest currently satisfiable prerequisite frontier");

    history.rewardedQuestIds.insert(101);
    auto const advanced = route.Synchronize(nullptr, history);
    Check(advanced.repairRequiredStepIds == std::vector<std::string>{"quest:second"} &&
            advanced.repairReadyStepIds == std::vector<std::string>{"quest:second"},
        "authoritative completions should advance prerequisite repair without relying on stale cursors");

    auto left = QuestStep("quest:left", 201);
    auto right = QuestStep("quest:right", 202);
    auto dependent = QuestStep("quest:dependent", 203);
    dependent.prerequisiteAnyOf = {{"quest:right", "quest:left"}};
    pbguide::GuideRoute alternativeRoute(RouteIdentity(), {dependent, right, left});
    pbguide::CharacterQuestHistory alternativeHistory;
    alternativeHistory.activeQuestIds.insert(203);
    auto const alternativeRepair = alternativeRoute.Synchronize(nullptr, alternativeHistory);
    Check(alternativeRepair.repairAlternativeGroups ==
            std::vector<std::vector<std::string>>{{"quest:left", "quest:right"}},
        "missing any-of prerequisites should be returned as alternatives for deterministic scoring, not guessed");
}

void TestActiveQuestKeepsValidSkipAndSeedsPrerequisiteRepair()
{
    auto prerequisite = QuestStep("quest:repair-first", 501);
    auto active = QuestStep("quest:active-skipped", 502, {"quest:repair-first"});
    active.maySkip = true;
    pbguide::GuideRoute route(RouteIdentity(), {active, prerequisite});

    auto persisted = Progress();
    persisted.skipped.insert("quest:active-skipped");
    pbguide::CharacterQuestHistory history;
    history.activeQuestIds.insert(502);

    auto const synchronized = route.Synchronize(&persisted, history);
    Check(synchronized.errors.empty(), "valid persisted skip and active native history should synchronize cleanly");
    Check(synchronized.progress.skipped == std::set<std::string>{"quest:active-skipped"},
        "active native quest history should retain a valid persisted explicit skip");
    Check(synchronized.progress.completed.empty(),
        "active native quest history must not manufacture completed progress");
    Check(synchronized.repairRequiredStepIds == std::vector<std::string>{"quest:repair-first"} &&
            synchronized.repairReadyStepIds == std::vector<std::string>{"quest:repair-first"},
        "an active skipped objective must still seed repair of its missing prerequisite");

    auto const resolution = route.Resolve(synchronized.progress);
    Check(resolution.ready.size() == 1 && resolution.ready.front().stableId == "quest:repair-first" &&
            std::none_of(resolution.ready.begin(), resolution.ready.end(), [](auto const& objective) {
                return objective.stableId == "quest:active-skipped";
            }),
        "the retained skipped objective should stay hidden while its prerequisite repair remains available");
}

void TestSkippedExclusiveBranchSatisfactionWithCompletedSibling()
{
    for (bool skippedSatisfiesDependents : {true, false})
    {
        auto branchA = QuestStep("quest:branch-a", 601);
        branchA.maySkip = true;
        branchA.skippedSatisfiesDependents = skippedSatisfiesDependents;
        branchA.exclusiveGroup = "exclusive:route-choice";
        auto branchB = QuestStep("quest:branch-b", 602);
        branchB.exclusiveGroup = "exclusive:route-choice";
        auto dependent = QuestStep("quest:dependent-on-skipped-a", 603, {"quest:branch-a"});
        pbguide::GuideRoute route(RouteIdentity(), {dependent, branchB, branchA});

        auto progress = Progress();
        progress.completed.insert("quest:branch-b");
        progress.skipped.insert("quest:branch-a");
        auto const withSkip = route.Resolve(progress);
        Check(withSkip.progressErrors.empty(),
            "a valid skipped exclusive branch and one completed sibling should not be contradictory progress");
        Check(std::none_of(withSkip.ready.begin(), withSkip.ready.end(), [](auto const& objective) {
                return objective.stableId == "quest:branch-a";
            }), "a skipped exclusive branch must not be pursued");
        if (skippedSatisfiesDependents)
            Check(std::any_of(withSkip.ready.begin(), withSkip.ready.end(), [](auto const& objective) {
                    return objective.stableId == "quest:dependent-on-skipped-a";
                }), "an explicitly satisfying skip should unlock its dependent despite a completed sibling");
        else
            Check(withSkip.blockedReasons.at("quest:dependent-on-skipped-a") ==
                    std::vector<std::string>{"prerequisite_incomplete:quest:branch-a"},
                "a non-satisfying skip must leave its dependent blocked despite a completed sibling");

        progress.skipped.clear();
        auto const pursuingSkippedBranch = route.Resolve(progress);
        Check(pursuingSkippedBranch.blockedReasons.at("quest:branch-a") ==
                std::vector<std::string>{"exclusive_branch_selected:quest:branch-b"},
            "a completed exclusive sibling must block pursuing the previously skipped branch");
    }
}

bool HasGearReason(pbguide::GearSourceEvaluation const& evaluation, std::string const& reason);

void TestEarnedGearSourceAndPhaseRestrictions()
{
    auto snapshot = BaseSnapshot();
    pbguide::CharacterResourceSnapshot resources;
    auto synthetic = ItemSource("source:synthetic", 100);
    synthetic.earnedSource = false;
    auto future = ItemSource("source:future", 100);
    future.minimumPhase = 2;
    auto index = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, {future, synthetic});
    pbguide::EarnedGearPlanner productionPlanner;
    auto const unverified = productionPlanner.Plan(snapshot, resources, index, {UpgradeTarget(100)});
    Check(unverified.evaluations.empty() &&
            unverified.errors == std::vector<std::string>{"unverified_source_index"},
        "the production planner must reject caller-created sources even when they claim a matching graph digest");
    auto planner = TestGearPlanner();
    auto const plan = planner.Plan(snapshot, resources, index, {UpgradeTarget(100)});

    Check(!plan.selectedSourceId.has_value(), "unearned and future-phase gear sources must never be pursued");
    bool sawUnearned = false;
    bool sawPhase = false;
    for (auto const& evaluation : plan.evaluations)
    {
        sawUnearned = sawUnearned ||
            (HasGearReason(evaluation, "unearned_or_synthetic_source") &&
                evaluation.disposition == pbguide::GearDisposition::Abandoned);
        sawPhase = sawPhase || std::find(evaluation.reasonCodes.begin(), evaluation.reasonCodes.end(),
                "phase_not_authorized") != evaluation.reasonCodes.end();
    }
    Check(sawUnearned, "synthetic gear sources must be permanently abandoned with an explanation");
    Check(sawPhase, "future-phase gear sources must be deferred before execution");
}

bool HasGearReason(pbguide::GearSourceEvaluation const& evaluation, std::string const& reason)
{
    return std::find(evaluation.reasonCodes.begin(), evaluation.reasonCodes.end(), reason) !=
        evaluation.reasonCodes.end();
}

void TestDropExpectedValueAndDeterministicSelection()
{
    auto snapshot = BaseSnapshot();
    pbguide::CharacterResourceSnapshot resources;
    auto halfChance = ItemSource("source:half-drop", 101, pbguide::ItemSourceKind::CreatureDrop);
    halfChance.chanceNumerator = 1;
    halfChance.chanceDenominator = 2;
    halfChance.secondsPerAttempt = 60;
    auto guaranteedSlow = ItemSource("source:guaranteed-slow", 101, pbguide::ItemSourceKind::QuestReward);
    guaranteedSlow.secondsPerAttempt = 200;
    auto index = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, {halfChance, guaranteedSlow});
    auto planner = TestGearPlanner();
    auto const plan = planner.Plan(snapshot, resources, index, {UpgradeTarget(101)});

    Check(plan.selectedSourceId == std::optional<std::string>("source:half-drop"),
        "pursue-until-acquired scoring should compare full terminal utility against rational expected cost");
    auto const half = std::find_if(plan.evaluations.begin(), plan.evaluations.end(), [](auto const& evaluation) {
        return evaluation.stableId == "source:half-drop";
    });
    Check(half != plan.evaluations.end() && half->score.expectedAttemptsNumerator == 2 &&
            half->score.expectedAttemptsDenominator == 1 && half->score.expectedAttemptsCeiling == 2 &&
            half->score.expectedAcquisitionSeconds == 120 &&
            half->score.upgradeUtility == 1000,
        "drop evaluation should expose exact rational attempts, expected time, and terminal utility");
    auto const guaranteed = std::find_if(plan.evaluations.begin(), plan.evaluations.end(), [](auto const& evaluation) {
        return evaluation.stableId == "source:guaranteed-slow";
    });
    Check(guaranteed != plan.evaluations.end() && HasGearReason(*guaranteed, "lower_ranked_than:source:half-drop"),
        "a deferred eligible source should explain which higher-valued source outranked it");

    auto twoThirds = ItemSource("source:two-thirds", 111, pbguide::ItemSourceKind::CreatureDrop);
    twoThirds.chanceNumerator = 2;
    twoThirds.chanceDenominator = 3;
    twoThirds.secondsPerAttempt = 5;
    auto rationalIndex = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, {twoThirds});
    auto const rational = planner.Plan(snapshot, resources, rationalIndex, {UpgradeTarget(111)});
    Check(rational.evaluations.size() == 1 &&
            rational.evaluations.front().score.expectedAttemptsNumerator == 3 &&
            rational.evaluations.front().score.expectedAttemptsDenominator == 2 &&
            rational.evaluations.front().score.expectedAttemptsCeiling == 2 &&
            rational.evaluations.front().score.expectedAcquisitionSeconds == 8,
        "non-integral expected attempts must retain the exact rational and ceil only the final second cost");
}

void TestGearNoActionBaseline()
{
    auto snapshot = BaseSnapshot();
    pbguide::CharacterResourceSnapshot resources;
    auto harmful = ItemSource("source:harmful-farm", 112, pbguide::ItemSourceKind::CreatureDrop);
    harmful.secondsPerAttempt = 100000;
    auto index = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, {harmful});
    auto planner = TestGearPlanner();
    auto const plan = planner.Plan(snapshot, resources, index, {UpgradeTarget(112, 1, snapshot.level + 2)});
    Check(!plan.selectedSourceId.has_value() && plan.evaluations.size() == 1 &&
            plan.evaluations.front().eligible &&
            plan.evaluations.front().disposition == pbguide::GearDisposition::Deferred &&
            HasGearReason(plan.evaluations.front(), "below_pursuit_threshold"),
        "a legal but negative-value acquisition must lose to continuing ordinary progression");

    pbguide::GearScoringPolicy thresholdPolicy;
    thresholdPolicy.version = "test-threshold-v1";
    thresholdPolicy.upgradeWeight = 1;
    thresholdPolicy.usefulLevelWeight = 0;
    thresholdPolicy.professionWeight = 0;
    thresholdPolicy.timeSecondCost = 1;
    pbguide::EarnedGearPlanner thresholdPlanner(thresholdPolicy, true);
    auto thresholdSource = ItemSource("source:threshold", 115);
    auto target = UpgradeTarget(115, 1, snapshot.level + 2);

    thresholdSource.secondsPerAttempt = 2;
    auto negativeIndex = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, {thresholdSource});
    auto const negative = thresholdPlanner.Plan(snapshot, resources, negativeIndex, {target});
    Check(!negative.selectedSourceId.has_value() && negative.evaluations.front().score.total == -1,
        "a negative net source must lose to the no-diversion baseline");

    thresholdSource.secondsPerAttempt = 1;
    auto zeroIndex = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, {thresholdSource});
    auto const zero = thresholdPlanner.Plan(snapshot, resources, zeroIndex, {target});
    Check(!zero.selectedSourceId.has_value() && zero.evaluations.front().score.total == 0,
        "a zero net source must not divert the bot from ordinary progression");

    thresholdSource.secondsPerAttempt = 0;
    auto exactIndex = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, {thresholdSource});
    auto const exact = thresholdPlanner.Plan(snapshot, resources, exactIndex, {target});
    Check(exact.selectedSourceId == std::optional<std::string>("source:threshold") &&
            exact.evaluations.front().score.total == 1,
        "a source exactly at the immutable pursuit threshold must be selectable");

    target.playerbotsUpgradeScore = 2;
    auto const positive = thresholdPlanner.Plan(snapshot, resources, exactIndex, {target});
    Check(positive.selectedSourceId == std::optional<std::string>("source:threshold") &&
            positive.evaluations.front().score.total == 2,
        "a source above the pursuit threshold must be selectable");
}

void TestGearSnapshotControlGates()
{
    auto snapshot = BaseSnapshot();
    pbguide::CharacterResourceSnapshot resources;
    auto source = ItemSource("source:control-gate", 113);
    auto index = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, {source});
    auto planner = TestGearPlanner();

    snapshot.hasHumanMaster = true;
    auto const controlled = planner.Plan(snapshot, resources, index, {UpgradeTarget(113)});
    Check(controlled.evaluations.empty() &&
            controlled.errors == std::vector<std::string>{"guide_control_suspended"},
        "gear planning must defer to an existing human or unsafe Playerbots control state");

    snapshot = BaseSnapshot();
    snapshot.scheduleActive = false;
    auto const inactive = planner.Plan(snapshot, resources, index, {UpgradeTarget(113)});
    Check(inactive.evaluations.empty() && inactive.errors == std::vector<std::string>{"schedule_inactive"},
        "gear planning must not create work outside the persistent cast schedule");

    snapshot = BaseSnapshot();
    snapshot.characterId = 0;
    auto const invalid = planner.Plan(snapshot, resources, index, {UpgradeTarget(113)});
    Check(invalid.evaluations.empty() && invalid.errors == std::vector<std::string>{"invalid_snapshot"},
        "gear planning must reject a malformed execution context before evaluating sources");
}

void TestQuestAlternativesCraftingDependenciesAndReplacementHorizon()
{
    auto snapshot = BaseSnapshot();
    pbguide::CharacterResourceSnapshot resources;
    resources.professionSkillValues[164] = 75;
    resources.consumedQuestAlternativeGroups.insert("reward_group:already-chosen");

    auto consumed = ItemSource("source:consumed-choice", 102);
    consumed.questAlternativeGroup = "reward_group:already-chosen";
    auto craft = ItemSource("source:craft", 103, pbguide::ItemSourceKind::Craft);
    craft.requiredProfessionSkillId = 164;
    craft.requiredProfessionSkillValue = 100;
    craft.craftDependencies = {{200, 2, false, true, true}};
    auto shortLived = ItemSource("source:short-lived", 104);
    auto dungeon = ItemSource("source:dungeon", 105, pbguide::ItemSourceKind::DungeonDrop);
    dungeon.requiredGroupSize = 5;
    dungeon.partyReady = false;

    auto index = pbguide::ItemSourceIndex::UnverifiedForTesting(snapshot.contentRevision, snapshot.contentGraphDigest,
        {dungeon, shortLived, craft, consumed});
    auto planner = TestGearPlanner();
    auto const plan = planner.Plan(snapshot, resources, index,
        {UpgradeTarget(102), UpgradeTarget(103), UpgradeTarget(104, 50, snapshot.level + 1), UpgradeTarget(105)});

    auto find = [&](std::string const& stableId) {
        return std::find_if(plan.evaluations.begin(), plan.evaluations.end(), [&](auto const& evaluation) {
            return evaluation.stableId == stableId;
        });
    };
    auto const consumedEvaluation = find("source:consumed-choice");
    auto const craftEvaluation = find("source:craft");
    auto const horizonEvaluation = find("source:short-lived");
    auto const dungeonEvaluation = find("source:dungeon");
    Check(consumedEvaluation != plan.evaluations.end() &&
            HasGearReason(*consumedEvaluation, "quest_alternative_consumed") &&
            consumedEvaluation->disposition == pbguide::GearDisposition::Abandoned,
        "an already-consumed quest reward alternative must not be planned as obtainable");
    Check(craftEvaluation != plan.evaluations.end() &&
            HasGearReason(*craftEvaluation, "profession_skill_too_low") &&
            HasGearReason(*craftEvaluation, "craft_dependency_unavailable"),
        "crafting should explain both profession and earned dependency blockers");
    Check(horizonEvaluation != plan.evaluations.end() &&
            HasGearReason(*horizonEvaluation, "replacement_horizon_too_short") &&
            horizonEvaluation->disposition == pbguide::GearDisposition::Abandoned,
        "an upgrade expected to be replaced too soon should be abandoned before acquisition work");
    Check(dungeonEvaluation != plan.evaluations.end() && HasGearReason(*dungeonEvaluation, "party_not_ready"),
        "dungeon gear should remain deferred until a role-ready party exists");
}

void TestGearProfessionValueMoneyAndContentIdentity()
{
    auto snapshot = BaseSnapshot();
    pbguide::CharacterResourceSnapshot resources;
    resources.moneyCopper = 500;
    auto affordable = ItemSource("source:affordable", 106, pbguide::ItemSourceKind::Vendor);
    affordable.moneyCostCopper = 400;
    auto expensive = ItemSource("source:expensive", 106, pbguide::ItemSourceKind::Vendor);
    expensive.moneyCostCopper = 600;
    auto index = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, {expensive, affordable});
    auto target = UpgradeTarget(106);
    target.professionValue = 3;
    auto planner = TestGearPlanner();
    auto const plan = planner.Plan(snapshot, resources, index, {target});
    Check(plan.selectedSourceId == std::optional<std::string>("source:affordable"),
        "the planner should pursue only an affordable ordinary vendor transaction");
    Check(plan.evaluations.front().score.profession == 300 && plan.evaluations.front().score.money == -4,
        "profession value and copper cost should have explicit fixed-integer score components");

    auto staleIndex = pbguide::ItemSourceIndex::UnverifiedForTesting(snapshot.contentRevision,
        "2222222222222222222222222222222222222222222222222222222222222222", {affordable});
    auto const stale = planner.Plan(snapshot, resources, staleIndex, {target});
    Check(stale.evaluations.empty() && stale.errors == std::vector<std::string>{"content_graph_identity_mismatch"},
        "gear planning must fail before evaluation when the effective content graph digest differs");
}

void TestGearPlanPersistenceShapeAndSemanticValidation()
{
    auto snapshot = BaseSnapshot();
    pbguide::CharacterResourceSnapshot resources;
    auto source = ItemSource("source:persisted", 114);
    auto index = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, {source});
    auto planner = TestGearPlanner();
    auto plan = planner.Plan(snapshot, resources, index, {UpgradeTarget(114)});

    Check(pbguide::ValidateGearPlanForPersistence(plan).empty(),
        "a selected gear plan must satisfy cross-field persistence invariants");
    auto const serialized = boost::json::parse(pbguide::GearPlanToJson(plan)).as_object();
    Check(serialized.at("schema_version").as_string() == "cpi-207-gear-plan-v1" &&
            serialized.at("plan_id").as_string() == "gear_plan:42:10000" &&
            serialized.at("selected_target_id").as_string() == "target:item-114-slot-15" &&
            serialized.at("evaluations").as_array().size() == 1 &&
            serialized.at("targets").as_array().size() == 1,
        "gear plan JSON must retain exact plan, target, source, and selection identity");

    plan.evaluations.front().score.total += 1;
    Check(pbguide::ValidateGearPlanForPersistence(plan) ==
            std::vector<std::string>{"score_total_mismatch:source:persisted"},
        "semantic persistence validation must reject a score total that does not equal its components");
    bool rejected = false;
    try
    {
        static_cast<void>(pbguide::GearPlanToJson(plan));
    }
    catch (std::invalid_argument const&)
    {
        rejected = true;
    }
    Check(rejected, "the serializer must fail closed rather than persist a semantically inconsistent plan");
}

void TestItemSourceReverseIndexKindsTieBreakAndOverflow()
{
    auto snapshot = BaseSnapshot();
    pbguide::CharacterResourceSnapshot resources;
    std::vector<pbguide::ItemSourceOption> allKinds = {
        ItemSource("source:quest", 201, pbguide::ItemSourceKind::QuestReward),
        ItemSource("source:creature", 202, pbguide::ItemSourceKind::CreatureDrop),
        ItemSource("source:gameobject", 203, pbguide::ItemSourceKind::GameObjectDrop),
        ItemSource("source:vendor", 204, pbguide::ItemSourceKind::Vendor),
        ItemSource("source:trainer-recipe", 214, pbguide::ItemSourceKind::TrainerRecipe),
        ItemSource("source:craft-kind", 205, pbguide::ItemSourceKind::Craft),
        ItemSource("source:pvp", 206, pbguide::ItemSourceKind::PvpReward),
        ItemSource("source:dungeon-quest", 207, pbguide::ItemSourceKind::DungeonQuest),
        ItemSource("source:dungeon-drop-kind", 208, pbguide::ItemSourceKind::DungeonDrop),
        ItemSource("source:disenchant", 215, pbguide::ItemSourceKind::Disenchant),
        ItemSource("source:prospecting", 216, pbguide::ItemSourceKind::Prospecting),
        ItemSource("source:milling", 217, pbguide::ItemSourceKind::Milling),
        ItemSource("source:fishing", 218, pbguide::ItemSourceKind::Fishing),
        ItemSource("source:skinning", 219, pbguide::ItemSourceKind::Skinning),
    };
    auto craftKind = std::find_if(allKinds.begin(), allKinds.end(), [](auto const& source) {
        return source.kind == pbguide::ItemSourceKind::Craft;
    });
    craftKind->requiredProfessionSkillId = 164;
    craftKind->craftDependencies = {{300, 1, true, true, true}};
    auto tiedB = ItemSource("source:tie-b", 209);
    auto tiedA = ItemSource("source:tie-a", 209);
    allKinds.push_back(tiedB);
    allKinds.push_back(tiedA);
    auto index = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, allKinds);
    Check(index.SourcesForItem(209).size() == 2 &&
            index.SourcesForItem(209)[0]->stableId == "source:tie-a" &&
            index.SourcesForItem(209)[1]->stableId == "source:tie-b",
        "the reverse item-source index should cover every supported source kind in canonical item/source order");

    auto planner = TestGearPlanner();
    auto const tie = planner.Plan(snapshot, resources, index, {UpgradeTarget(209)});
    Check(tie.selectedSourceId == std::optional<std::string>("source:tie-a"),
        "equal gear opportunities should use lexical stable source ID as an input-order-independent tie break");

    auto overflowSource = ItemSource("source:overflow", 210, pbguide::ItemSourceKind::CreatureDrop);
    overflowSource.chanceNumerator = 1;
    overflowSource.chanceDenominator = 2;
    overflowSource.secondsPerAttempt = std::numeric_limits<pbguide::Score>::max();
    auto overflowIndex = pbguide::ItemSourceIndex::UnverifiedForTesting(
        snapshot.contentRevision, snapshot.contentGraphDigest, {overflowSource});
    auto const overflow = planner.Plan(snapshot, resources, overflowIndex, {UpgradeTarget(210)});
    Check(!overflow.selectedSourceId.has_value() && overflow.evaluations.size() == 1 &&
            HasGearReason(overflow.evaluations.front(), "score_overflow") &&
            overflow.evaluations.front().disposition == pbguide::GearDisposition::Abandoned,
        "expected-attempt time overflow must fail closed without signed undefined behavior");

    auto missingTarget = UpgradeTarget(999);
    auto validTarget = UpgradeTarget(209);
    auto const missing = planner.Plan(snapshot, resources, index, {missingTarget, validTarget});
    Check(missing.selectedTargetId == std::optional<std::string>(validTarget.stableId) &&
            missing.selectedSourceId == std::optional<std::string>("source:tie-a") &&
            missing.targets.size() == 2 &&
            std::any_of(missing.targets.begin(), missing.targets.end(), [&](auto const& target) {
                return target.stableId == missingTarget.stableId &&
                    target.status == pbguide::GearTargetStatus::Unavailable &&
                    target.reasonCodes == std::vector<std::string>{"no_sources_for_target"};
            }),
        "a target absent from the reverse index must be explicit without suppressing an independent valid target");

    auto secondSlot = UpgradeTarget(209);
    secondSlot.stableId = "target:item-209-slot-12";
    secondSlot.slot = 12;
    auto const multiSlot = planner.Plan(snapshot, resources, index, {validTarget, secondSlot});
    Check(multiSlot.errors.empty() && multiSlot.evaluations.size() == 4,
        "target identity must permit the same item to be evaluated for multiple equipment slots");

    bool reservedPolicyRejected = false;
    try
    {
        pbguide::GearScoringPolicy policy;
        policy.upgradeWeight += 1;
        pbguide::EarnedGearPlanner invalid(policy);
        static_cast<void>(invalid);
    }
    catch (std::invalid_argument const&)
    {
        reservedPolicyRejected = true;
    }
    Check(reservedPolicyRejected, "the gear policy version must bind one immutable set of scoring constants");

    auto stochasticVendor = ItemSource("source:invalid-stochastic-vendor", 220, pbguide::ItemSourceKind::Vendor);
    stochasticVendor.chanceNumerator = 1;
    stochasticVendor.chanceDenominator = 2;
    stochasticVendor.secondsPerAttempt = 10;
    auto zeroTimeDrop = ItemSource("source:invalid-zero-time-drop", 221, pbguide::ItemSourceKind::CreatureDrop);
    zeroTimeDrop.chanceNumerator = 1;
    zeroTimeDrop.chanceDenominator = 2;
    auto nonCraftDependency = ItemSource("source:invalid-dependency", 222);
    nonCraftDependency.craftDependencies = {{300, 1, true, true, true}};
    for (auto const& invalidSource : {stochasticVendor, zeroTimeDrop, nonCraftDependency})
    {
        bool sourceRejected = false;
        try
        {
            static_cast<void>(pbguide::ItemSourceIndex::UnverifiedForTesting(
                snapshot.contentRevision, snapshot.contentGraphDigest, {invalidSource}));
        }
        catch (std::invalid_argument const&)
        {
            sourceRejected = true;
        }
        Check(sourceRejected, "source-kind semantic contradictions must fail during index construction");
    }
}
}

int main()
{
    TestScoreAndExplanation();
    TestDeterministicTieBreakIsInputOrderIndependent();
    TestLegalityAndCapabilityGates();
    TestFailureBackoffAndPostBackoffPenalty();
    TestFailureEvidenceIsRevisionScoped();
    TestSelectUsesLatestMatchingFailureEvidence();
    TestHumanControlAndUnsafeStatesSuspendGuide();
    TestAdmissionScheduleAndSnapshotIdentityGates();
    TestFailureEvidenceScopeAndCorruptionHandling();
    TestFailureDecayAndFutureEvidence();
    TestFailureMemoryAggregationScopingAndLoadValidation();
    TestInvalidInputsAndDuplicateIdsFailClosed();
    TestOverflowFailsClosed();
    TestPartyRequirementAndGroupOpportunity();
    TestActivityReservationsAreAtomicAndDeterministic();
    TestActivityReservationExpiryAndOwnership();
    TestRoutePrerequisitesAndExplicitSkipSemantics();
    TestRouteRejectsMissingPrerequisitesAndCycles();
    TestRouteProgressRevisionMismatchFailsClosed();
    TestAlternativePrerequisitesExclusiveBranchesAndClusters();
    TestAlternativePrerequisiteCycleValidationUsesAcyclicMember();
    TestAuthoritativeQuestHistorySynchronizationAndRepair();
    TestActiveQuestKeepsValidSkipAndSeedsPrerequisiteRepair();
    TestSkippedExclusiveBranchSatisfactionWithCompletedSibling();
    TestEarnedGearSourceAndPhaseRestrictions();
    TestDropExpectedValueAndDeterministicSelection();
    TestGearNoActionBaseline();
    TestGearSnapshotControlGates();
    TestQuestAlternativesCraftingDependenciesAndReplacementHorizon();
    TestGearProfessionValueMoneyAndContentIdentity();
    TestGearPlanPersistenceShapeAndSemanticValidation();
    TestItemSourceReverseIndexKindsTieBreakAndOverflow();

    if (failures != 0)
    {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All strategic director tests passed\n";
    return EXIT_SUCCESS;
}
