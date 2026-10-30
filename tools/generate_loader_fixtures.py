#!/usr/bin/env python3
"""Deterministic offline contracts; never a frozen realm extraction or accepted binding."""
import copy,hashlib,json
from pathlib import Path
from compile_content_graph import compile_graph
out=Path(__file__).resolve().parents[1]/'fixtures/canonical-item-sources';out.mkdir(exist_ok=True)
def canon(v):return json.dumps(v,ensure_ascii=False,sort_keys=True,separators=(',',':'))
def digest(v):return hashlib.sha256(canon(v).encode()).hexdigest()
av=dict(minimum_frontier=1,maximum_frontier=80,phase_mask=1,spawn_mask=1,event_ids=[],pool_ids=[],condition_id=None)
kinds={'quest':'quests','item':'items','creature':'creatures','gameobject':'gameobjects','spell':'spells','trainer':'trainers','vendor':'vendors','taxi_node':'taxi_nodes','area':'areas','map':'maps','dungeon':'dungeons','loot_template':'loot_templates','recipe':'recipes'}
adapters=[('quest_reward','quest','quest_reward'),('creature_loot','creature','loot_source'),('gameobject_loot','gameobject','loot_source'),('vendor','vendor','vendor_source'),('trainer_recipe','trainer','trainer_spell'),('craft','recipe','recipe_product'),('pvp_reward','vendor','vendor_source'),('dungeon_quest','quest','quest_reward'),('dungeon_drop','creature','loot_source'),('disenchant','item','loot_source'),('prospecting','item','loot_source'),('milling','item','loot_source'),('fishing','loot_template','loot_source'),('skinning','creature','loot_source')]
g=dict(schema_version='cpi-207-content-graph-v1',core_revision='core-offline',playerbots_revision='pb-offline',progression_revision='progression-offline',content_revision='content-offline',world_dump_digest='1'*64,dbc_set_digest='2'*64,availability_model='realm_frontier+phase_mask+spawn_mask+event_pool+condition_ast',nodes={v:[] for v in kinds.values()},condition_roots=[],edges=[])
p=[]
for i,(kind,node,edge) in enumerate(adapters):
 item=100+i;ref=200+i
 for nk,nid in [('item',item),(node,ref)]:g['nodes'][kinds[nk]].append(dict(id=nid,availability=copy.deepcopy(av),attributes={}))
 g['edges'].append(dict(edge_id=f'source:{i:02}',kind=edge,from_={},to={},semantics=kind,availability=copy.deepcopy(av),attributes={},provenance=dict(source='normalized.'+kind,natural_key=[ref,item])))
 e=g['edges'][-1];e['from']=dict(kind=node,id=ref);del e['from_'];e['to']=dict(kind='item',id=item)
 p.append(dict(schema_version='cpi-207-item-source-v1',content_revision='content-offline',graph_digest='',item_id=item,sources=[dict(source_id=f'source:{i:02}',source_kind=kind,source_ref=dict(kind=node,id=ref),minimum_count=1,maximum_count=1,chance_numerator=1,chance_denominator=1,minimum_phase=1,maximum_phase=80,required_group_size=1,money_cost_copper=0,travel_seconds=1,seconds_per_attempt=1,risk_milli=0,contention_milli=0,required_capabilities=[],required_profession_skill_id=0,required_profession_skill_value=0,quest_alternative_group=None,craft_dependencies=[],condition_root=None,earned=True)]))
m=dict(schema_version='cpi-207-content-manifest-v1',compiler_revision='compiler-offline',started_at='2026-10-18T00:00:00Z',completed_at='2026-10-18T00:00:01Z',source=dict(candidate_id='candidate:offline',**{k:g[k] for k in ['core_revision','playerbots_revision','progression_revision','world_dump_digest','dbc_set_digest']},config_digest='3'*64),migrations=[dict(ordinal=0,name='a.sql',digest='4'*64),dict(ordinal=1,name='b.sql',digest='5'*64)],dbc_files=[dict(name='a.dbc',digest='6'*64),dict(name='b.dbc',digest='7'*64)],row_counts={},warning_counts={},graph_digest='',reproducible_peer_digest='')
def pins(graph,manifest,projections):
 try: graph,gd=compile_graph(copy.deepcopy(graph))
 except ValueError:gd=digest(graph)
 manifest['graph_digest']=manifest['reproducible_peer_digest']=gd
 for x in projections:x['graph_digest']=gd
 normalized=copy.deepcopy(projections)
 for x in normalized:
  x['sources'].sort(key=lambda x:x['source_id'])
  for s in x['sources']:
   s['required_capabilities'].sort(key=canon);s['craft_dependencies'].sort(key=canon)
 normalized.sort(key=lambda x:x['item_id']);manifest['dbc_files'].sort(key=lambda x:x['name'])
 generation={k:manifest[k] for k in ['source','compiler_revision','migrations','dbc_files']};generation['content_revision']=graph['content_revision']
 return dict(candidateId='candidate:offline',generation=digest(generation),graphDigest=gd,manifestDigest=digest(manifest),projectionDigest=digest(normalized))
