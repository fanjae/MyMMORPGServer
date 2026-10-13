-- 003_field_combat.sql 적용 후 실행한다. 피격 재시도와 HP 변경을 같은 트랜잭션으로 확정한다.
CREATE TABLE IF NOT EXISTS field_combat_mutations
(
    mutation_id BIGINT UNSIGNED NOT NULL,
    character_id INT UNSIGNED NOT NULL,
    damage SMALLINT UNSIGNED NOT NULL,
    applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (mutation_id),
    KEY ix_mutation_applied (applied_at),
    CONSTRAINT fk_mutation_owner FOREIGN KEY (character_id) REFERENCES characters(id) ON DELETE CASCADE,
    CONSTRAINT ck_mutation_damage CHECK (damage > 0)
) ENGINE=InnoDB;
