#include "pbguide/canonical_item_sources.hpp"
#include "pbguide/gear_plan_json.hpp"
#include "../src/json_contract.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>
using namespace pbguide;
namespace j=boost::json;
static_assert(!std::is_default_constructible_v<LoadedItemSources>);
static_assert(!std::is_default_constructible_v<EffectiveDatasetBinding>);
static_assert(!std::is_constructible_v<EffectiveDatasetBinding,OfflineSourcePins>);
static_assert(!std::is_constructible_v<ItemSourceIndex,std::string,std::string,std::vector<ItemSourceOption>,bool>);
void require(bool condition,std::string const& message){if(!condition)throw std::runtime_error(message);}
std::string text(j::value const& v){return std::string(v.as_string());}
int main(int argc,char** argv){try{
 if(argc!=1){
  require(argc==4&&std::string(argv[1])=="--gear-plan-roundtrip","unknown canonical item test mode");
  std::ifstream planStream(argv[2]);require(bool(planStream),"native plan round-trip input open");
  std::string planBytes((std::istreambuf_iterator<char>(planStream)),{});
  auto typedPlan=GearPlanFromJson(planBytes);
  require(typedPlan.schemaVersion=="cpi-211-gear-plan-v3"&&typedPlan.generationIdentity&&
   typedPlan.generationIdentity->generationId==argv[3],"native plan round-trip generation identity mismatch");
  auto serializedPlan=GearPlanToJson(typedPlan);auto restoredPlan=GearPlanFromJson(serializedPlan);
  require(restoredPlan==typedPlan,"typed native plan round-trip changed plan or generation identity");
  require(!j::parse(serializedPlan).as_object().contains("content_revision"),"native round-trip invented unknown content revision");
  std::cout<<serializedPlan<<'\n';return 0;
 }
 std::ifstream stream(std::string(CANONICAL_FIXTURE_DIR)+"/cases.json");std::string bytes((std::istreambuf_iterator<char>(stream)),{});auto cases=j::parse(bytes);std::string baseJson,combinatorJson;std::map<std::string,std::string> reagentBlocks;
 require(cases.is_array()&&!cases.as_array().empty(),"fixture cases must be nonempty");
 std::set<std::string> caseNames;for(auto const& c:cases.as_array())require(caseNames.insert(text(c.at("name"))).second,"duplicate case name");
 for(auto name:{"all-kinds","reordered","wrong-graph-pin","changed-bytes","wrong-generation","wrong-manifest","wrong-projection","wrong-candidate","source-pin","provenance","missing-endpoint","wrong-endpoint-kind","wrong-semantics","duplicate-edge","ambiguous-reverse","duplicate-projection","missing-condition","condition-cycle","invalid-phase","empty-phase","unearned","unearned-dependency","craft-dependency","missing-reagent","wrong-reagent-count","omitted-reagent","blocked-reagent","unknown-kind","forged-verified","forged-domain","migration-reordered","invalid-probability","zero-yield","condition-false","condition-indeterminate","condition-true","condition-leaf","event_ids","pool_ids","phase_mask","spawn_mask","reagent-future-frontier","reagent-empty-frontier","recipe-cycle","invalid-calendar","invalid-clock","invalid-offset","unicode-attributes","condition-combinators","condition-combinators-reordered","nonunit-yield","multibyte-revision-160","multibyte-revision-161","two-blocked-reagents","two-blocked-reagents-reordered","vendor-price"})require(caseNames.contains(name),std::string("missing required positive/negative/permutation case: ")+name);
 require(!AcceptedDatasetRegistry::Lookup("candidate:offline"),"fixture minted realm binding");
 for(auto const& c:cases.as_array()){
  auto name=text(c.at("name"));auto const& p=c.at("pins");OfflineSourcePins pins{text(p.at("candidateId")),text(p.at("generation")),text(p.at("graphDigest")),text(p.at("manifestDigest")),text(p.at("projectionDigest"))};
  auto graph=j::serialize(c.at("graph")),manifest=j::serialize(c.at("manifest")),projections=j::serialize(c.at("projections"));auto expected=text(c.at("error"));
  try{
   auto loaded=CanonicalItemSourceLoader::LoadOffline(graph,manifest,projections,pins);
   require(expected.empty(),name+": accepted invalid input (expected "+expected+")");
   require(loaded.Domain()==ProjectionDomain::OfflineContract&&!loaded.Index().IsVerifiedCanonicalProjection(),name+": trust domain");require(loaded.Index().Sources().size()==14,name+": lost source kind");
   std::set<ItemSourceKind> kinds;for(auto const& s:loaded.Index().Sources())kinds.insert(s.kind);require(kinds.size()==14,name+": lost source kind identity");
   BotProgressSnapshot snapshot;snapshot.characterId=42;snapshot.level=8;snapshot.phase=1;snapshot.nowSeconds=10000;snapshot.coreRevision="core-offline";snapshot.playerbotsRevision="pb-offline";snapshot.contentRevision="content-offline";snapshot.contentGraphDigest=pins.graphDigest;snapshot.routeId="route:offline";snapshot.routeRevision="offline";snapshot.catalogueDigest=std::string(64,'0');snapshot.profileId="profile:offline";snapshot.profileVersion="offline";snapshot.capabilityFingerprint="offline";snapshot.populationAdmitted=true;snapshot.scheduleActive=true;
   std::vector<ItemUpgradeTarget> targets;for(std::uint32_t i=100;i<114;++i)targets.push_back({"target:"+std::to_string(i),i,15,10,20,0});
   auto requireMetadata=[&](GearPlan const& plan){
    require(plan.planId=="gear_plan:42:10000"&&plan.characterId==snapshot.characterId&&plan.createdAtSeconds==snapshot.nowSeconds,name+": rejected identity metadata lost");
    require(plan.contentRevision==snapshot.contentRevision&&plan.contentGraphDigest==snapshot.contentGraphDigest,name+": rejected provenance lost");
    require(plan.policyVersion==GearScoringPolicy{}.version&&plan.policyIdentity==CanonicalGearPolicyIdentity({}),name+": rejected policy identity lost");
    require(plan.evaluations.empty()&&!plan.selectedSourceId,name+": rejected sources evaluated");
    require(j::parse(GearPlanToJson(plan)).at("character_id")==j::value(snapshot.characterId),name+": rejection not serializable");
   };
   auto raw=EarnedGearPlanner().Plan(snapshot,{},loaded.Index(),targets);require(raw.errors==std::vector<std::string>{"unverified_source_index"},name+": ordinary Plan admitted offline index");requireMetadata(raw);
   auto forged=ItemSourceIndex::UnverifiedForTesting(snapshot.contentRevision,pins.graphDigest,loaded.Index().Sources());require(!EarnedGearPlanner().Plan(snapshot,{},forged,targets).errors.empty(),name+": copied digest bypass");
   CharacterResourceSnapshot resources;resources.professionSkillValues[164]=450;
   auto result=CanonicalGearPlanningService::PlanOffline(loaded,snapshot,resources,targets);require(result.domain==ProjectionDomain::OfflineContract,name+": envelope domain");require(result.plan.errors.empty(),name+": shared algorithm errors");require(result.plan.evaluations.size()==14,name+": caller did not execute planner");require(result.plan.selectedSourceId.has_value(),name+": no real selection");
   auto block=text(c.at("block"));if(!block.empty()){require(result.blockedReasons.at("source:00")==block,name+": missing explicit block reason");require(result.plan.selectedSourceId!="source:00",name+": blocked source selected");}
   auto serialized=GearPlanToJson(result.plan);auto parsed=j::parse(serialized);require(j::parse(j::serialize(parsed))==parsed,name+": structural JSON parse/serialize preservation");require(text(parsed.at("content_graph_digest"))==pins.graphDigest,name+": serialized provenance");
   if(name=="all-kinds")baseJson=serialized;if(name=="reordered")require(serialized==baseJson,name+": nondeterministic full explanations");
   if(name=="blocked-reagent"){
    auto craftOnly=CanonicalGearPlanningService::PlanOffline(loaded,snapshot,resources,{targets.at(5)});
    require(!craftOnly.plan.selectedSourceId,"blocked reagent selected by forged available=true");
    require(craftOnly.blockedReasons.at("source:05")=="craft_dependency:event_context_unavailable","missing reagent reason");
   }
   if(name=="reagent-future-frontier"){
    auto early=CanonicalGearPlanningService::PlanOffline(loaded,snapshot,resources,{targets.at(5)});
    require(!early.plan.selectedSourceId,"future reagent pursued at current phase1");
    auto future=snapshot;future.phase=50;
    auto ready=CanonicalGearPlanningService::PlanOffline(loaded,future,resources,{targets.at(5)});
    require(ready.plan.selectedSourceId=="source:05","reagent not available at phase50");
   }
   if(name=="reagent-empty-frontier"){
    auto impossible=CanonicalGearPlanningService::PlanOffline(loaded,snapshot,resources,{targets.at(5)});
    require(!impossible.plan.selectedSourceId,"incompatible reagent frontiers selected");
    require(impossible.blockedReasons.at("source:05")=="craft_dependency:empty_phase_intersection","missing frontier block explanation");
   }
   if(name=="condition-combinators")combinatorJson=serialized;
   if(name=="condition-combinators-reordered")require(combinatorJson==serialized,"condition reorder changed explanations");
   if(name=="two-blocked-reagents")reagentBlocks=result.blockedReasons;
   if(name=="two-blocked-reagents-reordered")require(reagentBlocks==result.blockedReasons,"reagent permutation changed exact blocked reasons");
   if(name=="vendor-price"){
    auto low=resources;low.moneyCopper=249;
    auto poor=CanonicalGearPlanningService::PlanOffline(loaded,snapshot,low,{targets.at(3)}).plan;
    require(!poor.selectedSourceId&&poor.evaluations.size()==1,"below-price vendor selected");
    auto const& reasons=poor.evaluations.at(0).reasonCodes;
    require(std::find(reasons.begin(),reasons.end(),"insufficient_money")!=reasons.end(),"nonzero loaded price lost");
    low.moneyCopper=250;auto paid=CanonicalGearPlanningService::PlanOffline(loaded,snapshot,low,{targets.at(3)}).plan;
    require(paid.selectedSourceId=="source:03"&&paid.evaluations.at(0).score.money==-3&&paid.evaluations.at(0).score.total==3395,"vendor affordability or loaded price scoring wrong");
   }
   if(name=="condition-true"||name=="condition-combinators"||name=="condition-combinators-reordered"){
    require(!result.blockedReasons.contains("source:00"),"satisfied condition recorded blocked");
    auto allowed=CanonicalGearPlanningService::PlanOffline(loaded,snapshot,resources,{targets.at(0)}).plan;
    require(allowed.selectedSourceId=="source:00"&&allowed.evaluations.size()==1&&allowed.evaluations.at(0).eligible,"satisfied condition did not permit isolated source pursuit");
   }
   auto wrongSource=snapshot;wrongSource.coreRevision="forged";
   auto mismatch=CanonicalGearPlanningService::PlanOffline(loaded,wrongSource,resources,targets).plan;
   require(mismatch.errors==std::vector<std::string>{"source_revision_mismatch"},"source revision snapshot forgery");requireMetadata(mismatch);
   auto pristineSnapshot=snapshot;
   snapshot.contentGraphDigest=std::string(64,'f');require(!CanonicalGearPlanningService::PlanOffline(loaded,snapshot,{},targets).plan.errors.empty(),name+": snapshot graph bypass");
   if(name=="all-kinds"){
    std::string duplicate="{\"schema_version\":\"wrong\","+graph.substr(1);try{(void)CanonicalItemSourceLoader::LoadOffline(duplicate,manifest,projections,pins);throw std::runtime_error("duplicate key accepted");}catch(std::invalid_argument const& e){require(std::string(e.what()).find("duplicate_json_key")!=std::string::npos,e.what());}
    // Reach every kind's pursuit path separately rather than only the tie winner.
    for(auto const& target:targets){auto one=CanonicalGearPlanningService::PlanOffline(loaded,pristineSnapshot, resources,{target});require(one.plan.selectedItemId==target.itemId,"source kind did not pursue");}
   }
  }catch(std::invalid_argument const& e){require(!expected.empty(),name+": unexpected rejection: "+e.what());require(std::string(e.what()).find(expected)!=std::string::npos,name+": wrong rejection: "+e.what());}
  std::cout<<name<<" passed\n";
 }
 auto const& allKinds=*std::find_if(cases.as_array().begin(),cases.as_array().end(),[](j::value const& c){return text(c.at("name"))=="all-kinds";});
 auto graph=allKinds.at("graph"),manifest=allKinds.at("manifest"),projections=allKinds.at("projections");
 graph.as_object()["schema_version"]="cpi-211-content-graph-v2";
 for(auto field:{"core_revision","playerbots_revision","progression_revision","content_revision"})graph.as_object()[field]=nullptr;
 auto const unknownComponent=[](){return j::object{{"status","unknown"},{"revision",nullptr},{"pathset",nullptr},{"evidence",j::array{}},{"reason","unknown in the complete synthetic bundle/pathset fixture"}};};
 j::object revisions{{"core",unknownComponent()},{"playerbots",unknownComponent()},{"progression",unknownComponent()},{"content",unknownComponent()}};
 j::object artifact{{"path","work/core2-native-held-custody-recovery-20261021/attempts/01/composition.json"},
  {"bytes",1092619},{"sha256","d866aa6b28761b769de8f7b5bb3d76a66a89389ba5746bbe47372425f4c55b7b"}};
 j::object lifecycle{{"built",false},{"deployed",false},{"runtime_verified",false},
  {"final_composition_acceptance","pending"}};
 j::object pathset{{"schema","realm-vfs-source-composition-v1"},{"artifact",artifact},{"entry_count",5395},
  {"bundle_identity_sha256","f4764eb77295c24027d407bd7476041e5796b625602031549b71465993ce037b"},
  {"lifecycle",lifecycle},{"component_revision_status","unmapped"}};
 j::object sourceIdentity{{"schema","core1-cpi211-source-identity-v2"},{"identity_mode","bundle_pathset"},
  {"bundle_pathset",pathset},{"component_revisions",revisions}};
 j::array batches{j::object{{"batch_ordinal",0},{"operation_id","fixture:vendor"},
  {"source_snapshot_id",std::string(64,'A')},{"request_keys_sha256",std::string(64,'f')}}};
 j::object coverage{{"scope","synthetic_fixture_only"},{"complete",false}};
 j::object scope{{"kind","test_only_vendor"}};
 j::object worldSnapshot{{"status","synthetic_fixture_only"}};
 j::object support{{"compiler",j::object{{"status","unknown"},{"reason","synthetic fixture"}}}};
 j::object closure{{"synthetic_fixture",std::string(64,'1')}};
 j::object commitment{{"scope",scope},{"source_identity",sourceIdentity},{"world_snapshot",worldSnapshot},
  {"raw_generation_snapshot_id",nullptr},{"native_source_snapshot_id",nullptr},{"request_batches",batches},
  {"coverage",coverage},{"support_inputs",support},{"compiler_schema_closure",closure}};
 auto const generationId=json_contract::Digest(commitment);
 j::object generationIdentity{{"schema","core1-cpi211-generation-reference-v1"},{"generation_id",generationId},
  {"source_identity_digest",json_contract::Digest(sourceIdentity)},
  {"bundle_identity_sha256","f4764eb77295c24027d407bd7476041e5796b625602031549b71465993ce037b"},
  {"raw_generation_snapshot_id",nullptr},{"native_source_snapshot_id",nullptr},{"request_batches",batches},
  {"coverage_commitment_sha256",json_contract::Digest(coverage)}};
 graph.as_object()["source_identity"]=sourceIdentity;graph.as_object()["generation_identity"]=generationIdentity;
 auto normalizeAvailability=[](j::value& value){for(auto key:{"event_ids","pool_ids"}){auto& rows=value.at(key).as_array();std::sort(rows.begin(),rows.end(),[](j::value const& a,j::value const& b){return json_contract::Unsigned(a)<json_contract::Unsigned(b);});}};
 for(auto& [kind,nodes]:graph.at("nodes").as_object()){
  (void)kind;auto& rows=nodes.as_array();for(auto& node:rows)normalizeAvailability(node.at("availability"));
  std::sort(rows.begin(),rows.end(),[](j::value const& a,j::value const& b){return json_contract::Unsigned(a.at("id"))<json_contract::Unsigned(b.at("id"));});
 }
 for(auto& edge:graph.at("edges").as_array())normalizeAvailability(edge.at("availability"));
 std::function<void(j::value&)> normalizeExpression=[&](j::value& expression){auto op=text(expression.at("op"));if(op=="all"||op=="any"){auto& args=expression.at("arguments").as_array();for(auto& child:args)normalizeExpression(child);std::sort(args.begin(),args.end(),[](j::value const& a,j::value const& b){return json_contract::Canonical(a)<json_contract::Canonical(b);});}else if(op=="not")normalizeExpression(expression.at("argument"));};
 auto& roots=graph.at("condition_roots").as_array();for(auto& root:roots)normalizeExpression(root.at("expression"));
 std::sort(roots.begin(),roots.end(),[](j::value const& a,j::value const& b){return text(a.at("condition_id"))<text(b.at("condition_id"));});
 auto& edges=graph.at("edges").as_array();std::sort(edges.begin(),edges.end(),[](j::value const& a,j::value const& b){return text(a.at("edge_id"))<text(b.at("edge_id"));});
 auto gd=json_contract::Digest(graph);manifest.as_object()["schema_version"]="cpi-211-content-manifest-v2";
 for(auto field:{"core_revision","playerbots_revision","progression_revision","content_revision"})manifest.as_object().at("source").as_object()[field]=nullptr;
 manifest.as_object()["graph_digest"]=gd;manifest.as_object()["reproducible_peer_digest"]=gd;
 manifest.as_object()["source_identity"]=sourceIdentity;manifest.as_object()["generation_identity"]=generationIdentity;
 manifest.as_object()["identity_commitment"]=commitment;manifest.as_object()["coverage"]=coverage;
 for(auto& projection:projections.as_array()){
  projection.as_object()["schema_version"]="cpi-211-item-source-v3";projection.as_object()["content_revision"]=nullptr;
  projection.as_object()["graph_digest"]=gd;projection.as_object()["generation_identity"]=generationIdentity;
  for(auto& source:projection.at("sources").as_array()){
   auto const sourceId=text(source.at("source_id"));
   j::object context{{"source_id",sourceId},{"actor_identity","fixture:actor"},{"character_id",1},
    {"session_incarnation",1},{"strategic_generation",1},{"goal_revision",0},{"profile_id","fixture:profile"},
    {"party_id",nullptr},{"phase",1},{"route_id","fixture:route"},{"route_revision","fixture-route-r1"},
    {"phase_digest",std::string(64,'e')}};
   source.as_object().erase("risk_milli");source.as_object().erase("contention_milli");
   source.as_object()["risk_evidence"]=j::object{{"state","unknown"},{"context",context},{"unknown_reason","synthetic fixture has no calibrated risk evidence"}};
   source.as_object()["contention_evidence"]=j::object{{"state","unknown"},{"context",context},{"unknown_reason","synthetic fixture has no calibrated contention evidence"}};
  }
 }
 OfflineSourcePins pairedPins{text(allKinds.at("pins").at("candidateId")),generationId,gd,
  json_contract::Digest(manifest),json_contract::Digest(projections)};
 auto paired=CanonicalItemSourceLoader::LoadOffline(json_contract::Canonical(graph),
  json_contract::Canonical(manifest),json_contract::Canonical(projections),pairedPins);
 require(paired.Generation().has_value()&&paired.Index().GenerationRevisions().has_value(),"paired identity/status was not retained by registered loader");
 BotProgressSnapshot unknownSnapshot;unknownSnapshot.characterId=42;unknownSnapshot.level=8;unknownSnapshot.phase=1;
 unknownSnapshot.nowSeconds=10000;unknownSnapshot.contentGraphDigest=gd;unknownSnapshot.routeId="route:offline";
 unknownSnapshot.routeRevision="offline";unknownSnapshot.catalogueDigest=std::string(64,'0');
 unknownSnapshot.profileId="profile:offline";unknownSnapshot.profileVersion="offline";
 unknownSnapshot.capabilityFingerprint="offline";unknownSnapshot.populationAdmitted=true;unknownSnapshot.scheduleActive=true;
 auto pairedResult=CanonicalGearPlanningService::PlanOffline(paired,unknownSnapshot,{},{});
 require(pairedResult.plan.errors.empty(),"all-unknown paired revisions refused: "+(pairedResult.plan.errors.empty()?std::string{}:pairedResult.plan.errors.front()));
 require(pairedResult.plan.schemaVersion=="cpi-211-gear-plan-v3"&&pairedResult.plan.generationIdentity==paired.Generation(),"paired identity did not reach the v3 plan");
 auto overflowTarget=ItemUpgradeTarget{"target:paired-overflow",paired.Index().Sources().front().itemId,
  15,std::numeric_limits<Score>::max(),80,0};
 auto pairedOverflow=CanonicalGearPlanningService::PlanOffline(paired,unknownSnapshot,{}, {overflowTarget});
 require(!pairedOverflow.plan.selectedSourceId&&pairedOverflow.plan.evaluations.size()==1&&
  pairedOverflow.plan.evaluations.front().disposition==GearDisposition::Abandoned&&
  std::find(pairedOverflow.plan.evaluations.front().reasonCodes.begin(),pairedOverflow.plan.evaluations.front().reasonCodes.end(),"score_overflow")!=pairedOverflow.plan.evaluations.front().reasonCodes.end(),
  "paired planner did not fail closed on score overflow");
 auto pairedJson=GearPlanToJson(pairedResult.plan);auto pairedDocument=j::parse(pairedJson);
 require(!pairedDocument.as_object().contains("content_revision"),"unknown content revision was serialized as a placeholder");
 auto restored=GearPlanFromJson(pairedJson);
 require(restored==pairedResult.plan,"paired identity did not survive native plan round-trip");
 auto mismatchedIdentityGraph=graph;
 mismatchedIdentityGraph.at("generation_identity").as_object()["generation_id"]=std::string(64,'0');
 try{(void)CanonicalItemSourceLoader::LoadOffline(json_contract::Canonical(mismatchedIdentityGraph),
   json_contract::Canonical(manifest),json_contract::Canonical(projections),pairedPins);
  throw std::runtime_error("paired graph/manifest identity mismatch accepted");
 }catch(std::invalid_argument const& e){require(std::string(e.what()).find("generation_identity_mismatch")!=std::string::npos,e.what());}
 auto mismatchedProjection=projections;
 mismatchedProjection.as_array()[0].as_object().at("generation_identity").as_object()["generation_id"]=std::string(64,'0');
 auto mismatchProjectionPins=pairedPins;mismatchProjectionPins.projectionDigest=json_contract::Digest(mismatchedProjection);
 try{(void)CanonicalItemSourceLoader::LoadOffline(json_contract::Canonical(graph),
   json_contract::Canonical(manifest),json_contract::Canonical(mismatchedProjection),mismatchProjectionPins);
  throw std::runtime_error("paired projection identity mismatch accepted");
 }catch(std::invalid_argument const& e){require(std::string(e.what()).find("projection_generation_identity_mismatch")!=std::string::npos,e.what());}
 auto mismatchedGraphPins=pairedPins;mismatchedGraphPins.graphDigest=std::string(64,'0');
 try{(void)CanonicalItemSourceLoader::LoadOffline(json_contract::Canonical(graph),
   json_contract::Canonical(manifest),json_contract::Canonical(projections),mismatchedGraphPins);
  throw std::runtime_error("paired graph digest mismatch accepted");
 }catch(std::invalid_argument const& e){require(std::string(e.what()).find("graph_digest_mismatch")!=std::string::npos,e.what());}
 auto forgedRevision=unknownSnapshot;forgedRevision.coreRevision=std::string(40,'a');
 auto revisionMismatch=CanonicalGearPlanningService::PlanOffline(paired,forgedRevision,{},{});
 require(revisionMismatch.plan.errors==std::vector<std::string>{"source_revision_mismatch"},"known/unknown component revision mismatch was not refused");
 auto knownGraph=graph,knownManifest=manifest,knownProjections=projections;
 auto knownSourceIdentity=sourceIdentity;auto const knownCore=std::string(40,'a');
 auto& coreStatus=knownSourceIdentity.at("component_revisions").as_object().at("core").as_object();
 coreStatus["status"]="verified";coreStatus["revision"]=knownCore;coreStatus["reason"]=nullptr;
 knownGraph.as_object()["source_identity"]=knownSourceIdentity;knownGraph.as_object()["core_revision"]=knownCore;
 auto knownIdentity=knownGraph.at("generation_identity").as_object();
 knownIdentity["source_identity_digest"]=json_contract::Digest(knownSourceIdentity);
 knownManifest.as_object()["source_identity"]=knownSourceIdentity;
 knownManifest.at("source").as_object()["core_revision"]=knownCore;
 auto knownCommitment=knownManifest.at("identity_commitment").as_object();
 knownCommitment["source_identity"]=knownSourceIdentity;
 knownManifest.as_object()["identity_commitment"]=knownCommitment;
 auto const knownGeneration=json_contract::Digest(knownCommitment);knownIdentity["generation_id"]=knownGeneration;
 knownGraph.as_object()["generation_identity"]=knownIdentity;
 knownManifest.as_object()["generation_identity"]=knownIdentity;
 auto const knownGraphDigest=json_contract::Digest(knownGraph);
 knownManifest.as_object()["graph_digest"]=knownGraphDigest;knownManifest.as_object()["reproducible_peer_digest"]=knownGraphDigest;
 for(auto& projection:knownProjections.as_array()){
  projection.as_object()["generation_identity"]=knownIdentity;projection.as_object()["graph_digest"]=knownGraphDigest;
 }
 OfflineSourcePins knownPins{pairedPins.candidateId,knownGeneration,knownGraphDigest,
  json_contract::Digest(knownManifest),json_contract::Digest(knownProjections)};
 auto knownLoaded=CanonicalItemSourceLoader::LoadOffline(json_contract::Canonical(knownGraph),
  json_contract::Canonical(knownManifest),json_contract::Canonical(knownProjections),knownPins);
 auto knownSnapshot=unknownSnapshot;knownSnapshot.coreRevision=knownCore;knownSnapshot.contentGraphDigest=knownGraphDigest;
 auto knownAccepted=CanonicalGearPlanningService::PlanOffline(knownLoaded,knownSnapshot,{},{});
 require(knownAccepted.plan.errors.empty(),"verified paired component revision did not match the snapshot");
 knownSnapshot.coreRevision=std::string(40,'b');
 auto knownMismatch=CanonicalGearPlanningService::PlanOffline(knownLoaded,knownSnapshot,{},{});
 require(knownMismatch.plan.errors==std::vector<std::string>{"source_revision_mismatch"},"verified paired component revision mismatch was not refused");
 auto forgedContext=unknownSnapshot;forgedContext.contentGraphDigest=std::string(64,'f');
 auto contextMismatch=CanonicalGearPlanningService::PlanOffline(paired,forgedContext,{},{});
 require(contextMismatch.plan.errors==std::vector<std::string>{"content_graph_identity_mismatch"},"paired context mismatch was not refused");
 auto legacyGraph=allKinds.at("graph");legacyGraph.as_object()["core_revision"]=nullptr;
 try{(void)CanonicalItemSourceLoader::LoadOffline(json_contract::Canonical(legacyGraph),
   j::serialize(allKinds.at("manifest")),j::serialize(allKinds.at("projections")),
   {text(allKinds.at("pins").at("candidateId")),text(allKinds.at("pins").at("generation")),
    text(allKinds.at("pins").at("graphDigest")),text(allKinds.at("pins").at("manifestDigest")),text(allKinds.at("pins").at("projectionDigest"))});
  throw std::runtime_error("legacy v1 accepted null component revision");
 }catch(std::invalid_argument const&){}
 auto legacyContentBehavior=CanonicalItemSourceLoader::LoadOffline(j::serialize(allKinds.at("graph")),
  j::serialize(allKinds.at("manifest")),j::serialize(allKinds.at("projections")),
  {text(allKinds.at("pins").at("candidateId")),text(allKinds.at("pins").at("generation")),
   text(allKinds.at("pins").at("graphDigest")),text(allKinds.at("pins").at("manifestDigest")),text(allKinds.at("pins").at("projectionDigest"))});
 require(!legacyContentBehavior.Generation().has_value(),"legacy content revision comparison changed behavior");
 return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
