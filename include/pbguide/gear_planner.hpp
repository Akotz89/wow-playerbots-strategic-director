#pragma once

#include "pbguide/director.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pbguide
{
enum class ItemSourceKind : std::uint8_t
{
    QuestReward,
    CreatureDrop,
    GameObjectDrop,
    Vendor,
    TrainerRecipe,
    Craft,
    PvpReward,
    DungeonQuest,
    DungeonDrop,
    Disenchant,
    Prospecting,
    Milling,
    Fishing,
    Skinning
};

struct CraftDependency
{
    std::uint32_t itemId = 0;
    std::uint32_t count = 0;
    bool available = true;
    bool phaseAuthorized = true;
    bool earnedSource = true;
};

enum class AcquisitionMetricState : std::uint8_t { Unknown, Measured, Calibrated };
enum class AcquisitionEpisodeOutcome : std::uint8_t { Success, Adverse, Contended, Censored };

struct AcquisitionMetricContext
{
    std::string sourceId, actorIdentity, profileId, routeId, routeRevision, phaseDigest;
    std::optional<std::string> partyId;
    std::uint64_t characterId = 0, sessionIncarnation = 0, strategicGeneration = 0, goalRevision = 0;
    std::uint32_t phase = 0;
    bool operator==(AcquisitionMetricContext const&) const = default;
};

struct AcquisitionMetricEpisode
{
    std::string episodeId, terminalEvidenceDigest;
    std::uint64_t startedMonotonicNs = 0, endedMonotonicNs = 0;
    AcquisitionEpisodeOutcome outcome = AcquisitionEpisodeOutcome::Censored;
    bool operator==(AcquisitionMetricEpisode const&) const = default;
};

struct AcquisitionMetricEvidence
{
    AcquisitionMetricState state = AcquisitionMetricState::Unknown;
    AcquisitionMetricContext context;
    std::string unknownReason, calibrationId, calibrationRevision, contextDigest;
    std::optional<std::uint16_t> probabilityPerThousand, uncertaintyLowPerThousand, uncertaintyHighPerThousand;
    std::uint32_t sampleCount = 0, eventCount = 0, censoredCount = 0;
    std::vector<AcquisitionMetricEpisode> episodes;
    bool operator==(AcquisitionMetricEvidence const&) const = default;
};

struct ItemSourceOption
{
    std::string stableId;
    std::uint32_t itemId = 0;
    ItemSourceKind kind = ItemSourceKind::QuestReward;
    std::uint32_t minimumPhase = 1;
    std::uint32_t maximumPhase = 99;
    std::uint32_t requiredGroupSize = 1;
    bool partyReady = true;
    bool available = true;
    bool phaseAuthorized = true;
    bool earnedSource = true;
    std::uint32_t chanceNumerator = 1;
    std::uint32_t chanceDenominator = 1;
    Score travelSeconds = 0;
    Score secondsPerAttempt = 0;
    std::uint64_t moneyCostCopper = 0;
    Score riskMilli = 0;
    Score contentionMilli = 0;
    std::vector<std::string> requiredCapabilities;
    std::uint32_t requiredProfessionSkillId = 0;
    std::uint32_t requiredProfessionSkillValue = 0;
    std::optional<std::string> questAlternativeGroup;
    std::vector<CraftDependency> craftDependencies;
    std::optional<AcquisitionMetricEvidence> riskEvidence, contentionEvidence;
};

class CanonicalItemSourceLoader;

// Typed cross-layer identity for the paired CPI-211 bundle/pathset path. Legacy
// plans leave this absent and retain their component-revision contract.
struct GenerationRequestBatch
{
    std::uint32_t batchOrdinal = 0;
    std::string operationId;
    std::string sourceSnapshotId;
    std::string requestKeysSha256;
    bool operator==(GenerationRequestBatch const&) const = default;
};

struct GenerationIdentity
{
    std::string schema = "core1-cpi211-generation-reference-v1";
    std::string generationId;
    std::string sourceIdentityDigest;
    std::string bundleIdentitySha256;
    std::optional<std::string> rawGenerationSnapshotId;
    std::optional<std::string> nativeSourceSnapshotId;
    std::vector<GenerationRequestBatch> requestBatches;
    std::string coverageCommitmentSha256;
    bool operator==(GenerationIdentity const&) const = default;
};

// A null optional is permitted only when the admitted v2 source_identity says
// that component revision is unknown. This provenance stays alongside, but is
// not conflated with, the compact generation identity serialized into plans.
struct GenerationComponentRevisions
{
    std::optional<std::string> core;
    std::optional<std::string> playerbots;
    std::optional<std::string> progression;
    std::optional<std::string> content;
    bool operator==(GenerationComponentRevisions const&) const = default;
};

class ItemSourceIndex
{
public:
    [[nodiscard]] static ItemSourceIndex UnverifiedForTesting(
        std::string contentRevision,
        std::string contentGraphDigest,
        std::vector<ItemSourceOption> sources);

    [[nodiscard]] bool IsVerifiedCanonicalProjection() const noexcept;
    [[nodiscard]] bool IsVersion2Projection() const noexcept;
    [[nodiscard]] std::optional<GenerationIdentity> const& Generation() const noexcept;
    [[nodiscard]] std::optional<GenerationComponentRevisions> const& GenerationRevisions() const noexcept;
    [[nodiscard]] std::string const& ContentRevision() const noexcept;
    [[nodiscard]] std::string const& ContentGraphDigest() const noexcept;
    [[nodiscard]] std::vector<ItemSourceOption> const& Sources() const noexcept;
    [[nodiscard]] std::vector<ItemSourceOption const*> SourcesForItem(std::uint32_t itemId) const;

private:
    friend class CanonicalItemSourceLoader;

    ItemSourceIndex(
        std::string contentRevision,
        std::string contentGraphDigest,
        std::vector<ItemSourceOption> sources,
        bool verifiedCanonicalProjection,
        bool version2Projection = false,
        std::optional<GenerationIdentity> generationIdentity = std::nullopt,
        std::optional<GenerationComponentRevisions> generationRevisions = std::nullopt);

    std::string contentRevision_;
    std::string contentGraphDigest_;
    std::vector<ItemSourceOption> sources_;
    std::map<std::uint32_t, std::vector<std::size_t>> sourceIndicesByItem_;
    bool verifiedCanonicalProjection_ = false;
    bool version2Projection_ = false;
    std::optional<GenerationIdentity> generationIdentity_;
    std::optional<GenerationComponentRevisions> generationRevisions_;
};

struct ItemUpgradeTarget
{
    std::string stableId;
    std::uint32_t itemId = 0;
    std::uint32_t slot = 0;
    Score playerbotsUpgradeScore = 0;
    std::uint32_t expectedReplacementLevel = 0;
    Score professionValue = 0;
};

struct CharacterResourceSnapshot
{
    std::uint64_t moneyCopper = 0;
    std::map<std::uint32_t, std::uint32_t> professionSkillValues;
    std::set<std::string> consumedQuestAlternativeGroups;
};

struct GearScoringPolicy
{
    std::string version = "cpi-211-gear-v1";
    Score upgradeWeight = 100;
    Score usefulLevelWeight = 200;
    Score professionWeight = 100;
    Score timeSecondCost = 1;
    std::uint64_t copperPerCostPoint = 100;
    Score riskMilliDivisor = 10;
    Score contentionMilliDivisor = 10;
    std::uint32_t minimumUsefulLevels = 2;
    Score minimumPursuitScore = 1;
};

struct GearScoreBreakdown
{
    Score upgradeUtility = 0;
    Score usefulLife = 0;
    Score profession = 0;
    Score acquisitionTime = 0;
    Score money = 0;
    Score risk = 0;
    Score contention = 0;
    Score total = 0;
    std::uint64_t expectedAttemptsNumerator = 0;
    std::uint64_t expectedAttemptsDenominator = 1;
    std::uint64_t expectedAttemptsCeiling = 0;
    Score expectedAcquisitionSeconds = 0;
    bool operator==(GearScoreBreakdown const&) const = default;
};

enum class GearDisposition : std::uint8_t
{
    Pursue,
    Deferred,
    Abandoned
};

struct GearSourceEvaluation
{
    std::string targetId;
    std::string stableId;
    std::uint32_t itemId = 0;
    bool eligible = false;
    GearDisposition disposition = GearDisposition::Deferred;
    std::vector<std::string> reasonCodes;
    GearScoreBreakdown score;
    std::optional<AcquisitionMetricEvidence> riskEvidence, contentionEvidence;
    bool operator==(GearSourceEvaluation const&) const = default;
};

enum class GearTargetStatus : std::uint8_t
{
    Candidate,
    Invalid,
    Unavailable
};

struct GearTargetEvaluation
{
    std::string stableId;
    std::uint32_t itemId = 0;
    std::uint32_t slot = 0;
    Score playerbotsUpgradeScore = 0;
    std::uint32_t expectedReplacementLevel = 0;
    Score professionValue = 0;
    GearTargetStatus status = GearTargetStatus::Candidate;
    std::vector<std::string> reasonCodes;
    bool operator==(GearTargetEvaluation const&) const = default;
};

struct GearPlan
{
    std::string schemaVersion = "cpi-207-gear-plan-v1";
    std::string planId;
    std::uint64_t characterId = 0;
    std::string contentRevision;
    std::string contentGraphDigest;
    std::optional<GenerationIdentity> generationIdentity;
    std::int64_t createdAtSeconds = 0;
    std::string policyVersion;
    std::string policyIdentity;
    std::optional<std::string> selectedTargetId;
    std::optional<std::string> selectedSourceId;
    std::optional<std::uint32_t> selectedItemId;
    std::vector<GearTargetEvaluation> targets;
    std::vector<GearSourceEvaluation> evaluations;
    std::vector<std::string> errors;
    bool operator==(GearPlan const&) const = default;
};

[[nodiscard]] std::string CanonicalGearPolicyIdentity(GearScoringPolicy const& policy);

class EarnedGearPlanner
{
public:
    explicit EarnedGearPlanner(
        GearScoringPolicy policy = {},
        bool allowUnverifiedSourceIndexForTesting = false);

    [[nodiscard]] GearPlan Plan(
        BotProgressSnapshot const& snapshot,
        CharacterResourceSnapshot const& resources,
        ItemSourceIndex const& sourceIndex,
        std::vector<ItemUpgradeTarget> const& targets) const;

private:
    friend class CanonicalGearPlanningService;
    [[nodiscard]] GearPlan MakePlanMetadata(BotProgressSnapshot const&) const;
    [[nodiscard]] GearPlan Evaluate(BotProgressSnapshot const&, CharacterResourceSnapshot const&,
        ItemSourceIndex const&, std::vector<ItemUpgradeTarget> const&) const;
    GearScoringPolicy policy_;
    bool allowUnverifiedSourceIndexForTesting_ = false;
};
}
