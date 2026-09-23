CREATE TABLE accounts
(
    id INT UNSIGNED NOT NULL,
    login_id VARCHAR(32) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,

    PRIMARY KEY (id),
    UNIQUE KEY uq_accounts_login_id (login_id)
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

INSERT INTO accounts (id, login_id, password_hash)
VALUES
(
    1,
    'test',
    'pbkdf2-sha256$600000$00112233445566778899aabbccddeeff$789a95f0eed0a9ceb639c5d039f1f891aa0b4fe415b7780752e57db5c3c1860d'
);

INSERT INTO characters (id, account_id, name, level)
VALUES
    (1001, 1, 'Warrior', 10),
    (1002, 1, 'Magician', 15);