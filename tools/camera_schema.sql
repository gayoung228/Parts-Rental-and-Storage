USE parts_rental;
CREATE TABLE IF NOT EXISTS tool_detections (
    id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
    locker_id VARCHAR(31) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    camera_id VARCHAR(31) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    uid VARCHAR(20) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    session_id CHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    tool_label VARCHAR(31) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    confidence_milli SMALLINT UNSIGNED NOT NULL,
    event_id CHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    household_id INT NOT NULL,
    received_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    UNIQUE KEY uq_camera_event (camera_id, event_id),
    FOREIGN KEY (household_id) REFERENCES households(id),
    CHECK (confidence_milli BETWEEN 900 AND 1000)
) ENGINE=InnoDB;
GRANT SELECT, INSERT ON parts_rental.tool_detections TO 'parts_app'@'localhost';
-- UPDATE is required for the no-op duplicate-key clause; existing rows stay unchanged.
GRANT UPDATE ON parts_rental.tool_detections TO 'parts_app'@'localhost';
