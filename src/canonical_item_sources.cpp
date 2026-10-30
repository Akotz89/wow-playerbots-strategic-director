#include "pbguide/canonical_item_sources.hpp"
#include "canonical_schemas.hpp"
#include "json_contract.hpp"
#include "acquisition_metric_json.hpp"
#include <boost/json.hpp>
#include <openssl/sha.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
namespace pbguide { namespace {
namespace j=boost::json;
using V=j::value; using O=j::object; using A=j::array;
[[noreturn]] void fail(std::string const& reason) { throw std::invalid_argument("canonical_item_sources:"+reason); }
std::string str(V const& v) { return std::string(v.as_string()); }
std::uint64_t num(V const& v) { if(v.is_uint64()) return v.as_uint64(); if(v.is_int64()&&v.as_int64()>=0) return static_cast<std::uint64_t>(v.as_int64()); fail("unsigned_integer"); }
// Preliminary recursive token walk rejects duplicate keys (including escaped spellings)
// before Boost's DOM parser could discard them. DOM parsing supplies full JSON/UTF8 grammar validation.
class UniqueKeys {
 std::string_view s; std::size_t p=0;
 void ws(){while(p<s.size()&&(s[p]==' '||s[p]=='\n'||s[p]=='\r'||s[p]=='\t'))++p;}
 std::string token(){std::size_t b=p++; bool esc=false;while(p<s.size()){char c=s[p++];if(!esc&&c=='"')return str(j::parse(s.substr(b,p-b)));if(!esc&&c=='\\')esc=true;else esc=false;}fail("unterminated_string");}
 void value(unsigned depth){if(depth>128)fail("json_depth");ws();if(p>=s.size())fail("json_truncated");char c=s[p++];if(c=='"'){--p;token();return;}if(c=='{'||c=='['){std::set<std::string> keys;ws();char end=c=='{'?'}':']';if(p<s.size()&&s[p]==end){++p;return;}while(true){ws();if(c=='{'){if(p>=s.size()||s[p]!='"')fail("object_key");if(!keys.insert(token()).second)fail("duplicate_json_key");ws();if(p>=s.size()||s[p++]!=':')fail("object_colon");}value(depth+1);ws();if(p>=s.size())fail("json_truncated");char next=s[p++];if(next==end)return;if(next!=',')fail("json_separator");}}while(p<s.size()&&s[p]!=','&&s[p]!=']'&&s[p]!='}'&&s[p]!=' '&&s[p]!='\n'&&s[p]!='\r'&&s[p]!='\t')++p;}
 public: explicit UniqueKeys(std::string_view bytes):s(bytes){} void check(){value(0);ws();if(p!=s.size())fail("trailing_json");}
};
V parse(std::string_view s){if(s.size()>json_contract::MaxDocumentBytes)fail("byte_bound");UniqueKeys(s).check();return j::parse(s);}
std::string canonical(V const& v){
 if(v.is_object()){std::map<std::string,V const*> sorted;for(auto const& kv:v.as_object())sorted.emplace(std::string(kv.key()),&kv.value());std::string r="{";for(auto const& [k,x]:sorted){if(r.size()>1)r+=",";r+=j::serialize(V(k))+":"+canonical(*x);}return r+"}";}
 if(v.is_array()){std::string r="[";for(auto const& x:v.as_array()){if(r.size()>1)r+=",";r+=canonical(x);}return r+"]";}
 if(v.is_double())fail("floating_point_attributes_unsupported");
 return j::serialize(v);
}
std::string digest(V const& v){auto bytes=canonical(v);unsigned char hash[SHA256_DIGEST_LENGTH];SHA256(reinterpret_cast<unsigned char const*>(bytes.data()),bytes.size(),hash);std::ostringstream out;for(auto b:hash)out<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(b);return out.str();}
bool sha256(std::string const& s){return std::regex_match(s,std::regex("[0-9a-f]{64}"));}
GenerationIdentity generationIdentity(V const& value){
 auto const& o=value.as_object();
 if(o.size()!=8||str(o.at("schema"))!="core1-cpi211-generation-reference-v1")fail("generation_identity_shape");
 GenerationIdentity result;result.schema=str(o.at("schema"));result.generationId=str(o.at("generation_id"));result.sourceIdentityDigest=str(o.at("source_identity_digest"));result.bundleIdentitySha256=str(o.at("bundle_identity_sha256"));result.coverageCommitmentSha256=str(o.at("coverage_commitment_sha256"));
 if(!sha256(result.generationId)||!sha256(result.sourceIdentityDigest)||!sha256(result.bundleIdentitySha256)||!sha256(result.coverageCommitmentSha256))fail("generation_identity_digest");
 auto const& raw=o.at("raw_generation_snapshot_id");if(!raw.is_null())result.rawGenerationSnapshotId=str(raw);
 auto const& native=o.at("native_source_snapshot_id");if(!native.is_null()){result.nativeSourceSnapshotId=str(native);if(!std::regex_match(*result.nativeSourceSnapshotId,std::regex("[0-9A-F]{64}")))fail("generation_identity_native_id");}
 std::set<std::uint64_t> ordinals;std::set<std::string> operations,subsets;
 for(auto const& record:o.at("request_batches").as_array()){
  auto ordinal=num(record.at("batch_ordinal"));auto operation=str(record.at("operation_id"));auto subset=str(record.at("source_snapshot_id"));auto requestHash=str(record.at("request_keys_sha256"));
  if(ordinal>16383||operation.empty()||!std::regex_match(requestHash,std::regex("[0-9a-f]{64}"))||!std::regex_match(subset,std::regex("[0-9A-F]{64}"))||!ordinals.insert(ordinal).second||!operations.insert(operation).second||!subsets.insert(subset).second)fail("generation_identity_batch");
  result.requestBatches.push_back({static_cast<std::uint32_t>(ordinal),std::move(operation),std::move(subset),std::move(requestHash)});
 }
 if(!std::is_sorted(result.requestBatches.begin(),result.requestBatches.end(),[](auto const& a,auto const& b){return a.batchOrdinal<b.batchOrdinal;}))fail("generation_identity_batch_order");
 return result;
}
std::optional<std::string> componentRevision(V const& sourceIdentity,char const* name,V const& graphRevision){
 auto const& status=sourceIdentity.at("component_revisions").at(name);
 auto const state=str(status.at("status"));auto const& revision=status.at("revision");
 if(state=="verified"){
  if(revision.is_null()||graphRevision.is_null()||revision!=graphRevision)fail("component_revision_status_mismatch:"+std::string(name));
  return str(revision);
 }
 if((state!="unknown"&&state!="unmapped")||!revision.is_null()||!graphRevision.is_null())fail("component_revision_status_mismatch:"+std::string(name));
 return std::nullopt;
}
void sortBy(A& a,char const* key){std::sort(a.begin(),a.end(),[&](V const& l,V const& r){return str(l.at(key))<str(r.at(key));});}
void sortSet(A& a){std::sort(a.begin(),a.end(),[](V const& l,V const& r){return canonical(l)<canonical(r);});}
void calendarDateTime(std::string const& text){
 int y=std::stoi(text.substr(0,4)),m=std::stoi(text.substr(5,2)),d=std::stoi(text.substr(8,2));
 std::chrono::year_month_day date{std::chrono::year{y},std::chrono::month{static_cast<unsigned>(m)},std::chrono::day{static_cast<unsigned>(d)}};
 if(std::stoi(text.substr(17,2))==60)fail("datetime_leap_second_unsupported");
 if(y<1||!date.ok()||std::stoi(text.substr(11,2))>23||std::stoi(text.substr(14,2))>59||std::stoi(text.substr(17,2))>59)fail("schema_datetime_calendar");
 if(text.back()!='Z'&&text.back()!='z'){auto offset=text.size()-6;if(std::stoi(text.substr(offset+1,2))>23||std::stoi(text.substr(offset+4,2))>59)fail("schema_datetime_offset");}
}
// JSON parsing has already validated UTF-8; each noncontinuation byte begins one codepoint.
std::size_t codepoints(std::string const& text){return static_cast<std::size_t>(std::count_if(text.begin(),text.end(),[](char byte){return (static_cast<unsigned char>(byte)&0xc0u)!=0x80u;}));}
// Validate the exact shipped schemas, rather than maintaining weaker duplicate field contracts.
void schema(V const& v,V const& s,V const& root,V const& docs,unsigned depth=0){
 if(depth>256)fail("schema_depth");
 auto const& o=s.as_object();
 if(auto r=o.if_contains("$ref")){auto ref=str(*r);auto hash=ref.find('#');std::string file=ref.substr(0,hash);V const* document=&root;if(!file.empty()){if(auto named=docs.as_object().if_contains(file))document=named;else{for(auto const& entry:docs.as_object()){auto const* id=entry.value().as_object().if_contains("$id");if(id&&str(*id)==file){document=&entry.value();break;}}if(document==&root)fail("schema_reference_document");}}V const* target=document;std::string path=ref.substr(hash+1);std::size_t start=1;while(start<path.size()){auto end=path.find('/',start);target=&target->at(path.substr(start,end-start));if(end==std::string::npos)break;start=end+1;}schema(v,*target,*document,docs,depth+1);return;}
 for(auto key:{"oneOf","anyOf"})if(auto alternatives=o.if_contains(key)){unsigned valid=0;for(auto const& alt:alternatives->as_array()){try{schema(v,alt,root,docs,depth+1);++valid;}catch(std::invalid_argument const&){}}if(valid==0||(std::string(key)=="oneOf"&&valid!=1))fail("schema_alternative");}
 if(auto c=o.if_contains("const");c&&v!=*c)fail("schema_const");
 if(auto e=o.if_contains("enum");e&&std::find(e->as_array().begin(),e->as_array().end(),v)==e->as_array().end())fail("schema_enum");
 auto type=[&](std::string const& t){return t=="object"?v.is_object():t=="array"?v.is_array():t=="string"?v.is_string():t=="integer"?(v.is_int64()||v.is_uint64()):t=="boolean"?v.is_bool():t=="null"?v.is_null():false;};
 if(auto t=o.if_contains("type")){bool ok=false;if(t->is_array()){for(auto const& x:t->as_array())ok|=type(str(x));}else ok=type(str(*t));if(!ok)fail("schema_type");}
 if(v.is_int64()||v.is_uint64()){
  auto less=[](V const& a,V const& b){
   bool an=a.is_int64()&&a.as_int64()<0,bn=b.is_int64()&&b.as_int64()<0;
   if(an!=bn)return an;
   if(an)return a.as_int64()<b.as_int64();
   return num(a)<num(b);
  };
  for(auto key:{"minimum","maximum"})if(auto b=o.if_contains(key))
   if((std::string(key)=="minimum"&&less(v,*b))||(std::string(key)=="maximum"&&less(*b,v)))fail("schema_numeric_bound");
 }
 if(v.is_string()){auto text=str(v);if(auto b=o.if_contains("maxLength");b&&codepoints(text)>num(*b))fail("schema_string_bound");if(auto b=o.if_contains("minLength");b&&codepoints(text)<num(*b))fail("schema_string_bound");if(auto pat=o.if_contains("pattern");pat&&!std::regex_search(text,std::regex(str(*pat))))fail("schema_pattern");if(auto fmt=o.if_contains("format");fmt&&str(*fmt)=="date-time"&&!std::regex_match(text,std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2}[Tt][0-9]{2}:[0-9]{2}:[0-9]{2}(\\.[0-9]+)?([Zz]|[+-][0-9]{2}:[0-9]{2})")))fail("schema_datetime");if(auto fmt=o.if_contains("format");fmt&&str(*fmt)=="date-time")calendarDateTime(text);}
 if(v.is_array()){auto const& a=v.as_array();if(auto b=o.if_contains("maxItems");b&&a.size()>num(*b))fail("schema_array_bound");if(auto b=o.if_contains("minItems");b&&a.size()<num(*b))fail("schema_array_bound");std::set<std::string> seen;for(auto const& x:a){if(auto unique=o.if_contains("uniqueItems");unique&&unique->as_bool()&&!seen.insert(canonical(x)).second)fail("schema_duplicate");if(auto items=o.if_contains("items"))schema(x,*items,root,docs,depth+1);}}
 if(v.is_object()){auto const& obj=v.as_object();if(auto req=o.if_contains("required"))for(auto const& key:req->as_array())if(!obj.contains(str(key)))fail("schema_required:"+str(key));for(auto const& kv:obj){if(auto pn=o.if_contains("propertyNames"))schema(V(kv.key()),*pn,root,docs,depth+1);V const* prop=nullptr;if(auto props=o.if_contains("properties"))prop=props->as_object().if_contains(kv.key());if(prop)schema(kv.value(),*prop,root,docs,depth+1);else if(auto ap=o.if_contains("additionalProperties")){if(ap->is_bool()){if(!ap->as_bool())fail("schema_extra:"+std::string(kv.key()));}else schema(kv.value(),*ap,root,docs,depth+1);}}}
}
std::map<std::string,std::string> const kinds={{"quest","quests"},{"item","items"},{"creature","creatures"},{"gameobject","gameobjects"},{"spell","spells"},{"trainer","trainers"},{"vendor","vendors"},{"taxi_node","taxi_nodes"},{"area","areas"},{"map","maps"},{"dungeon","dungeons"},{"loot_template","loot_templates"},{"recipe","recipes"}};
std::string identity(V const& ref){return str(ref.at("kind"))+":"+std::to_string(num(ref.at("id")));}
void normalizeExpression(V& e){auto op=str(e.at("op"));if(op=="all"||op=="any"){auto& a=e.at("arguments").as_array();for(auto& x:a)normalizeExpression(x);sortSet(a);}else if(op=="not")normalizeExpression(e.at("argument"));}
struct Adapter{ItemSourceKind kind;char const* node;char const* edge;char const* semantics;};
std::map<std::string,Adapter> const adapters={
 {"quest_reward",{ItemSourceKind::QuestReward,"quest","quest_reward","quest_reward"}},
 {"creature_loot",{ItemSourceKind::CreatureDrop,"creature","loot_source","creature_loot"}},
 {"gameobject_loot",{ItemSourceKind::GameObjectDrop,"gameobject","loot_source","gameobject_loot"}},
 {"vendor",{ItemSourceKind::Vendor,"vendor","vendor_source","vendor"}},
 {"trainer_recipe",{ItemSourceKind::TrainerRecipe,"trainer","trainer_spell","trainer_recipe"}},
 {"craft",{ItemSourceKind::Craft,"recipe","recipe_product","craft"}},
 {"pvp_reward",{ItemSourceKind::PvpReward,"vendor","vendor_source","pvp_reward"}},
 {"dungeon_quest",{ItemSourceKind::DungeonQuest,"quest","quest_reward","dungeon_quest"}},
 {"dungeon_drop",{ItemSourceKind::DungeonDrop,"creature","loot_source","dungeon_drop"}},
 {"disenchant",{ItemSourceKind::Disenchant,"item","loot_source","disenchant"}},
 {"prospecting",{ItemSourceKind::Prospecting,"item","loot_source","prospecting"}},
 {"milling",{ItemSourceKind::Milling,"item","loot_source","milling"}},
 {"fishing",{ItemSourceKind::Fishing,"loot_template","loot_source","fishing"}},
 {"skinning",{ItemSourceKind::Skinning,"creature","loot_source","skinning"}}};
}
namespace json_contract {
boost::json::value Parse(std::string_view bytes) {
 try { return parse(bytes); } catch (std::exception const&) { throw std::invalid_argument("json_contract:invalid_json"); }
}
void Validate(V const& value, std::string_view name) {
 try {
  static V const docs=j::parse(CanonicalSchemas);
  auto const& root=docs.at(std::string(name));schema(value,root,root,docs);
 } catch(std::exception const&) { throw std::invalid_argument("json_contract:invalid_schema"); }
}
std::string Canonical(V const& value) { return canonical(value); }
std::string Digest(V const& value) { return digest(value); }
std::uint64_t Unsigned(V const& value) { return num(value); }
std::int64_t Signed(V const& value) {
 if(value.is_int64())return value.as_int64();
 if(value.is_uint64()&&value.as_uint64()<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))return static_cast<std::int64_t>(value.as_uint64());
 throw std::invalid_argument("json_contract:signed_integer");
}
}
EffectiveDatasetBinding EffectiveDatasetBinding::FromRootAcceptedManifest(std::string_view bytes, std::string_view expectedSha256) {
 auto hex=[](std::string_view x){return x.size()==64 && std::all_of(x.begin(),x.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});};
 if(!hex(expectedSha256)||bytes.size()>1024*1024)fail("accepted_manifest_bound_or_hash");
 unsigned char hash[SHA256_DIGEST_LENGTH];SHA256(reinterpret_cast<unsigned char const*>(bytes.data()),bytes.size(),hash);
 std::ostringstream actual;for(auto b:hash)actual<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(b);
 if(actual.str()!=expectedSha256)fail("accepted_manifest_hash_mismatch");
 auto accepted=parse(bytes);auto const& o=accepted.as_object();auto const version=str(o.at("schema_version"));
 bool const v2=version=="cpi211-root-accepted-item-sources-v2";
 if((!v2&&(o.size()!=7||version!="cpi211-root-accepted-item-sources-v1"))||(v2&&o.size()!=8)||str(o.at("status"))!="ROOT_ACCEPTED_EFFECTIVE_REALM_ITEM_SOURCES")fail("accepted_manifest_status_or_schema");
 OfflineSourcePins pins{str(o.at("candidate_id")),str(o.at("generation")),str(o.at("graph_digest")),str(o.at("manifest_digest")),str(o.at("projection_digest"))};
 if(pins.candidateId.empty()||pins.candidateId.size()>256||!hex(pins.generation)||!hex(pins.graphDigest)||!hex(pins.manifestDigest)||!hex(pins.projectionDigest))fail("accepted_manifest_pins");
 if(v2&&generationIdentity(o.at("generation_identity")).generationId!=pins.generation)fail("accepted_generation_identity_mismatch");
 return EffectiveDatasetBinding(std::move(pins));
}
std::optional<EffectiveDatasetBinding> AcceptedDatasetRegistry::Lookup(std::string_view){return std::nullopt;}
LoadedItemSources CanonicalItemSourceLoader::LoadOffline(std::string_view g,std::string_view m,std::string_view p,OfflineSourcePins const& pins){return Load(g,m,p,pins,ProjectionDomain::OfflineContract);}
LoadedItemSources CanonicalItemSourceLoader::LoadEffectiveRealm(std::string_view g,std::string_view m,std::string_view p,EffectiveDatasetBinding const& b){return Load(g,m,p,b.pins_,ProjectionDomain::EffectiveRealm);}
LoadedItemSources CanonicalItemSourceLoader::Load(std::string_view gb,std::string_view mb,std::string_view pb,OfflineSourcePins const& pins,ProjectionDomain domain){
 V graph=parse(gb),manifest=parse(mb),projections=parse(pb);V docs=j::parse(CanonicalSchemas);
 auto validate=[&](V const& v,char const* name){auto const& root=docs.at(name);schema(v,root,root,docs);};
 auto const graphVersion=str(graph.at("schema_version"));auto const manifestVersion=str(manifest.at("schema_version"));
 bool const paired=graphVersion=="cpi-211-content-graph-v2";
 if(paired!=(manifestVersion=="cpi-211-content-manifest-v2"))fail("mixed_generation_schema_versions");
 validate(graph,paired?"content-graph-v2.schema.json":"content-graph-v1.schema.json");
 validate(manifest,paired?"content-manifest-v2.schema.json":"content-manifest-v1.schema.json");
 std::optional<GenerationIdentity> pairedIdentity;
 std::optional<GenerationComponentRevisions> pairedRevisions;
 if(paired){
  pairedIdentity=generationIdentity(graph.at("generation_identity"));
  auto manifestIdentity=generationIdentity(manifest.at("generation_identity"));
  if(*pairedIdentity!=manifestIdentity||graph.at("source_identity")!=manifest.at("source_identity"))fail("generation_identity_mismatch");
  auto const& sourceIdentity=graph.at("source_identity");
  pairedRevisions=GenerationComponentRevisions{
   componentRevision(sourceIdentity,"core",graph.at("core_revision")),
   componentRevision(sourceIdentity,"playerbots",graph.at("playerbots_revision")),
   componentRevision(sourceIdentity,"progression",graph.at("progression_revision")),
   componentRevision(sourceIdentity,"content",graph.at("content_revision"))};
  if(digest(sourceIdentity)!=pairedIdentity->sourceIdentityDigest||
     str(sourceIdentity.at("bundle_pathset").at("bundle_identity_sha256"))!=pairedIdentity->bundleIdentitySha256||
     digest(manifest.at("identity_commitment"))!=pairedIdentity->generationId||
     digest(manifest.at("coverage"))!=pairedIdentity->coverageCommitmentSha256||
     manifest.at("identity_commitment").at("source_identity")!=sourceIdentity||
     manifest.at("identity_commitment").at("coverage")!=manifest.at("coverage"))fail("generation_commitment_mismatch");
  if(pins.generation!=pairedIdentity->generationId)fail("generation_pin_mismatch");
 }
 if(!projections.is_array()||projections.as_array().size()>10000)fail("projection_array_bound");
 std::map<std::string,V*> conditions;for(auto& c:graph.at("condition_roots").as_array()){auto id=str(c.at("condition_id"));if(!conditions.emplace(id,&c.at("expression")).second)fail("duplicate_condition");normalizeExpression(c.at("expression"));}
 std::map<std::string,unsigned> conditionState;std::map<std::string,int> conditionValue;
 std::function<int(V const&,unsigned)> expression;std::function<int(std::string const&,unsigned)> condition;
 condition=[&](std::string const& id,unsigned depth){if(depth>128)fail("condition_depth");if(!conditions.contains(id))fail("missing_condition:"+id);if(conditionState[id]==1)fail("condition_cycle");if(conditionState[id]==2)return conditionValue.at(id);conditionState[id]=1;int result=expression(*conditions.at(id),depth+1);conditionState[id]=2;conditionValue[id]=result;return result;};
 expression=[&](V const& e,unsigned depth)->int{if(depth>128)fail("condition_depth");auto op=str(e.at("op"));if(op=="reference")return condition(str(e.at("condition_id")),depth+1);if(op=="true")return 1;if(op=="false")return 0;if(op=="leaf"||op=="indeterminate")return -1;if(op=="not"){int n=expression(e.at("argument"),depth+1);return n<0?-1:1-n;}bool unknown=false,yes=false,no=false;for(auto const& a:e.at("arguments").as_array()){int n=expression(a,depth+1);unknown|=n<0;yes|=n==1;no|=n==0;}return op=="all"?(no?0:unknown?-1:1):(yes?1:unknown?-1:0);};
 for(auto const& [id,e]:conditions){(void)e;condition(id,0);}
 auto availability=[&](V& a){if(num(a.at("minimum_frontier"))>num(a.at("maximum_frontier")))fail("invalid_frontier");if(!a.at("condition_id").is_null())condition(str(a.at("condition_id")),0);for(auto key:{"event_ids","pool_ids"}){auto& x=a.at(key).as_array();std::sort(x.begin(),x.end(),[](V const& l,V const& r){return num(l)<num(r);});}};
 std::map<std::string,V const*> nodes;for(auto const& [kind,collection]:kinds){auto& a=graph.at("nodes").at(collection).as_array();for(auto& node:a){auto id=kind+":"+std::to_string(num(node.at("id")));if(!nodes.emplace(id,&node).second)fail("duplicate_node");availability(node.at("availability"));}std::sort(a.begin(),a.end(),[](V const& l,V const& r){return num(l.at("id"))<num(r.at("id"));});}
 // Sorting arrays moves values: rebind every pointer after normalization.
 nodes.clear();for(auto const& [kind,collection]:kinds)for(auto const& node:graph.at("nodes").at(collection).as_array())nodes.emplace(kind+":"+std::to_string(num(node.at("id"))),&node);
 std::set<std::string> edgeIds,reverse,provenance;
 for(auto& edge:graph.at("edges").as_array()){
  if(!edgeIds.insert(str(edge.at("edge_id"))).second)fail("duplicate_edge");
  for(auto endpoint:{"from","to"})if(!nodes.contains(identity(edge.at(endpoint))))fail("missing_endpoint");
  auto reverseId=canonical(A{edge.at("kind"),edge.at("from"),edge.at("to"),edge.at("semantics")});
  if(!reverse.insert(reverseId).second)fail("ambiguous_reverse_identity");
  if(!provenance.insert(canonical(edge.at("provenance"))).second)fail("ambiguous_provenance");
  availability(edge.at("availability"));
 }
 sortBy(graph.at("condition_roots").as_array(),"condition_id");sortBy(graph.at("edges").as_array(),"edge_id");
 std::map<std::string,V const*> edges;for(auto const& edge:graph.at("edges").as_array())edges.emplace(str(edge.at("edge_id")),&edge);
 std::map<std::string,std::vector<V const*>> reagentsBySource;
 for(auto const& edge:graph.at("edges").as_array())if(str(edge.at("kind"))=="recipe_reagent")reagentsBySource[identity(edge.at("from"))].push_back(&edge);
 auto gd=digest(graph);if(gd!=pins.graphDigest||str(manifest.at("graph_digest"))!=gd||str(manifest.at("reproducible_peer_digest"))!=gd)fail("graph_digest_mismatch");
 auto const& source=manifest.at("source");if(str(source.at("candidate_id"))!=pins.candidateId)fail("candidate_mismatch");
 for(auto key:{"core_revision","playerbots_revision","progression_revision","world_dump_digest","dbc_set_digest"}){
  auto const* sourceValue=source.as_object().if_contains(key);auto const* graphValue=graph.as_object().if_contains(key);
  if(!sourceValue||!graphValue||*sourceValue!=*graphValue)fail("source_pin_mismatch:"+std::string(key));
 }
 if(paired&&source.at("content_revision")!=graph.at("content_revision"))fail("source_pin_mismatch:content_revision");
 std::set<std::string> names;std::uint64_t ordinal=0;for(auto const& migration:manifest.at("migrations").as_array()){if(num(migration.at("ordinal"))!=ordinal++)fail("ordered_migration_ordinal");if(!names.insert(str(migration.at("name"))).second)fail("duplicate_migration");}
 names.clear();for(auto const& file:manifest.at("dbc_files").as_array())if(!names.insert(str(file.at("name"))).second)fail("duplicate_dbc_file");sortBy(manifest.at("dbc_files").as_array(),"name");
 if(digest(manifest)!=pins.manifestDigest)fail("manifest_digest_mismatch");
 if(!paired){V generation=O{{"source",source},{"compiler_revision",manifest.at("compiler_revision")},{"migrations",manifest.at("migrations")},{"dbc_files",manifest.at("dbc_files")},{"content_revision",graph.at("content_revision")}};if(digest(generation)!=pins.generation)fail("generation_mismatch");}
 std::vector<ItemSourceOption> options;std::set<std::string> projectedEdges;std::set<std::uint64_t> projectedItems;std::map<std::string,std::string> blocked;std::optional<bool> projectionV2;
 auto availabilityBlock=[&](V const& a)->std::string{if(num(a.at("phase_mask"))!=1||num(a.at("spawn_mask"))!=1)return "mask_context_unavailable";if(!a.at("event_ids").as_array().empty())return "event_context_unavailable";if(!a.at("pool_ids").as_array().empty())return "pool_context_unavailable";auto const& c=a.at("condition_id");if(c.is_null())return {};int state=conditionValue.at(str(c));return state==0?"condition_false":state<0?"condition_unknown":"";};
 for(auto& projection:projections.as_array()){
  auto const version=str(projection.at("schema_version"));bool const isV3=version=="cpi-211-item-source-v3";bool const isV2=version=="cpi-211-item-source-v2";if(!isV2&&!isV3&&version!="cpi-207-item-source-v1")fail("unsupported_source_schema");
  if(isV3!=paired)fail("mixed_generation_schema_versions");
  validate(projection,isV3?"item-source-v3.schema.json":isV2?"item-source-v2.schema.json":"item-source-v1.schema.json");bool const metricV2=isV2||isV3;if(projectionV2&&*projectionV2!=metricV2)fail("mixed_projection_versions");projectionV2=metricV2;
  auto item=num(projection.at("item_id"));if(!item||!projectedItems.insert(item).second)fail("duplicate_or_zero_item_projection");auto const* projectionRevision=projection.as_object().if_contains("content_revision");if((!isV3&&(!projectionRevision||*projectionRevision!=graph.at("content_revision")))||(isV3&&projectionRevision&&*projectionRevision!=graph.at("content_revision"))||str(projection.at("graph_digest"))!=gd)fail("projection_generation_mismatch");
  if(isV3&&generationIdentity(projection.at("generation_identity"))!=*pairedIdentity)fail("projection_generation_identity_mismatch");
  auto target=nodes.find("item:"+std::to_string(item));if(target==nodes.end())fail("missing_item_endpoint");
  for(auto& s:projection.at("sources").as_array()){
   auto id=str(s.at("source_id"));if(!projectedEdges.insert(id).second)fail("duplicate_reverse_entry");auto edgeIt=edges.find(id);if(edgeIt==edges.end())fail("missing_reverse_edge");auto const& edge=*edgeIt->second;
   auto kind=str(s.at("source_kind"));auto const& adapter=adapters.at(kind);
   if(str(s.at("source_ref").at("kind"))!=adapter.node||s.at("source_ref")!=edge.at("from")||str(edge.at("to").at("kind"))!="item"||num(edge.at("to").at("id"))!=item||str(edge.at("kind"))!=adapter.edge||str(edge.at("semantics"))!=adapter.semantics)fail("source_edge_semantics:"+id);
   // Explicit normalized adapter provenance; never represented as a native table/extract proof.
   auto const& prov=edge.at("provenance");if(str(prov.at("source"))!="normalized."+kind||prov.at("natural_key")!=V(A{s.at("source_ref").at("id"),projection.at("item_id")}))fail("source_natural_key:"+id);
   if(s.at("condition_root")!=edge.at("availability").at("condition_id"))fail("condition_identity:"+id);
   auto lo=num(s.at("minimum_phase")),hi=num(s.at("maximum_phase"));if(lo>hi)fail("invalid_phase");
   std::string reason;
   for(V const* a:{&edge.at("availability"),&nodes.at(identity(edge.at("from")))->at("availability"),&target->second->at("availability")}){lo=std::max(lo,num(a->at("minimum_frontier")));hi=std::min(hi,num(a->at("maximum_frontier")));auto r=availabilityBlock(*a);if(reason.empty()&&!r.empty())reason=r;}
   if(lo>hi)fail("empty_phase_intersection");
   if(num(s.at("minimum_count"))==0||num(s.at("minimum_count"))>num(s.at("maximum_count")))fail("unsupported_zero_or_invalid_yield");
   if(num(s.at("minimum_count"))!=1||num(s.at("maximum_count"))!=1)fail("unsupported_nonunit_yield");
   if(num(s.at("chance_numerator"))>num(s.at("chance_denominator")))fail("invalid_probability");
   ItemSourceOption option;option.stableId=id;option.itemId=static_cast<std::uint32_t>(item);option.kind=adapter.kind;option.minimumPhase=static_cast<std::uint32_t>(lo);option.maximumPhase=static_cast<std::uint32_t>(hi);option.requiredGroupSize=static_cast<std::uint32_t>(num(s.at("required_group_size")));option.available=reason.empty();
   if(!reason.empty())blocked.emplace(id,reason);
   option.chanceNumerator=static_cast<std::uint32_t>(num(s.at("chance_numerator")));option.chanceDenominator=static_cast<std::uint32_t>(num(s.at("chance_denominator")));option.moneyCostCopper=num(s.at("money_cost_copper"));option.travelSeconds=static_cast<Score>(num(s.at("travel_seconds")));option.secondsPerAttempt=static_cast<Score>(num(s.at("seconds_per_attempt")));
   if(metricV2){option.riskEvidence=metric_json::decode(s.at("risk_evidence"),id,false);option.contentionEvidence=metric_json::decode(s.at("contention_evidence"),id,true);}
   else {option.riskMilli=static_cast<Score>(num(s.at("risk_milli")));option.contentionMilli=static_cast<Score>(num(s.at("contention_milli")));}
   option.requiredProfessionSkillId=static_cast<std::uint32_t>(num(s.at("required_profession_skill_id")));option.requiredProfessionSkillValue=static_cast<std::uint32_t>(num(s.at("required_profession_skill_value")));
   sortSet(s.at("required_capabilities").as_array());for(auto const& cap:s.at("required_capabilities").as_array())option.requiredCapabilities.push_back(str(cap));if(!s.at("quest_alternative_group").is_null())option.questAlternativeGroup=str(s.at("quest_alternative_group"));
   std::map<std::uint64_t,V const*> reagentEdges;
   auto candidates=reagentsBySource.find(identity(s.at("source_ref")));
   if(candidates!=reagentsBySource.end())for(auto const* candidate:candidates->second){
    if(kind!="craft"||str(candidate->at("semantics"))!="recipe_reagent"||str(candidate->at("to").at("kind"))!="item")fail("invalid_reagent_semantics");
    auto did=num(candidate->at("to").at("id"));if(!reagentEdges.emplace(did,candidate).second)fail("ambiguous_reagent");
   }
   sortSet(s.at("craft_dependencies").as_array());
   if(kind!="craft"&&!s.at("craft_dependencies").as_array().empty())fail("noncraft_dependencies");
   std::set<std::uint64_t> dependencies;for(auto const& dep:s.at("craft_dependencies").as_array()){
    auto did=num(dep.at("item_id"));if(!dependencies.insert(did).second)fail("duplicate_craft_dependency");
    if(!nodes.contains("item:"+std::to_string(did)))fail("missing_craft_item");
    auto found=reagentEdges.find(did);if(found==reagentEdges.end())fail("missing_recipe_reagent_edge");auto const& reagent=*found->second;
    auto const* count=reagent.at("attributes").as_object().if_contains("count");if(!count||num(*count)!=num(dep.at("count")))fail("recipe_reagent_count");
    auto const& rp=reagent.at("provenance");if(str(rp.at("source"))!="normalized.recipe_reagent"||rp.at("natural_key")!=V(A{s.at("source_ref").at("id"),dep.at("item_id")}))fail("recipe_reagent_natural_key");
    auto depLo=static_cast<std::uint64_t>(option.minimumPhase),depHi=static_cast<std::uint64_t>(option.maximumPhase);bool depAvailable=dep.at("available").as_bool();
    for(V const* a:{&reagent.at("availability"),&nodes.at("item:"+std::to_string(did))->at("availability")}){
     depLo=std::max(depLo,num(a->at("minimum_frontier")));depHi=std::min(depHi,num(a->at("maximum_frontier")));auto r=availabilityBlock(*a);if(!r.empty()){depAvailable=false;blocked.emplace(id,"craft_dependency:"+r);}
    }
    if(depLo<=depHi){option.minimumPhase=static_cast<std::uint32_t>(depLo);option.maximumPhase=static_cast<std::uint32_t>(depHi);}
    else {option.phaseAuthorized=false;blocked.emplace(id,"craft_dependency:empty_phase_intersection");}
    option.craftDependencies.push_back({static_cast<std::uint32_t>(did),static_cast<std::uint32_t>(num(dep.at("count"))),depAvailable,dep.at("phase_authorized").as_bool()&&depLo<=depHi,true});
   }
   if(dependencies.size()!=reagentEdges.size())fail("omitted_recipe_reagent");
   std::sort(option.craftDependencies.begin(),option.craftDependencies.end(),[](auto const& l,auto const& r){return l.itemId<r.itemId;});options.push_back(std::move(option));
  }sortBy(projection.at("sources").as_array(),"source_id");
 }
 std::sort(projections.as_array().begin(),projections.as_array().end(),[](V const& l,V const& r){return num(l.at("item_id"))<num(r.at("item_id"));});
 // A cyclic reagent contract cannot establish earned acquisition, even offline.
 std::map<std::uint32_t,std::set<std::uint32_t>> reagentGraph;
 for(auto const& option:options)if(option.kind==ItemSourceKind::Craft)for(auto const& dep:option.craftDependencies)reagentGraph[option.itemId].insert(dep.itemId);
 std::map<std::uint32_t,unsigned> reagentState;
 std::function<void(std::uint32_t,unsigned)> visitReagent=[&](std::uint32_t item,unsigned depth){
  if(depth>128)fail("recipe_dependency_depth");
  if(reagentState[item]==1)fail("recipe_dependency_cycle");
  if(reagentState[item]==2)return;
  reagentState[item]=1;for(auto next:reagentGraph[item])visitReagent(next,depth+1);reagentState[item]=2;
 };
 for(auto const& option:options)visitReagent(option.itemId,0);
 if(digest(projections)!=pins.projectionDigest)fail("projection_digest_mismatch");
 auto contentRevision=graph.at("content_revision").is_null()?std::string{}:str(graph.at("content_revision"));
 auto coreRevision=graph.at("core_revision").is_null()?std::string{}:str(graph.at("core_revision"));
 auto playerbotsRevision=graph.at("playerbots_revision").is_null()?std::string{}:str(graph.at("playerbots_revision"));
 return LoadedItemSources(ItemSourceIndex(std::move(contentRevision),gd,std::move(options),domain==ProjectionDomain::EffectiveRealm,projectionV2.value_or(false),pairedIdentity,pairedRevisions),pins,domain,std::move(blocked),std::move(coreRevision),std::move(playerbotsRevision),std::move(pairedIdentity));
}
CanonicalGearPlanResult CanonicalGearPlanningService::PlanOffline(LoadedItemSources const& loaded,BotProgressSnapshot const& snapshot,CharacterResourceSnapshot const& resources,std::vector<ItemUpgradeTarget> const& targets,GearScoringPolicy policy){
 if(loaded.Domain()!=ProjectionDomain::OfflineContract)fail("offline_domain_required");
 EarnedGearPlanner planner(std::move(policy));
 auto decorate=[&](GearPlan plan){if(loaded.generationIdentity_){plan.schemaVersion="cpi-211-gear-plan-v3";plan.generationIdentity=loaded.generationIdentity_;plan.contentRevision=loaded.Index().ContentRevision();}return plan;};
 auto revisionsMatch=[&](){if(!loaded.generationIdentity_)return snapshot.coreRevision==loaded.coreRevision_&&snapshot.playerbotsRevision==loaded.playerbotsRevision_;auto const& revisions=*loaded.Index().GenerationRevisions();return (revisions.core?snapshot.coreRevision==*revisions.core:snapshot.coreRevision.empty())&&(revisions.playerbots?snapshot.playerbotsRevision==*revisions.playerbots:snapshot.playerbotsRevision.empty());};
 if(!revisionsMatch()){
  GearPlan rejected=decorate(planner.MakePlanMetadata(snapshot));rejected.errors.push_back("source_revision_mismatch");return {ProjectionDomain::OfflineContract,loaded.Pins(),loaded.BlockedReasons(),std::move(rejected)};
 }
 return {ProjectionDomain::OfflineContract,loaded.Pins(),loaded.BlockedReasons(),decorate(planner.Evaluate(snapshot,resources,loaded.Index(),targets))};
}
CanonicalGearPlanResult CanonicalGearPlanningService::PlanEffectiveRealm(LoadedItemSources const& loaded,BotProgressSnapshot const& snapshot,CharacterResourceSnapshot const& resources,std::vector<ItemUpgradeTarget> const& targets,GearScoringPolicy policy){
 if(loaded.Domain()!=ProjectionDomain::EffectiveRealm||!loaded.Index().IsVerifiedCanonicalProjection())fail("effective_realm_required");
 EarnedGearPlanner planner(std::move(policy));
 auto decorate=[&](GearPlan plan){if(loaded.generationIdentity_){plan.schemaVersion="cpi-211-gear-plan-v3";plan.generationIdentity=loaded.generationIdentity_;plan.contentRevision=loaded.Index().ContentRevision();}else if(loaded.Index().IsVersion2Projection())plan.schemaVersion="cpi-211-gear-plan-v2";return plan;};
 auto revisionsMatch=[&](){if(!loaded.generationIdentity_)return snapshot.coreRevision==loaded.coreRevision_&&snapshot.playerbotsRevision==loaded.playerbotsRevision_;auto const& revisions=*loaded.Index().GenerationRevisions();return (revisions.core?snapshot.coreRevision==*revisions.core:snapshot.coreRevision.empty())&&(revisions.playerbots?snapshot.playerbotsRevision==*revisions.playerbots:snapshot.playerbotsRevision.empty());};
 if(!revisionsMatch()){GearPlan rejected=decorate(planner.MakePlanMetadata(snapshot));rejected.errors.push_back("source_revision_mismatch");return {ProjectionDomain::EffectiveRealm,loaded.Pins(),loaded.BlockedReasons(),std::move(rejected)};}
 constexpr std::size_t MaxEvaluationWork=16384;std::size_t work=0;
 for(auto const& target:targets){auto count=loaded.Index().SourcesForItem(target.itemId).size();if(count>MaxEvaluationWork-work)fail("evaluation_work_bound");work+=count;}
 return {ProjectionDomain::EffectiveRealm,loaded.Pins(),loaded.BlockedReasons(),decorate(planner.Plan(snapshot,resources,loaded.Index(),targets))};
}
}
