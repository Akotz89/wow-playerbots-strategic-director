-- Destructive rollback. Run only after disabling the guide, draining writes, and
-- preserving an evidence backup during the CPI-212 rollback rehearsal.
DROP TABLE IF EXISTS `playerbots_guide_failure_memory`;
