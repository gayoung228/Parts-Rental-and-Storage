-- Apply rental_schema.sql first. This migration enables automatic rental/return.
USE parts_rental;
SET NAMES utf8mb4;
ALTER TABLE loans
    MODIFY rental_date DATETIME(6) NOT NULL,
    MODIFY due_date DATETIME(6) NOT NULL,
    MODIFY return_date DATETIME(6) NULL,
    ADD COLUMN IF NOT EXISTS late_fee_period_seconds INT UNSIGNED NOT NULL DEFAULT 86400,
    ADD CONSTRAINT IF NOT EXISTS ck_late_fee_period CHECK (late_fee_period_seconds > 0);

CREATE TABLE IF NOT EXISTS rental_policy (
    id TINYINT UNSIGNED PRIMARY KEY,
    duration_seconds INT UNSIGNED NOT NULL,
    rental_fee_won INT UNSIGNED NOT NULL,
    late_fee_won INT UNSIGNED NOT NULL,
    late_fee_period_seconds INT UNSIGNED NOT NULL,
    CHECK (id=1), CHECK (duration_seconds>0), CHECK (late_fee_period_seconds>0)
) ENGINE=InnoDB;
-- Preserve administrator changes on repeated deployment.
INSERT INTO rental_policy VALUES (1,300,500,500,300) ON DUPLICATE KEY UPDATE id=id;
INSERT INTO rental_tools(tool_no,tool_name) VALUES
    ('1','스트리퍼'),('2','드라이버'),('3','니퍼'),('4','롱노즈'),('5','멀티미터기')
ON DUPLICATE KEY UPDATE tool_name=VALUES(tool_name);

CREATE TABLE IF NOT EXISTS rental_actions (
    locker_id VARCHAR(31) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    session_id CHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    tool_id BIGINT UNSIGNED NOT NULL,
    household_id INT NOT NULL,
    camera_id VARCHAR(31) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    event_id CHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    action ENUM('RENTED','RETURNED','BUSY') NOT NULL,
    loan_id BIGINT UNSIGNED NULL,
    PRIMARY KEY (locker_id,session_id,tool_id),
    FOREIGN KEY (tool_id) REFERENCES rental_tools(id),
    FOREIGN KEY (household_id) REFERENCES households(id),
    FOREIGN KEY (loan_id) REFERENCES loans(id)
) ENGINE=InnoDB;

-- All DATETIME values used here are Korea time (UTC+09:00).
CREATE OR REPLACE SQL SECURITY INVOKER VIEW loan_overdue AS
SELECT l.id AS loan_id,l.household_id,l.tool_id,l.rental_date,l.due_date,l.return_date,
       (l.return_date IS NOT NULL) AS is_returned,l.rental_fee_won,l.daily_late_fee_won,
       l.late_fee_period_seconds,
       GREATEST(0,TIMESTAMPDIFF(DAY,l.due_date,
           COALESCE(l.return_date,UTC_TIMESTAMP(6)+INTERVAL 9 HOUR))) AS overdue_days,
       GREATEST(0,TIMESTAMPDIFF(SECOND,l.due_date,
           COALESCE(l.return_date,UTC_TIMESTAMP(6)+INTERVAL 9 HOUR))) AS overdue_seconds,
       CEILING(GREATEST(0,TIMESTAMPDIFF(MICROSECOND,l.due_date,
           COALESCE(l.return_date,UTC_TIMESTAMP(6)+INTERVAL 9 HOUR))) /
           (CAST(l.late_fee_period_seconds AS DECIMAL(20,0))*1000000)) AS overdue_periods
FROM loans l;

CREATE OR REPLACE SQL SECURITY INVOKER VIEW loan_list AS
SELECT o.loan_id,o.household_id,h.building_no,h.unit_no,t.tool_no,t.tool_name,
       o.rental_date,o.due_date,o.is_returned,o.return_date,o.overdue_days,
       o.overdue_seconds,FLOOR(o.overdue_seconds/60) AS overdue_minutes,
       o.overdue_periods,o.late_fee_period_seconds,o.rental_fee_won,
       o.overdue_periods*o.daily_late_fee_won AS late_fee_won,
       o.rental_fee_won+o.overdue_periods*o.daily_late_fee_won AS assessed_amount_won,
       COALESCE(b.billed_amount_won,0) AS billed_amount_won,
       GREATEST(0,CAST(o.rental_fee_won+o.overdue_periods*o.daily_late_fee_won AS DECIMAL(20,0))
           -CAST(COALESCE(b.billed_amount_won,0) AS DECIMAL(20,0))) AS amount_due_won
FROM loan_overdue o JOIN households h ON h.id=o.household_id
JOIN rental_tools t ON t.id=o.tool_id LEFT JOIN loan_billed_totals b ON b.loan_id=o.loan_id;

CREATE OR REPLACE SQL SECURITY INVOKER VIEW household_loan_totals AS
SELECT household_id,SUM(is_returned=0) AS unreturned_count,
       SUM(CASE WHEN is_returned=0 THEN overdue_days ELSE 0 END) AS overdue_days_total,
       SUM(CASE WHEN is_returned=0 THEN overdue_seconds ELSE 0 END) AS overdue_seconds_total,
       SUM(amount_due_won) AS amount_due_won FROM loan_list GROUP BY household_id;

CREATE OR REPLACE SQL SECURITY INVOKER VIEW household_rental_summary AS
SELECT h.id AS household_id,h.building_no,h.unit_no,
       COALESCE(t.unreturned_count,0) AS unreturned_count,
       COALESCE(t.overdue_days_total,0) AS overdue_days_total,
       COALESCE(t.overdue_seconds_total,0) AS overdue_seconds_total,
       FLOOR(COALESCE(t.overdue_seconds_total,0)/60) AS overdue_minutes_total,
       COALESCE(t.amount_due_won,0) AS amount_due_won
FROM households h LEFT JOIN household_loan_totals t ON t.household_id=h.id;

CREATE OR REPLACE SQL SECURITY INVOKER VIEW rental_tool_status AS
SELECT t.id,t.tool_no,t.tool_name,t.enabled,(l.id IS NOT NULL) AS rental_state,
       l.id AS loan_id,l.household_id,l.rental_date,l.due_date
FROM rental_tools t LEFT JOIN loans l ON l.tool_id=t.id AND l.return_date IS NULL;

GRANT SELECT ON parts_rental.rental_policy TO 'parts_app'@'localhost';
GRANT SELECT,INSERT ON parts_rental.rental_actions TO 'parts_app'@'localhost';
GRANT SELECT ON parts_rental.rental_tool_status TO 'parts_app'@'localhost';
