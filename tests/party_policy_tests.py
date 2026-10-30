from pathlib import Path
import sys,unittest
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).parents[1]/'runtime'))
from party_policy import ManagedPairPolicy,PartyQuestStrategy,ProtocolError
A='a'*64;B='b'*64
class Native:
 def __init__(self):self.calls=[];self.status='pending';self.reason='native_invitation_pending';self.group=0;self.mismatch=False;self.control=True
 def strategic_read_quest_snapshot(self,key,quest):
  self.calls.append(('read',key));return dict(identity_key=key,account_id=2 if key==A else 4,character_guid=11 if key==A else 12,online=True,control_allowed=self.control,quests=[dict(quest_id=7,status='incomplete',eligible=True)])
 def strategic_read_party_snapshot(self,a,b,q):
  self.calls.append(('party_read',));return self._result(a,b,q)
 def strategic_request_party(self,a,b,q):
  self.calls.append(('request',a,b,q));return self._result(a,b,q)
 def _result(self,a,b,q):
  return dict(status=self.status,reason=self.reason,leader_key=a,member_key=b,quest_id=q,leader_account_id=2,leader_guid=99 if self.mismatch else 11,member_account_id=4,member_guid=12,group_guid=self.group)
class Tests(unittest.TestCase):
 def setUp(self):self.n=Native();self.audit=[];self.p=ManagedPairPolicy(self.n,self.audit.append,A,B);self.bindings={A:(2,11),B:(4,12)}
 def test_no_load_calls_and_cap_one_does_not_create_pair(self):
  self.assertEqual(self.n.calls,[]);self.p.tick({A:(2,11)});self.assertEqual(self.n.calls,[])
 def test_shared_quest_pending_is_not_success_and_renew_no_duplicate_reads(self):
  self.p.tick(self.bindings);self.assertEqual(self.audit[-1]['status'],'pending');self.p.tick(self.bindings);self.assertEqual(len(self.audit),1);self.assertEqual(sum(c[0]=='read' for c in self.n.calls),2)
  self.n.status='accepted';self.n.reason='native_group_confirmed';self.n.group=77;self.p.tick(self.bindings);self.assertEqual(self.audit[-1]['status'],'accepted')
 def test_combat_defers_and_exact_pair_renewal_continues(self):
  self.p.tick(self.bindings);self.n.status='deferred';self.n.reason='combat';self.n.group=77;self.p.tick(self.bindings);self.assertEqual(self.audit[-1]['reason'],'combat');self.assertEqual(sum(c[0]=='request' for c in self.n.calls),2)
 def test_binding_and_false_group_confirmation_rejected(self):
  self.n.mismatch=True
  with self.assertRaises(ProtocolError):self.p.tick(self.bindings)
  self.n.mismatch=False;self.n.status='accepted'
  with self.assertRaises(ProtocolError):self.p.tick(self.bindings)
 def test_unowned_no_eligible_goal_never_invites(self):
  self.n.control=False;self.p.tick(self.bindings);self.assertFalse(any(c[0]=='request' for c in self.n.calls))
 def test_online_deselected_no_new_pair_generic_quest_inputs_unchanged(self):
  seen=[]
  class Quests:
   def tick(_,bindings):seen.append(bindings)
  state=SimpleNamespace(pending_admission_key=None,desired_session_keys={A},progress_cache={k:SimpleNamespace(online=True,session_requested=True) for k in self.bindings})
  wrapper=PartyQuestStrategy(self.p,Quests(),SimpleNamespace(state=state));wrapper.tick(self.bindings)
  self.assertEqual(self.n.calls,[]);self.assertEqual(seen,[self.bindings]);state.desired_session_keys.add(B);wrapper.tick(self.bindings);self.assertTrue(any(c[0]=='request' for c in self.n.calls))
 def test_deselected_pending_not_refreshed_confirmed_combat_pair_can_renew(self):
  self.p.tick(self.bindings);before=sum(c[0]=='request' for c in self.n.calls);self.p.tick(self.bindings,allow_new=False);self.assertEqual(sum(c[0]=='request' for c in self.n.calls),before)
  self.n.status='deferred';self.n.reason='combat';self.n.group=77;self.p.tick(self.bindings,allow_new=False);self.assertEqual(sum(c[0]=='request' for c in self.n.calls),before+1)
 def test_pending_admission_blocks_new_pair(self):
  class Quests:
   def tick(_,bindings):pass
  state=SimpleNamespace(pending_admission_key='x',desired_session_keys={A,B},progress_cache={k:SimpleNamespace(online=True,session_requested=True) for k in self.bindings})
  PartyQuestStrategy(self.p,Quests(),SimpleNamespace(state=state)).tick(self.bindings);self.assertEqual(self.n.calls,[])
if __name__=='__main__':unittest.main()
