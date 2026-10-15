-- 양쪽 인벤토리와 함께 커밋하며 같은 거래의 재시도에서 교환을 다시 실행하지 않는다.
CREATE TABLE IF NOT EXISTS character_trade_records
(
    trade_id BIGINT UNSIGNED NOT NULL,
    first_character_id INT UNSIGNED NOT NULL,
    second_character_id INT UNSIGNED NOT NULL,
    request_payload VARBINARY(512) NOT NULL,
    committed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (trade_id),
    KEY ix_trade_first (first_character_id, committed_at),
    KEY ix_trade_second (second_character_id, committed_at),
    CONSTRAINT fk_trade_first FOREIGN KEY (first_character_id) REFERENCES characters(id) ON DELETE CASCADE,
    CONSTRAINT fk_trade_second FOREIGN KEY (second_character_id) REFERENCES characters(id) ON DELETE CASCADE,
    CONSTRAINT ck_trade_distinct CHECK (first_character_id <> second_character_id)
) ENGINE=InnoDB;
