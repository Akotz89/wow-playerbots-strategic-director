#pragma once

#include "pbguide/gear_planner.hpp"
#include "json_contract.hpp"
#include <boost/json.hpp>
#include <stdexcept>

namespace pbguide::metric_json {
namespace j = boost::json;
inline std::string text(j::value const& v) { return std::string(v.as_string()); }
inline std::uint64_t number(j::value const& v) { return json_contract::Unsigned(v); }
inline std::uint32_t u32(j::value const& v) {
    auto n=number(v); if(n>UINT32_MAX) throw std::invalid_argument("metric:uint32"); return static_cast<std::uint32_t>(n);
}
inline std::uint16_t u16(j::value const& v) {
    auto n=number(v); if(n>1000) throw std::invalid_argument("metric:probability"); return static_cast<std::uint16_t>(n);
}
inline j::object encodeContext(AcquisitionMetricContext const& c) {
    return {{"source_id",c.sourceId},{"actor_identity",c.actorIdentity},{"character_id",c.characterId},
        {"session_incarnation",c.sessionIncarnation},{"strategic_generation",c.strategicGeneration},
        {"goal_revision",c.goalRevision},{"profile_id",c.profileId},
        {"party_id",c.partyId ? j::value(*c.partyId) : j::value(nullptr)}, {"phase",c.phase},
        {"route_id",c.routeId},{"route_revision",c.routeRevision},{"phase_digest",c.phaseDigest}};
}
inline AcquisitionMetricContext decodeContext(j::value const& v) {
    auto const& o=v.as_object(); AcquisitionMetricContext c;
    c.sourceId=text(o.at("source_id")); c.actorIdentity=text(o.at("actor_identity"));
    c.characterId=number(o.at("character_id")); c.sessionIncarnation=number(o.at("session_incarnation"));
    c.strategicGeneration=number(o.at("strategic_generation")); c.goalRevision=number(o.at("goal_revision"));
    c.profileId=text(o.at("profile_id")); if(!o.at("party_id").is_null()) c.partyId=text(o.at("party_id"));
    c.phase=u32(o.at("phase")); c.routeId=text(o.at("route_id")); c.routeRevision=text(o.at("route_revision"));
    c.phaseDigest=text(o.at("phase_digest"));
    if(c.sourceId.empty()||c.actorIdentity.empty()||!c.characterId||!c.sessionIncarnation||!c.strategicGeneration||
       c.profileId.empty()||c.routeId.empty()||c.routeRevision.empty()||c.phaseDigest.size()!=64||!c.phase)
        throw std::invalid_argument("metric:invalid_context");
    return c;
}
inline j::array encodeEpisodes(std::vector<AcquisitionMetricEpisode> const& episodes) {
    j::array a; for(auto const& e:episodes) {
        char const* outcome=e.outcome==AcquisitionEpisodeOutcome::Success?"success":
            e.outcome==AcquisitionEpisodeOutcome::Adverse?"adverse":
            e.outcome==AcquisitionEpisodeOutcome::Contended?"contended":"censored";
        a.emplace_back(j::object{{"episode_id",e.episodeId},{"started_monotonic_ns",e.startedMonotonicNs},
            {"ended_monotonic_ns",e.endedMonotonicNs},{"outcome",outcome},
            {"terminal_evidence_digest",e.terminalEvidenceDigest}});
    } return a;
}
inline std::vector<AcquisitionMetricEpisode> decodeEpisodes(j::value const& v) {
    std::vector<AcquisitionMetricEpisode> out;
    for(auto const& x:v.as_array()) {
        auto const& o=x.as_object(); AcquisitionMetricEpisode e;
        e.episodeId=text(o.at("episode_id")); e.startedMonotonicNs=number(o.at("started_monotonic_ns"));
        e.endedMonotonicNs=number(o.at("ended_monotonic_ns")); auto result=text(o.at("outcome"));
        e.outcome=result=="success"?AcquisitionEpisodeOutcome::Success:result=="adverse"?AcquisitionEpisodeOutcome::Adverse:
            result=="contended"?AcquisitionEpisodeOutcome::Contended:AcquisitionEpisodeOutcome::Censored;
        e.terminalEvidenceDigest=text(o.at("terminal_evidence_digest"));
        if(e.episodeId.empty()||e.endedMonotonicNs<e.startedMonotonicNs||e.terminalEvidenceDigest.size()!=64)
            throw std::invalid_argument("metric:invalid_episode");
        out.push_back(std::move(e));
    } return out;
}
inline j::object encode(AcquisitionMetricEvidence const& e) {
    if(e.state!=AcquisitionMetricState::Unknown&&e.state!=AcquisitionMetricState::Measured&&e.state!=AcquisitionMetricState::Calibrated)
        throw std::invalid_argument("metric:invalid_state");
    if(e.state==AcquisitionMetricState::Unknown&&e.unknownReason.empty())throw std::invalid_argument("metric:unknown_reason");
    if(e.state!=AcquisitionMetricState::Unknown&&
       (!e.sampleCount||e.sampleCount!=e.episodes.size()||e.eventCount+e.censoredCount>e.sampleCount))
        throw std::invalid_argument("metric:sample_counts");
    if(e.state==AcquisitionMetricState::Calibrated&&
       (!e.probabilityPerThousand||!e.uncertaintyLowPerThousand||!e.uncertaintyHighPerThousand))
        throw std::invalid_argument("metric:calibration_fields");
    j::object o{{"state",e.state==AcquisitionMetricState::Unknown?"unknown":e.state==AcquisitionMetricState::Measured?"measured":"calibrated"},
        {"context",encodeContext(e.context)}};
    if(e.state==AcquisitionMetricState::Unknown) o["unknown_reason"]=e.unknownReason;
    else {
        o["episodes"]=encodeEpisodes(e.episodes); o["sample_count"]=e.sampleCount;
        o["event_count"]=e.eventCount; o["censored_count"]=e.censoredCount;
        if(e.state==AcquisitionMetricState::Calibrated) {
            o["probability_per_thousand"]=*e.probabilityPerThousand;
            o["calibration_id"]=e.calibrationId; o["calibration_revision"]=e.calibrationRevision;
            o["context_digest"]=e.contextDigest; o["uncertainty_low_per_thousand"]=*e.uncertaintyLowPerThousand;
            o["uncertainty_high_per_thousand"]=*e.uncertaintyHighPerThousand;
        }
    } return o;
}
inline AcquisitionMetricEvidence decode(j::value const& v, std::string const& sourceId, bool contention) {
    auto const& o=v.as_object(); AcquisitionMetricEvidence e; auto state=text(o.at("state"));
    e.context=decodeContext(o.at("context"));
    if(e.context.sourceId!=sourceId) throw std::invalid_argument("metric:source_context_mismatch");
    if(state=="unknown") {e.state=AcquisitionMetricState::Unknown;e.unknownReason=text(o.at("unknown_reason"));if(e.unknownReason.empty())throw std::invalid_argument("metric:unknown_reason");return e;}
    if(state!="measured"&&state!="calibrated")throw std::invalid_argument("metric:invalid_state");
    e.state=state=="measured"?AcquisitionMetricState::Measured:AcquisitionMetricState::Calibrated;
    e.episodes=decodeEpisodes(o.at("episodes"));e.sampleCount=u32(o.at("sample_count"));
    e.eventCount=u32(o.at("event_count"));e.censoredCount=u32(o.at("censored_count"));
    if(e.sampleCount==0||e.sampleCount!=e.episodes.size()||e.eventCount+e.censoredCount>e.sampleCount)
        throw std::invalid_argument("metric:sample_counts");
    unsigned censored=0,events=0;for(auto const& episode:e.episodes){
        censored+=episode.outcome==AcquisitionEpisodeOutcome::Censored;
        events+=episode.outcome==(contention?AcquisitionEpisodeOutcome::Contended:AcquisitionEpisodeOutcome::Adverse);
    }
    if(censored!=e.censoredCount||events!=e.eventCount)throw std::invalid_argument("metric:outcome_counts");
    if(e.state==AcquisitionMetricState::Calibrated) {
        e.probabilityPerThousand=u16(o.at("probability_per_thousand"));e.calibrationId=text(o.at("calibration_id"));
        e.calibrationRevision=text(o.at("calibration_revision"));e.contextDigest=text(o.at("context_digest"));
        e.uncertaintyLowPerThousand=u16(o.at("uncertainty_low_per_thousand"));e.uncertaintyHighPerThousand=u16(o.at("uncertainty_high_per_thousand"));
        auto contextDigest=json_contract::Digest(encodeContext(e.context));
        if(e.calibrationId.empty()||e.calibrationRevision.empty()||e.contextDigest!=contextDigest||
           *e.uncertaintyLowPerThousand>*e.probabilityPerThousand||*e.probabilityPerThousand>*e.uncertaintyHighPerThousand)
            throw std::invalid_argument("metric:invalid_calibration");
    }
    return e;
}
}
