#pragma once
#include "pbguide/gear_planner.hpp"
#include <string_view>
namespace pbguide {
enum class ProjectionDomain { OfflineContract, EffectiveRealm };
struct OfflineSourcePins {
    std::string candidateId, generation, graphDigest, manifestDigest, projectionDigest;
    bool operator==(OfflineSourcePins const&) const = default;
};
class EffectiveDatasetBinding {
public:
    static EffectiveDatasetBinding FromRootAcceptedManifest(std::string_view bytes, std::string_view expectedSha256);
private:
    friend class AcceptedDatasetRegistry;
    friend class CanonicalItemSourceLoader;
    explicit EffectiveDatasetBinding(OfflineSourcePins pins) : pins_(std::move(pins)) {}
    OfflineSourcePins pins_;
};
class AcceptedDatasetRegistry {
public:
    // Deliberately empty until a source-pinned frozen extract receives acceptance.
    static std::optional<EffectiveDatasetBinding> Lookup(std::string_view candidateId);
};
class LoadedItemSources {
    friend class CanonicalItemSourceLoader;
    friend class CanonicalGearPlanningService;
    LoadedItemSources(ItemSourceIndex index, OfflineSourcePins pins, ProjectionDomain domain,
        std::map<std::string,std::string> blocked, std::string core, std::string playerbots,
        std::optional<GenerationIdentity> identity = std::nullopt)
        : coreRevision_(std::move(core)), playerbotsRevision_(std::move(playerbots)), index_(std::move(index)), pins_(std::move(pins)), domain_(domain), blocked_(std::move(blocked)), generationIdentity_(std::move(identity)) {}
    std::string coreRevision_, playerbotsRevision_;
    ItemSourceIndex index_;
    OfflineSourcePins pins_;
    ProjectionDomain domain_;
    std::map<std::string,std::string> blocked_;
    std::optional<GenerationIdentity> generationIdentity_;
public:
    std::string const& CoreRevision() const noexcept { return coreRevision_; }
    std::string const& PlayerbotsRevision() const noexcept { return playerbotsRevision_; }
    ItemSourceIndex const& Index() const noexcept { return index_; }
    OfflineSourcePins const& Pins() const noexcept { return pins_; }
    ProjectionDomain Domain() const noexcept { return domain_; }
    std::map<std::string,std::string> const& BlockedReasons() const noexcept { return blocked_; }
    std::optional<GenerationIdentity> const& Generation() const noexcept { return generationIdentity_; }
};
class CanonicalItemSourceLoader {
public:
    // projectionBytes is an array of item-source-v1 documents. Source ID explicitly equals edge ID.
    static LoadedItemSources LoadOffline(std::string_view graphBytes, std::string_view manifestBytes,
        std::string_view projectionBytes, OfflineSourcePins const& pins);
    static LoadedItemSources LoadEffectiveRealm(std::string_view graphBytes, std::string_view manifestBytes,
        std::string_view projectionBytes, EffectiveDatasetBinding const& binding);
private:
    static LoadedItemSources Load(std::string_view, std::string_view, std::string_view,
        OfflineSourcePins const&, ProjectionDomain);
};
struct CanonicalGearPlanResult {
    ProjectionDomain domain;
    OfflineSourcePins provenance;
    std::map<std::string,std::string> blockedReasons;
    GearPlan plan;
    bool operator==(CanonicalGearPlanResult const&) const = default;
};
class CanonicalGearPlanningService {
public:
    static CanonicalGearPlanResult PlanEffectiveRealm(LoadedItemSources const& loaded,
        BotProgressSnapshot const& snapshot, CharacterResourceSnapshot const& resources,
        std::vector<ItemUpgradeTarget> const& targets, GearScoringPolicy policy = {});
    // Concrete validated capsule is mandatory; raw indexes/flags cannot enter this path.
    static CanonicalGearPlanResult PlanOffline(LoadedItemSources const& loaded,
        BotProgressSnapshot const& snapshot, CharacterResourceSnapshot const& resources,
        std::vector<ItemUpgradeTarget> const& targets, GearScoringPolicy policy = {});
};
}