def craft(g,m,p):
 p[5]['sources'][0]['craft_dependencies']=[dict(item_id=100,count=1,available=True,phase_authorized=True,earned=True)]
 g['edges'][:]=[e for e in g['edges'] if e['edge_id']!='reagent:one']
 g['edges'].append(dict(edge_id='reagent:one',kind='recipe_reagent',**{'from':dict(kind='recipe',id=205),'to':dict(kind='item',id=100)},semantics='recipe_reagent',availability=copy.deepcopy(av),attributes=dict(count=1),provenance=dict(source='normalized.recipe_reagent',natural_key=[205,100])))
p[5]['sources'][0]['required_profession_skill_id']=164
p[5]['sources'][0]['required_profession_skill_value']=1
craft(g,m,p)
cases=[]
def case(name,mut=None,expected='',block='',raw=None):
 graph,manifest,projections=copy.deepcopy((g,m,p))
 if mut:mut(graph,manifest,projections)
 pin=pins(graph,manifest,projections)
 if raw:raw(graph,manifest,projections,pin)
 cases.append(dict(name=name,graph=graph,manifest=manifest,projections=projections,pins=pin,error=expected,block=block))
case('all-kinds')
case('reordered',lambda g,m,p:([a.reverse() for a in g['nodes'].values()],g['edges'].reverse(),p.reverse(),m['dbc_files'].reverse()))
case('wrong-graph-pin',raw=lambda g,m,p,pin:pin.update(graphDigest='0'*64),expected='graph_digest_mismatch')
case('changed-bytes',raw=lambda g,m,p,pin:g.update(content_revision='changed'),expected='graph_digest_mismatch')
case('wrong-generation',raw=lambda g,m,p,pin:pin.update(generation='0'*64),expected='generation_mismatch')
case('wrong-manifest',raw=lambda g,m,p,pin:pin.update(manifestDigest='0'*64),expected='manifest_digest_mismatch')
case('wrong-projection',raw=lambda g,m,p,pin:pin.update(projectionDigest='0'*64),expected='projection_digest_mismatch')
case('wrong-candidate',raw=lambda g,m,p,pin:pin.update(candidateId='candidate:forged'),expected='candidate_mismatch')
case('source-pin',lambda g,m,p:m['source'].update(core_revision='wrong'),expected='source_pin_mismatch')
case('provenance',lambda g,m,p:g['edges'][0]['provenance'].update(natural_key=[999,100]),expected='source_natural_key')
case('missing-endpoint',lambda g,m,p:g['edges'][0]['from'].update(id=999),expected='missing_endpoint')
case('wrong-endpoint-kind',lambda g,m,p:p[0]['sources'][0]['source_ref'].update(kind='item'),expected='source_edge_semantics')
case('wrong-semantics',lambda g,m,p:g['edges'][0].update(semantics='invented'),expected='source_edge_semantics')
case('duplicate-edge',lambda g,m,p:g['edges'].append(copy.deepcopy(g['edges'][0])),expected='duplicate_edge')
case('ambiguous-reverse',lambda g,m,p:g['edges'].append(dict(copy.deepcopy(g['edges'][0]),edge_id='source:other')),expected='ambiguous_reverse_identity')
case('duplicate-projection',lambda g,m,p:p.append(copy.deepcopy(p[0])),expected='duplicate_or_zero_item_projection')
case('missing-condition',lambda g,m,p:g['edges'][0]['availability'].update(condition_id='condition:missing'),expected='missing_condition')
case('condition-cycle',lambda g,m,p:g['condition_roots'].append(dict(condition_id='condition:a',expression=dict(op='reference',condition_id='condition:a'))),expected='condition_cycle')
case('invalid-phase',lambda g,m,p:p[0]['sources'][0].update(minimum_phase=9,maximum_phase=2),expected='invalid_phase')
case('empty-phase',lambda g,m,p:p[0]['sources'][0].update(minimum_phase=81,maximum_phase=90),expected='empty_phase_intersection')
case('unearned',lambda g,m,p:p[0]['sources'][0].update(earned=False),expected='schema_const')
case('unearned-dependency',lambda g,m,p:p[5]['sources'][0].update(craft_dependencies=[dict(item_id=100,count=1,available=True,phase_authorized=True,earned=False)]),expected='schema_const')
case('craft-dependency',craft)
case('missing-reagent',lambda g,m,p:g['edges'].pop(),expected='missing_recipe_reagent_edge')
case('wrong-reagent-count',lambda g,m,p:(craft(g,m,p),g['edges'][-1]['attributes'].update(count=2)),expected='recipe_reagent_count')
case('omitted-reagent',lambda g,m,p:(craft(g,m,p),p[5]['sources'][0].update(craft_dependencies=[])),expected='omitted_recipe_reagent')
case('blocked-reagent',lambda g,m,p:(craft(g,m,p),g['edges'][-1]['availability'].update(event_ids=[1])))
case('unknown-kind',lambda g,m,p:p[0]['sources'][0].update(source_kind='invented'),expected='schema_enum')
case('forged-verified',lambda g,m,p:p[0].update(verified=True),expected='schema_extra')
case('forged-domain',lambda g,m,p:p[0].update(domain='EffectiveRealm'),expected='schema_extra')
case('migration-reordered',lambda g,m,p:m['migrations'].reverse(),expected='ordered_migration_ordinal')
case('invalid-probability',lambda g,m,p:p[0]['sources'][0].update(chance_numerator=2),expected='invalid_probability')
case('zero-yield',lambda g,m,p:p[0]['sources'][0].update(minimum_count=0),expected='unsupported_zero')
for op in ['false','indeterminate','true','leaf']:
 def mutate(g,m,p,op=op):
  expression=dict(op=op)
  if op=='leaf':expression.update(condition_type=1,values=[1,0,0])
  g['condition_roots'].append(dict(condition_id='condition:a',expression=expression));g['edges'][0]['availability']['condition_id']='condition:a';p[0]['sources'][0]['condition_root']='condition:a'
 case('condition-'+op,mutate,block={'false':'condition_false','indeterminate':'condition_unknown','leaf':'condition_unknown','true':''}[op])
