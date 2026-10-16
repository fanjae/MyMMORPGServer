-- 기존 계정·캐릭터와 외래 키를 유지하고 새 ID는 DB에서 발급한다.
SET @registration_foreign_key_checks = @@FOREIGN_KEY_CHECKS;
SET FOREIGN_KEY_CHECKS = 0;
ALTER TABLE accounts MODIFY COLUMN id INT UNSIGNED NOT NULL AUTO_INCREMENT;
ALTER TABLE characters MODIFY COLUMN id INT UNSIGNED NOT NULL AUTO_INCREMENT;
SET FOREIGN_KEY_CHECKS = @registration_foreign_key_checks;
