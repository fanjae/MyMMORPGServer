-- 기존 002_inventory.sql 적용 후 실행한다. 기존 장비 슬롯의 인스턴스도 한 번만 생성한다.
CREATE TABLE IF NOT EXISTS character_item_instances
(
    instance_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    character_id INT UNSIGNED NOT NULL,
    item_id INT UNSIGNED NOT NULL,
    origin_slot TINYINT UNSIGNED NULL,
    PRIMARY KEY (instance_id),
    UNIQUE KEY uq_instance_owner_item (instance_id, character_id, item_id),
    UNIQUE KEY uq_instance_origin (character_id, origin_slot),
    CONSTRAINT fk_instance_owner FOREIGN KEY (character_id) REFERENCES characters(id) ON DELETE CASCADE,
    CONSTRAINT fk_instance_item FOREIGN KEY (item_id) REFERENCES item_definitions(id),
    CONSTRAINT ck_instance_item CHECK (item_id IN (1,2))
) ENGINE=InnoDB;

DELIMITER //
DROP PROCEDURE IF EXISTS migrate_field_equipment//
CREATE PROCEDURE migrate_field_equipment()
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = 'character_inventory_slots' AND column_name = 'instance_id') THEN
        ALTER TABLE character_inventory_slots ADD COLUMN instance_id BIGINT UNSIGNED NULL;
    END IF;
    INSERT INTO character_item_instances (character_id, item_id, origin_slot)
    SELECT s.character_id, s.item_id, s.slot FROM character_inventory_slots s
    WHERE s.category = 0 AND s.instance_id IS NULL
    ON DUPLICATE KEY UPDATE instance_id = character_item_instances.instance_id;
    UPDATE character_inventory_slots s JOIN character_item_instances i ON i.character_id = s.character_id AND i.origin_slot = s.slot AND i.item_id = s.item_id
    SET s.instance_id = i.instance_id WHERE s.category = 0 AND s.instance_id IS NULL;
    IF NOT EXISTS (SELECT 1 FROM information_schema.table_constraints WHERE constraint_schema = DATABASE() AND table_name = 'character_inventory_slots' AND constraint_name = 'fk_slot_instance') THEN
        ALTER TABLE character_inventory_slots
        ADD UNIQUE KEY uq_slot_instance (instance_id),
        ADD CONSTRAINT fk_slot_instance FOREIGN KEY (instance_id, character_id, item_id) REFERENCES character_item_instances(instance_id, character_id, item_id),
        ADD CONSTRAINT ck_slot_instance CHECK ((category = 0 AND instance_id IS NOT NULL) OR (category <> 0 AND instance_id IS NULL));
    END IF;
END//
DELIMITER ;
CALL migrate_field_equipment();
DROP PROCEDURE migrate_field_equipment;

CREATE TABLE IF NOT EXISTS character_equipment
(
    character_id INT UNSIGNED NOT NULL,
    slot TINYINT UNSIGNED NOT NULL,
    instance_id BIGINT UNSIGNED NOT NULL,
    item_id INT UNSIGNED NOT NULL,
    PRIMARY KEY (character_id, slot),
    UNIQUE KEY uq_equipped_instance (instance_id),
    CONSTRAINT fk_equipped_inventory FOREIGN KEY (character_id) REFERENCES character_inventories(character_id) ON DELETE CASCADE,
    CONSTRAINT fk_equipped_instance FOREIGN KEY (instance_id, character_id, item_id) REFERENCES character_item_instances(instance_id, character_id, item_id),
    CONSTRAINT ck_equipped_slot CHECK ((slot=0 AND item_id=1) OR (slot=1 AND item_id=2))
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS field_drop_claims
(
    drop_id BIGINT UNSIGNED NOT NULL,
    character_id INT UNSIGNED NOT NULL,
    item_id INT UNSIGNED NOT NULL,
    quantity SMALLINT UNSIGNED NOT NULL,
    acquired_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (drop_id),
    KEY ix_claim_created (acquired_at),
    CONSTRAINT fk_claim_owner FOREIGN KEY (character_id) REFERENCES characters(id) ON DELETE CASCADE,
    CONSTRAINT fk_claim_item FOREIGN KEY (item_id) REFERENCES item_definitions(id),
    CONSTRAINT ck_claim_quantity CHECK (quantity BETWEEN 1 AND 200)
) ENGINE=InnoDB;
