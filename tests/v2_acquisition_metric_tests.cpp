#include "pbguide/canonical_item_sources.hpp"
#include "pbguide/gear_plan_json.hpp"
#include "pbguide/replay.hpp"
#include "../src/json_contract.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace j=boost::json;
using namespace pbguide;
void check(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
j::object context(std::string const& source){return {{"source_id",source},{"actor_identity","actor:test"},{"character_id",42},
 {"session_incarnation",1},{"strategic_generation",1},{"goal_revision",0},{"profile_id","profile:test"},
 {"party_id",nullptr},{"phase",1},{"route_id","route:test"},{"route_revision","r1"},{"phase_digest",std::string(64,'a')}};}
j::object unknown(std::string const& source){return {{"state","unknown"},{"context",context(source)},{"unknown_reason","insufficient_observations"}};}
j::object calibrated(std::string const& source,unsigned probability,unsigned low,unsigned high){
 auto c=context(source);auto digest=json_contract::Digest(c);
 j::array episodes{j::object{{"episode_id","episode:test"},{"started_monotonic_ns",1},{"ended_monotonic_ns",2},
  {"outcome","success"},{"terminal_evidence_digest",std::string(64,'b')}}};
 return {{"state","calibrated"},{"context",c},{"episodes",episodes},{"sample_count",1},{"event_count",0},{"censored_count",0},
  {"probability_per_thousand",probability},{"calibration_id","calibration:test"},{"calibration_revision","r1"},
  {"context_digest",digest},{"uncertainty_low_per_thousand",low},{"uncertainty_high_per_thousand",high}};
}
j::object measured(std::string const& source,char const* outcome){
 auto c=context(source);j::array episodes{j::object{{"episode_id","episode:measured"},{"started_monotonic_ns",3},{"ended_monotonic_ns",4},
  {"outcome",outcome},{"terminal_evidence_digest",std::string(64,'c')}}};
 bool event=std::string(outcome)=="adverse"||std::string(outcome)=="contended";
 return {{"state","measured"},{"context",c},{"episodes",episodes},{"sample_count",1},{"event_count",event?1:0},{"censored_count",0}};
}
int main(){try{
 std::ifstream f(std::string(CANONICAL_FIXTURE_DIR)+"/cases.json");check(bool(f),"fixture open");
 auto cases=j::parse(std::string((std::istreambuf_iterator<char>(f)),{}));j::value graph,manifest,projections;OfflineSourcePins pins;
 for(auto const& c:cases.as_array())if(c.at("name")==j::value("all-kinds")){graph=c.at("graph");manifest=c.at("manifest");projections=c.at("projections");auto const& p=c.at("pins");pins={std::string(p.at("candidateId").as_string()),std::string(p.at("generation").as_string()),std::string(p.at("graphDigest").as_string()),std::string(p.at("manifestDigest").as_string()),std::string(p.at("projectionDigest").as_string())};break;}
 auto& ps=projections.as_array();
 for(auto& projection:ps){projection.as_object()["schema_version"]="cpi-211-item-source-v2";for(auto& source:projection.at("sources").as_array()){
   auto id=std::string(source.at("source_id").as_string());source.as_object().erase("risk_milli");source.as_object().erase("contention_milli");
   source.as_object()["risk_evidence"]=unknown(id);source.as_object()["contention_evidence"]=unknown(id);
 }}
 pins.projectionDigest=json_contract::Digest(projections);
 auto loaded=CanonicalItemSourceLoader::LoadOffline(j::serialize(graph),j::serialize(manifest),j::serialize(projections),pins);
 check(loaded.Index().IsVersion2Projection(),"v2 source version was lost");
 BotProgressSnapshot snapshot;snapshot.characterId=42;snapshot.level=8;snapshot.phase=1;snapshot.nowSeconds=10000;snapshot.coreRevision="core-offline";snapshot.playerbotsRevision="pb-offline";snapshot.contentRevision="content-offline";snapshot.contentGraphDigest=pins.graphDigest;snapshot.routeId="route:test";snapshot.routeRevision="r1";snapshot.catalogueDigest=std::string(64,'0');snapshot.profileId="profile:test";snapshot.profileVersion="r1";snapshot.capabilityFingerprint="r1";snapshot.populationAdmitted=true;snapshot.scheduleActive=true;
 AcquisitionMetricPlanContext planContext;planContext.actorIdentity="actor:test";planContext.characterId=42;planContext.sessionIncarnation=1;planContext.strategicGeneration=1;planContext.goalRevision=0;planContext.profileId="profile:test";planContext.phase=1;planContext.routeId="route:test";planContext.routeRevision="r1";planContext.phaseDigest=std::string(64,'a');snapshot.acquisitionMetricContext=planContext;
 j::object plannerSnapshot{{"character_id",42},{"level",8},{"phase",1},{"group_size",1},{"map_id",0},{"zone_id",0},{"now_seconds",10000},
  {"core_revision","core-offline"},{"playerbots_revision","pb-offline"},{"content_revision","content-offline"},
  {"content_graph_digest",pins.graphDigest},{"route_id","route:test"},{"route_revision","r1"},
  {"catalogue_digest",std::string(64,'0')},{"profile_id","profile:test"},{"profile_version","r1"},
  {"capability_fingerprint","r1"},{"population_admitted",true},{"schedule_active",true},{"in_combat",false},
  {"on_taxi",false},{"in_battleground_or_arena",false},{"logging_out",false},{"forced_travel",false},
  {"has_human_master",false},{"capabilities",j::object{}}};
 auto parsedContext=context("source:fixture");parsedContext.erase("source_id");
 plannerSnapshot["acquisition_metric_context"]=parsedContext;
 auto parsedSnapshot=ParseGearSnapshot(j::serialize(plannerSnapshot));
 check(parsedSnapshot.acquisitionMetricContext&&parsedSnapshot.acquisitionMetricContext->sessionIncarnation==1,
  "planner metric context was not parsed");
 parsedContext["profile_id"]="profile:other";plannerSnapshot["acquisition_metric_context"]=parsedContext;
 bool inconsistentPlanContextRejected=false;try{(void)ParseGearSnapshot(j::serialize(plannerSnapshot));}
 catch(std::invalid_argument const&){inconsistentPlanContextRejected=true;}
 check(inconsistentPlanContextRejected,"internally mismatched planner metric context was accepted");
 std::vector<ItemUpgradeTarget> targets;for(unsigned i=100;i<114;++i)targets.push_back({"target:"+std::to_string(i),i,15,10,20,0});CharacterResourceSnapshot resources;resources.moneyCopper=10000;resources.professionSkillValues[164]=450;
 auto plan=CanonicalGearPlanningService::PlanOffline(loaded,snapshot,resources,targets).plan;
 check(plan.schemaVersion=="cpi-211-gear-plan-v2"&&plan.selectedSourceId&&plan.evaluations.size()==14,"v2 planner failed family coverage/legal action");
 auto encoded=j::parse(GearPlanToJson(plan));auto const& first=encoded.at("evaluations").as_array().front();
 check(first.at("score").at("risk").is_null()&&first.at("score").at("contention").is_null(),"unknown was serialized as zero");
 check(GearPlanFromJson(j::serialize(encoded))==plan,"v2 serializer roundtrip mismatch");
 auto unauthorized=snapshot;unauthorized.populationAdmitted=false;
 auto denied=CanonicalGearPlanningService::PlanOffline(loaded,unauthorized,resources,targets).plan;
 check(!denied.errors.empty()&&!denied.selectedSourceId,"v2 evidence bypassed a hard population gate");
 auto& vendor=ps.at(3).at("sources").as_array().front();auto vendorId=std::string(vendor.at("source_id").as_string());
 vendor.as_object()["risk_evidence"]=measured(vendorId,"adverse");vendor.as_object()["contention_evidence"]=measured(vendorId,"contended");
 pins.projectionDigest=json_contract::Digest(projections);auto measuredIndex=CanonicalItemSourceLoader::LoadOffline(j::serialize(graph),j::serialize(manifest),j::serialize(projections),pins);
 auto measuredPlan=CanonicalGearPlanningService::PlanOffline(measuredIndex,snapshot,resources,{targets.at(3)}).plan;
 auto measuredJson=j::parse(GearPlanToJson(measuredPlan));auto const& measuredScore=measuredJson.at("evaluations").as_array()[0].at("score");
 check(measuredPlan.evaluations[0].score.risk==0&&measuredPlan.evaluations[0].score.contention==0&&measuredScore.at("risk").is_null()&&measuredScore.at("contention").is_null(),"measured-only evidence was priced as calibrated");
 vendor.as_object()["risk_evidence"]=calibrated(vendorId,0,0,0);vendor.as_object()["contention_evidence"]=unknown(vendorId);
 pins.projectionDigest=json_contract::Digest(projections);auto known=CanonicalItemSourceLoader::LoadOffline(j::serialize(graph),j::serialize(manifest),j::serialize(projections),pins);
 auto zeroPlan=CanonicalGearPlanningService::PlanOffline(known,snapshot,resources,{targets.at(3)}).plan;
 auto zeroJson=j::parse(GearPlanToJson(zeroPlan));check(zeroPlan.evaluations.at(0).score.risk==0&&zeroJson.at("evaluations").as_array()[0].at("score").at("risk")==j::value(0),"known calibrated zero not distinguished from unknown");
 auto malformed=zeroPlan;malformed.evaluations[0].riskEvidence->probabilityPerThousand.reset();bool malformedRejected=false;
 try{(void)GearPlanToJson(malformed);}catch(std::invalid_argument const&){malformedRejected=true;}
 check(malformedRejected,"serializer accepted incomplete calibration evidence");
 vendor.as_object()["risk_evidence"]=calibrated(vendorId,500,400,600);
 vendor.as_object()["contention_evidence"]=calibrated(vendorId,400,300,500);pins.projectionDigest=json_contract::Digest(projections);
 auto priced=CanonicalItemSourceLoader::LoadOffline(j::serialize(graph),j::serialize(manifest),j::serialize(projections),pins);
 auto pricedPlan=CanonicalGearPlanningService::PlanOffline(priced,snapshot,resources,{targets.at(3)}).plan;
 check(pricedPlan.evaluations.at(0).score.risk==-50&&pricedPlan.evaluations.at(0).score.contention==-40,
  "compatible calibrated risk/contention probabilities were not priced");
 auto assertUnpriced=[&](BotProgressSnapshot const& contextSnapshot,char const* label){
  auto mismatch=CanonicalGearPlanningService::PlanOffline(priced,contextSnapshot,resources,{targets.at(3)}).plan;
  auto encodedMismatch=j::parse(GearPlanToJson(mismatch));auto const& evaluation=encodedMismatch.at("evaluations").as_array()[0];
  check(mismatch.selectedSourceId.has_value(),label);
  check(mismatch.evaluations.at(0).score.risk==0,label);
  check(mismatch.evaluations.at(0).score.contention==0,label);
  check(evaluation.at("score").at("risk").is_null(),label);
  check(evaluation.at("score").at("contention").is_null(),label);
  check(evaluation.at("risk_evidence").at("state")==j::value("unknown"),label);
  check(evaluation.at("contention_evidence").at("state")==j::value("unknown"),label);
  check(evaluation.at("risk_evidence").at("unknown_reason")==j::value("plan_context_unavailable_or_mismatched"),label);
  check(evaluation.at("contention_evidence").at("unknown_reason")==j::value("plan_context_unavailable_or_mismatched"),label);
 };
 auto mismatched=snapshot;mismatched.acquisitionMetricContext->actorIdentity="actor:other";assertUnpriced(mismatched,"actor mismatch was priced or blocked the legal source");
 mismatched=snapshot;mismatched.acquisitionMetricContext->profileId="profile:other";assertUnpriced(mismatched,"profile mismatch was priced or blocked the legal source");
 mismatched=snapshot;mismatched.acquisitionMetricContext->partyId="party:other";assertUnpriced(mismatched,"party mismatch was priced or blocked the legal source");
 mismatched=snapshot;mismatched.acquisitionMetricContext->phase=2;assertUnpriced(mismatched,"phase mismatch was priced or blocked the legal source");
 mismatched=snapshot;mismatched.acquisitionMetricContext->routeId="route:other";assertUnpriced(mismatched,"route mismatch was priced or blocked the legal source");
 mismatched=snapshot;mismatched.acquisitionMetricContext->sessionIncarnation=2;assertUnpriced(mismatched,"session epoch mismatch was priced or blocked the legal source");
 mismatched=snapshot;mismatched.acquisitionMetricContext->strategicGeneration=2;assertUnpriced(mismatched,"generation mismatch was priced or blocked the legal source");
 mismatched=snapshot;mismatched.acquisitionMetricContext->goalRevision=1;assertUnpriced(mismatched,"goal revision mismatch was priced or blocked the legal source");
 mismatched=snapshot;mismatched.acquisitionMetricContext->phaseDigest=std::string(64,'b');assertUnpriced(mismatched,"phase digest mismatch was priced or blocked the legal source");
 mismatched=snapshot;mismatched.acquisitionMetricContext.reset();assertUnpriced(mismatched,"missing plan context was priced or blocked the legal source");
 vendor.as_object()["risk_evidence"].as_object()["context"].as_object()["source_id"]="source:wrong";
 pins.projectionDigest=json_contract::Digest(projections);bool wrongSourceRejected=false;
 try{(void)CanonicalItemSourceLoader::LoadOffline(j::serialize(graph),j::serialize(manifest),j::serialize(projections),pins);}
 catch(std::invalid_argument const&){wrongSourceRejected=true;}
 check(wrongSourceRejected,"source context mismatch was not rejected at load");
 std::cout<<"v2 metrics: unknown/unpriced, known-zero/calibrated, calibrated penalty, 14 families, plan roundtrip passed\n";return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