for field,val,reason in [('event_ids',[1],'event_context_unavailable'),('pool_ids',[1],'pool_context_unavailable'),('phase_mask',2,'mask_context_unavailable'),('spawn_mask',0,'mask_context_unavailable')]:
 case(field,lambda g,m,p,field=field,val=val:g['edges'][0]['availability'].update({field:val}),block=reason)
case('reagent-future-frontier',lambda g,m,p:g['edges'][-1]['availability'].update(minimum_frontier=50))
case('reagent-empty-frontier',lambda g,m,p:(g['edges'][-1]['availability'].update(minimum_frontier=50),g['nodes']['items'][0]['availability'].update(maximum_frontier=20)))
case('recipe-cycle',lambda g,m,p:(p[5]['sources'][0]['craft_dependencies'][0].update(item_id=105),g['edges'][-1]['to'].update(id=105),g['edges'][-1]['provenance'].update(natural_key=[205,105])),expected='recipe_dependency_cycle')
case('invalid-calendar',lambda g,m,p:m.update(started_at='2026-02-30T00:00:00Z'),expected='schema_datetime_calendar')
case('invalid-clock',lambda g,m,p:m.update(started_at='2026-10-18T24:00:00Z'),expected='schema_datetime_calendar')
case('invalid-offset',lambda g,m,p:m.update(started_at='2026-10-18T00:00:00+25:00'),expected='schema_datetime_offset')
case('unicode-attributes',lambda g,m,p:g['nodes']['items'][0]['attributes'].update({'é':'雪','a':'é','control':'\n'}))
def combinators(g,m,p):
 g['condition_roots']=[dict(condition_id='condition:a',expression=dict(op='all',arguments=[dict(op='not',argument=dict(op='false')),dict(op='any',arguments=[dict(op='indeterminate'),dict(op='reference',condition_id='condition:b')])])),dict(condition_id='condition:b',expression=dict(op='true'))]
 g['edges'][0]['availability']['condition_id']='condition:a';p[0]['sources'][0]['condition_root']='condition:a'
case('condition-combinators',combinators)
case('condition-combinators-reordered',lambda g,m,p:(combinators(g,m,p),g['condition_roots'].reverse(),g['condition_roots'][1]['expression']['arguments'].reverse()))
case('nonunit-yield',lambda g,m,p:p[0]['sources'][0].update(minimum_count=2,maximum_count=3),expected='unsupported_nonunit_yield')
case('multibyte-revision-160',lambda g,m,p:m.update(compiler_revision='é'*160))
case('multibyte-revision-161',lambda g,m,p:m.update(compiler_revision='é'*161),expected='schema_string_bound')
def two_blocked_reagents(g,m,p):
 craft(g,m,p)
 g['edges'][-1]['availability']['event_ids']=[1]
 p[5]['sources'][0]['craft_dependencies'].append(dict(item_id=101,count=1,available=True,phase_authorized=True,earned=True))
 g['edges'].append(dict(edge_id='reagent:two',kind='recipe_reagent',**{'from':dict(kind='recipe',id=205),'to':dict(kind='item',id=101)},semantics='recipe_reagent',availability=dict(copy.deepcopy(av),pool_ids=[1]),attributes=dict(count=1),provenance=dict(source='normalized.recipe_reagent',natural_key=[205,101])))
case('two-blocked-reagents',two_blocked_reagents)
case('two-blocked-reagents-reordered',lambda g,m,p:(two_blocked_reagents(g,m,p),p[5]['sources'][0]['craft_dependencies'].reverse(),g['edges'].reverse()))
case('vendor-price',lambda g,m,p:p[3]['sources'][0].update(money_cost_copper=250))
(out/'cases.json').write_text(json.dumps(cases,ensure_ascii=False,indent=2)+'\n')
