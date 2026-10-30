-- CPI-210 draft Playerbots DB migration. Apply only through the CPI-212 exact-pin
-- migration rehearsal after CPI-191 handoff; this file is not applied by tests.
CREATE TABLE IF NOT EXISTS `playerbots_guide_failure_memory` (
  `failure_key` BINARY(32) NOT NULL COMMENT 'SHA-256 of CanonicalFailureKey v1 bytes',
  `schema_version` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `objective_id` VARCHAR(160) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `scope` TINYINT UNSIGNED NOT NULL COMMENT '0 character, 1 cohort, 2 global',
  `character_id` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `phase_id` INT UNSIGNED NOT NULL,
  `map_id` INT UNSIGNED NOT NULL,
  `zone_id` INT UNSIGNED NOT NULL,
  `group_size` INT UNSIGNED NOT NULL,
  `core_revision` VARCHAR(160) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `playerbots_revision` VARCHAR(160) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `content_revision` VARCHAR(160) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `content_graph_digest` BINARY(32) NOT NULL,
  `profile_id` VARCHAR(160) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `capability_fingerprint` VARCHAR(160) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `failure_count` INT UNSIGNED NOT NULL,
  `last_failure_seconds` BIGINT UNSIGNED NOT NULL,
  `classification` VARCHAR(160) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `observation_digest` BINARY(32) NULL,
  `created_at` TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  `updated_at` TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (`failure_key`),
  KEY `idx_pb_guide_failure_character_objective` (`character_id`, `objective_id`),
  KEY `idx_pb_guide_failure_context` (`phase_id`, `map_id`, `zone_id`, `group_size`),
  KEY `idx_pb_guide_failure_updated` (`updated_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
