"""One explicit outdoor pair, using native invite/accept and ordinary follow AI.

The existing population controller must have selected both sessions. This policy
never requests sessions, raises capacity, accepts quests, or changes membership.
"""
from quest_policy import ProtocolError, _integer


class ManagedPairPolicy:
    def __init__(self, native, audit, leader_key, member_key):
        if not all(isinstance(k, str) and len(k) == 64 and all(c in '0123456789abcdef' for c in k)
                   for k in (leader_key, member_key)) or leader_key == member_key:
            raise ProtocolError('invalid managed pair keys')
        for port in ('strategic_read_quest_snapshot', 'strategic_read_party_snapshot', 'strategic_request_party'):
            if not callable(getattr(native, port, None)):
                raise ProtocolError('missing native party port: ' + port)
        self.native, self.audit = native, audit
        self.leader, self.member = leader_key, member_key
        self.quest = 0
        self.last_event = None

    def _audit(self, status, reason):
        event = (status, reason, self.quest)
        if event != self.last_event:
            self.audit(dict(event='managed_pair', status=status, reason=reason,
                            leader_key=self.leader, member_key=self.member, quest_id=self.quest))
            self.last_event = event

    def _quests(self, key, binding):
        row = self.native.strategic_read_quest_snapshot(key, 0)
        if not isinstance(row, dict) or row.get('identity_key') != key or (
                row.get('account_id'), row.get('character_guid')) != binding:
            raise ProtocolError('party quest snapshot binding mismatch')
        if row.get('online') is not True or row.get('control_allowed') is not True:
            return set()
        quests = row.get('quests')
        if not isinstance(quests, list) or len(quests) > 25:
            raise ProtocolError('invalid party quest log')
        result = set()
        for quest in quests:
            if not isinstance(quest, dict):
                raise ProtocolError('invalid party quest row')
            qid = _integer(quest.get('quest_id'), 'quest_id', 1)
            if quest.get('eligible') is True and quest.get('status') in ('incomplete', 'complete'):
                result.add(qid)
        return result

    def _result(self, row, bindings):
        if not isinstance(row, dict):
            raise ProtocolError('invalid native party result')
        expected = dict(leader_key=self.leader, member_key=self.member, quest_id=self.quest,
                        leader_account_id=bindings[self.leader][0], leader_guid=bindings[self.leader][1],
                        member_account_id=bindings[self.member][0], member_guid=bindings[self.member][1])
        for field, value in expected.items():
            if row.get(field) != value or type(row.get(field)) is not type(value):
                raise ProtocolError('native party result binding mismatch: ' + field)
        status, reason = row.get('status'), row.get('reason')
        if status not in {'ready', 'pending', 'accepted', 'deferred', 'rejected'} or not isinstance(reason, str) or not reason:
            raise ProtocolError('invalid native party status')
        group = _integer(row.get('group_guid'), 'group_guid')
        if (status == 'accepted' and not group) or (status in {'ready', 'pending'} and group):
            raise ProtocolError('native party confirmation mismatch')
        return status, reason

    def tick(self, bindings, *, allow_new=True):
        if self.leader not in bindings or self.member not in bindings:
            self._audit('deferred', 'both_sessions_required')
            return
        for key in (self.leader, self.member):
            binding = bindings[key]
            if not isinstance(binding, tuple) or len(binding) != 2:
                raise ProtocolError('invalid party binding')
            for value in binding:
                _integer(value, 'party binding', 1)
        if not self.quest and not allow_new:
            self._audit('deferred', 'pair_not_selected')
            return
        if not self.quest:
            shared = self._quests(self.leader, bindings[self.leader]) & self._quests(self.member, bindings[self.member])
            if not shared:
                self._audit('deferred', 'no_shared_accepted_quest')
                return
            self.quest = min(shared)
        if not allow_new:
            current = self.native.strategic_read_party_snapshot(self.leader, self.member, self.quest)
            status, reason = self._result(current, bindings)
            if status != 'accepted' and not (status == 'deferred' and reason == 'combat' and current['group_guid']):
                self._audit('deferred', 'pair_not_selected')
                return
        # This also renews a confirmed pair during combat, while the native
        # response stays deferred and native goal execution remains blocked.
        row = self.native.strategic_request_party(self.leader, self.member, self.quest)
        status, reason = self._result(row, bindings)
        self._audit(status, reason)


class PartyQuestStrategy:
    """Plug into existing cadence after successful population publication."""
    def __init__(self, party, quests, population):
        self.party, self.quests, self.population = party, quests, population

    def tick(self, bindings):
        state = self.population.state
        selected = not state.pending_admission_key and all(
            key in state.desired_session_keys and key in state.progress_cache and
            state.progress_cache[key].online and state.progress_cache[key].session_requested
            for key in (self.party.leader, self.party.member))
        self.party.tick(bindings, allow_new=selected)
        self.quests.tick(bindings)
