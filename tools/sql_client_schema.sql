USE parts_rental;

-- Existing households/rfid_cards tables and registered cards are preserved.
CREATE TABLE IF NOT EXISTS card_events (
    id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
    locker_id VARCHAR(31) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    uid VARCHAR(20) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    request_id CHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    household_id INT NULL,
    auth_result ENUM('APPROVED', 'DENIED') NOT NULL,
    received_count INT UNSIGNED NOT NULL DEFAULT 1,
    received_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    last_received_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    UNIQUE KEY uq_card_request (locker_id, uid, request_id),
    FOREIGN KEY (household_id) REFERENCES households(id)
) ENGINE=InnoDB;

GRANT SELECT ON parts_rental.rfid_cards TO 'parts_app'@'localhost';
GRANT SELECT ON parts_rental.households TO 'parts_app'@'localhost';
GRANT SELECT, INSERT, UPDATE ON parts_rental.card_events TO 'parts_app'@'localhost';

CREATE TABLE IF NOT EXISTS cds_samples (
    id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
    locker_id VARCHAR(31) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    sample_id CHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    adc_value SMALLINT UNSIGNED NOT NULL,
    received_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    last_received_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    UNIQUE KEY uq_cds_sample (locker_id, sample_id),
    CHECK (adc_value <= 4095)
) ENGINE=InnoDB;

GRANT SELECT, INSERT, UPDATE ON parts_rental.cds_samples TO 'parts_app'@'localhost';
