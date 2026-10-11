-- Apply to the existing character database; fresh Docker volumes run this after 001_init.sql.
CREATE TABLE IF NOT EXISTS item_definitions
(
    id INT UNSIGNED NOT NULL,
    category TINYINT UNSIGNED NOT NULL,
    max_stack SMALLINT UNSIGNED NOT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uq_item_category (id, category),
    CONSTRAINT ck_item_category CHECK (category < 5),
    CONSTRAINT ck_item_stack CHECK (max_stack BETWEEN 1 AND 200)
) ENGINE=InnoDB;

INSERT INTO item_definitions (id, category, max_stack)
VALUES (1,0,1), (2,0,1), (101,1,100), (102,1,100), (201,2,1), (301,3,200), (302,3,200), (401,4,1)
ON DUPLICATE KEY UPDATE id = item_definitions.id;

CREATE TABLE IF NOT EXISTS character_inventories
(
    character_id INT UNSIGNED NOT NULL,
    revision BIGINT UNSIGNED NOT NULL DEFAULT 1,
    mesos BIGINT UNSIGNED NOT NULL DEFAULT 1250,
    hp SMALLINT UNSIGNED NOT NULL DEFAULT 50,
    max_hp SMALLINT UNSIGNED NOT NULL DEFAULT 100,
    mp SMALLINT UNSIGNED NOT NULL DEFAULT 25,
    max_mp SMALLINT UNSIGNED NOT NULL DEFAULT 60,
    PRIMARY KEY (character_id),
    CONSTRAINT fk_inventory_character FOREIGN KEY (character_id) REFERENCES characters(id) ON DELETE CASCADE,
    CONSTRAINT ck_inventory_revision CHECK (revision > 0),
    CONSTRAINT ck_inventory_hp CHECK (max_hp > 0 AND hp <= max_hp),
    CONSTRAINT ck_inventory_mp CHECK (max_mp > 0 AND mp <= max_mp)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS character_inventory_slots
(
    character_id INT UNSIGNED NOT NULL,
    category TINYINT UNSIGNED NOT NULL,
    slot TINYINT UNSIGNED NOT NULL,
    item_id INT UNSIGNED NOT NULL,
    quantity SMALLINT UNSIGNED NOT NULL,
    PRIMARY KEY (character_id, category, slot),
    CONSTRAINT fk_slot_inventory FOREIGN KEY (character_id) REFERENCES character_inventories(character_id) ON DELETE CASCADE,
    CONSTRAINT fk_slot_item_category FOREIGN KEY (item_id, category) REFERENCES item_definitions(id, category),
    CONSTRAINT ck_inventory_slot CHECK (category < 5 AND slot < 24),
    CONSTRAINT ck_inventory_quantity CHECK (quantity BETWEEN 1 AND 200)
) ENGINE=InnoDB;
