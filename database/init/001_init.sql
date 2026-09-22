CREATE TABLE accounts
(
    id INT UNSIGNED NOT NULL,
    PRIMARY KEY (id)
) ENGINE=InnoDB;

CREATE TABLE characters
(
    id INT UNSIGNED NOT NULL,
    account_id INT UNSIGNED NOT NULL,
    name VARCHAR(16) NOT NULL,
    level SMALLINT UNSIGNED NOT NULL DEFAULT 1,

    PRIMARY KEY (id),
    INDEX idx_characters_account_id (account_id),

    CONSTRAINT fk_characters_account
        FOREIGN KEY (account_id)
        REFERENCES accounts(id)
        ON DELETE CASCADE
) ENGINE=InnoDB;

INSERT INTO accounts (id)
VALUES (1);

INSERT INTO characters (id, account_id, name, level)
VALUES
    (1001, 1, 'Warrior', 10),
    (1002, 1, 'Magician', 15);