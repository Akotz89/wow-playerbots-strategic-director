"""Native-protocol regression for the bounded accepted-quest integration."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("quest_policy", Path(__file__).parents[1] / "runtime/quest_policy.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Native:
    def __init__(self):
        self.current = 0
        self.observed = "incomplete"
        self.control = True
        self.online = True
        self.response = "accepted"
        self.calls = []
        self.rows = [dict(quest_id=7, status="incomplete", eligible=True, rejection_reason=""),
                     dict(quest_id=783, status="complete", eligible=True, rejection_reason="")]

    def strategic_read_quest_snapshot(self, key, observed):
        self.calls.append(("read", key, observed))
        return dict(identity_key=key, account_id=2, character_guid=11, online=self.online,
                    control_allowed=self.control, reason="human_control" if not self.control else "",
                    current_goal_quest_id=self.current, observed_quest_id=observed,
                    observed_quest_status=self.observed if observed else "none", quests=self.rows)

    def strategic_set_quest_goal(self, key, quest):
        self.calls.append(("set", key, quest))
        if self.response == "accepted":
            self.current = quest
        return dict(status=self.response, reason="native", quest_id=quest)


class QuestPolicyTests(unittest.TestCase):
    def setUp(self):
        self.native = Native()
        self.audit = []
        self.now = 100
        self.policy = module.QuestPolicy(self.native, self.audit.append, lambda: self.now, max_per_tick=2)
        self.bindings = {"a": (2, 11)}

    def test_select_turnin_then_preserve_and_reward_only_from_history(self):
        self.policy.tick(self.bindings)
        self.assertEqual(self.native.current, 783)
        self.assertEqual(self.audit[-1]["event"], "selected")
        self.native.rows.reverse()
        self.policy.tick(self.bindings)
        self.assertEqual(self.audit[-1]["event"], "renewed")
        self.assertFalse(any(x["event"] == "rewarded" for x in self.audit))
        self.native.observed = "rewarded"
        self.native.current = 0
        self.native.rows = [q for q in self.native.rows if q["quest_id"] == 7]
        self.policy.tick(self.bindings)
        self.assertTrue(any(x["event"] == "rewarded" for x in self.audit))
        self.assertEqual(self.native.current, 7)

    def test_human_control_and_held_eligibility_never_submit(self):
        self.native.control = False
        self.policy.tick(self.bindings)
        self.assertEqual(len(self.native.calls), 1)
        self.native.control = True
        for row in self.native.rows:
            row["eligible"] = False
        self.policy.tick(self.bindings)
        self.assertTrue(all(x[0] == "read" for x in self.native.calls))

    def test_lost_or_failed_is_not_success_and_waits_before_retry(self):
        self.native.rows = self.native.rows[:1]
        self.policy.tick(self.bindings)
        self.native.current = 0
        self.native.observed = "none"
        self.policy.tick(self.bindings)
        self.assertEqual(len([x for x in self.native.calls if x[0] == "set"]), 1)
        self.assertFalse(any(x["event"] == "rewarded" for x in self.audit))
        self.now += 31
        self.policy.tick(self.bindings)
        self.assertEqual(len([x for x in self.native.calls if x[0] == "set"]), 2)

    def test_rejection_backoff_and_binding_mismatch(self):
        self.native.response = "rejected"
        self.native.rows = self.native.rows[:1]
        self.policy.tick(self.bindings)
        self.policy.tick(self.bindings)
        self.assertEqual(len([x for x in self.native.calls if x[0] == "set"]), 1)
        with self.assertRaises(module.ProtocolError):
            self.policy.tick({"a": (2, 99)})

    def test_cached_online_then_native_offline_keeps_binding_and_defers(self):
        self.native.online = False
        self.policy.tick(self.bindings)
        self.assertEqual(self.audit[-1]["event"], "deferred")
        self.assertTrue(all(x[0] == "read" for x in self.native.calls))
        # An offline observation is not permission to silently change identity.
        with self.assertRaises(module.ProtocolError):
            self.policy.tick({"a": (3, 11)})

    def test_paused_population_true_does_not_schedule_strategy(self):
        from types import SimpleNamespace
        calls = []
        state = SimpleNamespace(revision=4, admitted_identity_keys={"a"},
                                bindings={"a": SimpleNamespace(account_id=2, character_guid=11)},
                                progress_cache={"a": SimpleNamespace(online=True),
                                                "unadmitted": SimpleNamespace(online=True)})
        population = SimpleNamespace(state=state, tick=lambda diff: True)
        strategy = SimpleNamespace(tick=lambda bindings: calls.append(bindings))
        self.assertTrue(module.population_then_strategy(population, strategy, 1000))
        self.assertEqual(calls, [])
        def healthy(diff):
            state.revision += 1
            return True
        population.tick = healthy
        self.assertTrue(module.population_then_strategy(population, strategy, 1000))
        self.assertEqual(calls, [{"a": (2, 11)}])
        def failed(diff):
            state.revision += 1
            return False
        population.tick = failed
        self.assertFalse(module.population_then_strategy(population, strategy, 1000))
        self.assertEqual(len(calls), 1)

    def test_round_robin_bound_and_invalid_rows(self):
        self.policy.tick({x: (2, 11) for x in "abcde"})
        self.policy.tick({x: (2, 11) for x in "abcde"})
        self.assertEqual([x[1] for x in self.native.calls if x[0] == "read"], list("abcd"))
        self.native.rows.append(self.native.rows[0])
        with self.assertRaises(module.ProtocolError):
            self.policy.tick(self.bindings)

    def test_composition_cadence_boundaries_and_paused_revision(self):
        from types import SimpleNamespace
        now = [10.0]
        calls = []
        state = SimpleNamespace(revision=0, progress_cache={}, admitted_identity_keys=set())
        def tick(diff):
            calls.append(("population", diff))
            state.revision += 1
            return True
        population = SimpleNamespace(state=state, tick=tick)
        strategy = SimpleNamespace(tick=lambda bindings: calls.append(("strategy", bindings)))
        composed = module.CadencedPopulationStrategy(population, strategy, lambda: now[0])
        self.assertTrue(composed.tick(50))
        self.assertEqual(len(calls), 2)
        now[0] = 10.999
        self.assertTrue(composed.tick(50))
        self.assertEqual((len(calls), state.revision), (2, 1))
        now[0] = 11.0
        self.assertTrue(composed.tick(50))
        self.assertEqual((len(calls), state.revision), (4, 2))
        population.tick = lambda diff: calls.append(("paused", diff)) or True
        now[0] = 12.0
        self.assertTrue(composed.tick(50))
        self.assertEqual((len(calls), state.revision), (5, 2))

    def test_cadence_does_not_resurrect_false_or_swallow_exception(self):
        from types import SimpleNamespace
        for raises in (False, True):
            calls = []
            def tick(diff):
                calls.append(diff)
                if raises:
                    raise RuntimeError("native failure")
                return False
            population = SimpleNamespace(state=SimpleNamespace(revision=0), tick=tick)
            composed = module.CadencedPopulationStrategy(population, None, lambda: 0.0)
            if raises:
                with self.assertRaisesRegex(RuntimeError, "native failure"):
                    composed.tick(50)
            else:
                self.assertFalse(composed.tick(50))
            self.assertFalse(composed.tick(50))
            self.assertEqual(calls, [50])


class DurableQuestTests(unittest.TestCase):
    def setUp(self):
        import tempfile
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "quest-state.json"
        self.native = Native()
        self.native.rows = self.native.rows[:1]
        read = self.native.strategic_read_quest_snapshot
        self.native.strategic_read_quest_snapshot = lambda *args: dict(read(*args), level=2)
        self.phase = dict(enabled=True, policy_valid=True, phase_id="1_19", policy_digest="b" * 64)
        self.native.population_read_phase_snapshot = lambda: self.phase
        self.now, self.wall = 100, 1000
        self.audit = []
        self.bindings = {"a": (2, 11)}

    def policy(self, source="a" * 64):
        return module.QuestPolicy(self.native, self.audit.append, lambda: self.now,
            memory_path=self.path, source_sha256=source, wall_clock=lambda: self.wall)

    def test_restart_retry_deadline_dedup_and_transition_only_writes(self):
        policy = self.policy()
        self.assertFalse(self.path.exists())  # Loading does not mutate state or native objects.
        policy.tick(self.bindings)
        initial = self.path.read_bytes(), self.path.stat().st_ino
        self.now += 1; self.wall += 1
        policy.tick(self.bindings)
        self.assertEqual((self.path.read_bytes(), self.path.stat().st_ino), initial)
        self.native.current = 0
        policy.tick(self.bindings)
        failure = next(iter(policy.memory.failures.values()))
        self.assertEqual((failure["count"], failure["retry_epoch"], failure["reason"]), (1, 1031, "goal_lost"))
        restarted = self.policy()
        sets = sum(c[0] == "set" for c in self.native.calls)
        self.now += 10; self.wall += 10
        restarted.tick(self.bindings)
        self.assertEqual(sum(c[0] == "set" for c in self.native.calls), sets)
        self.assertEqual(next(iter(restarted.memory.failures.values()))["count"], 1)
        self.now += 20; self.wall += 20
        restarted.tick(self.bindings)
        self.assertEqual(sum(c[0] == "set" for c in self.native.calls), sets + 1)

    def test_deferrals_do_not_create_failure_or_rewrite_selected_goal(self):
        policy = self.policy(); policy.tick(self.bindings)
        initial = self.path.read_bytes()
        self.native.control = False
        for _ in range(3):
            self.now += 1; self.wall += 1; policy.tick(self.bindings)
        self.assertEqual(policy.memory.failures, {})
        self.assertEqual(self.path.read_bytes(), initial)
        self.assertEqual(policy.goals, {"a": 7})
        self.native.current = 0
        policy.tick(self.bindings)
        self.assertEqual(policy.goals, {})
        self.native.control = True
        restarted = self.policy(); restarted.tick(self.bindings)
        self.assertEqual(restarted.memory.failures, {})

    def test_reward_after_restart_is_history_not_failure(self):
        self.policy().tick(self.bindings)
        self.native.observed = "rewarded"; self.native.rows = []; self.native.current = 0
        restarted = self.policy(); restarted.tick(self.bindings)
        self.assertEqual(restarted.goals, {})
        self.assertEqual(restarted.memory.failures, {})
        self.assertEqual([r["event"] for r in self.audit].count("rewarded"), 1)
        self.policy().tick(self.bindings)
        self.assertEqual([r["event"] for r in self.audit].count("rewarded"), 1)

    def test_context_change_does_not_reuse_failure_or_count_interruption(self):
        policy = self.policy(); policy.tick(self.bindings)
        self.native.current = 0
        changed = self.policy(source="c" * 64); changed.tick(self.bindings)
        self.assertEqual(changed.memory.failures, {})
        self.native.current = 0; changed.tick(self.bindings)
        self.assertEqual(len(changed.memory.failures), 1)
        self.phase["policy_digest"] = "d" * 64
        # Fresh context does not inherit the old retry deadline.
        self.policy(source="c" * 64).tick(self.bindings)
        self.assertEqual(self.native.current, 7)

    def test_exact_failure_names_and_corruption_or_clock_rollback_refuse(self):
        policy = self.policy(); policy.tick(self.bindings)
        self.native.current = 0; self.native.observed = "failed"
        self.native.rows[0]["status"] = "failed"
        policy.tick(self.bindings)
        self.assertEqual(next(iter(policy.memory.failures.values()))["reason"], "quest_failed")
        self.wall -= 1
        with self.assertRaises(module.ProtocolError):
            self.policy().tick(self.bindings)
        self.path.write_text(self.path.read_text().replace('"count":1', '"count":2'))
        with self.assertRaises(module.ProtocolError):
            self.policy()

    def test_native_failed_marker_is_counted_once_without_inventing_cause(self):
        policy = self.policy(); policy.tick(self.bindings)
        self.native.current = 0
        self.native.rows[0].update(eligible=False, rejection_reason="native_failed_quest")
        policy.tick(self.bindings)
        first = self.path.read_bytes()
        policy.tick(self.bindings)
        record = next(iter(policy.memory.failures.values()))
        self.assertEqual((record["reason"], record["count"]), ("native_failed_quest", 1))
        self.assertEqual(first, self.path.read_bytes())


if __name__ == "__main__":
    unittest.main()
