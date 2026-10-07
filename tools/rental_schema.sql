-- Run as an administrator. Adds rental tables; preserves existing cards/detections.
CREATE DATABASE IF NOT EXISTS parts_rental CHARACTER SET utf8mb4;
USE parts_rental;

CREATE TABLE IF NOT EXISTS households (
    id INT PRIMARY KEY AUTO_INCREMENT,
    building_no VARCHAR(10) NOT NULL,
    unit_no VARCHAR(10) NOT NULL,
    UNIQUE KEY uq_household_address (building_no, unit_no)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS rental_tools (
    id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
    tool_no VARCHAR(31) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    tool_name VARCHAR(100) CHARACTER SET utf8mb4 NOT NULL,
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    UNIQUE KEY uq_tool_number (tool_no)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS loans (
    id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
    household_id INT NOT NULL,
    tool_id BIGINT UNSIGNED NOT NULL,
    rental_date DATE NOT NULL,
    due_date DATE NOT NULL,
    return_date DATE NULL,
    rental_fee_won INT UNSIGNED NOT NULL DEFAULT 0,
    daily_late_fee_won INT UNSIGNED NOT NULL DEFAULT 0,
    -- Multiple historical rentals are allowed; only one open rental per physical tool.
    active_tool_id BIGINT UNSIGNED GENERATED ALWAYS AS
        (CASE WHEN return_date IS NULL THEN tool_id ELSE NULL END) PERSISTENT,
    UNIQUE KEY uq_open_tool_loan (active_tool_id),
    KEY ix_household_returns (household_id, return_date),
    CONSTRAINT fk_loan_household FOREIGN KEY (household_id) REFERENCES households(id),
    CONSTRAINT fk_loan_tool FOREIGN KEY (tool_id) REFERENCES rental_tools(id),
    CONSTRAINT ck_loan_due CHECK (due_date >= rental_date),
    CONSTRAINT ck_loan_return CHECK (return_date IS NULL OR return_date >= rental_date)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS loan_billing_items (
    id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
    loan_id BIGINT UNSIGNED NOT NULL,
    billing_month DATE NOT NULL,
    amount_won BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    UNIQUE KEY uq_monthly_loan_bill (loan_id, billing_month),
    CONSTRAINT fk_bill_loan FOREIGN KEY (loan_id) REFERENCES loans(id),
    CONSTRAINT ck_bill_month CHECK (DAYOFMONTH(billing_month) = 1),
    CONSTRAINT ck_bill_positive CHECK (amount_won > 0)
) ENGINE=InnoDB;

-- Calendar-day policy: due date itself is free of late fees; next day = one day late.
-- Unreturned loans use today's date in Korea, independent of client session timezone.
CREATE OR REPLACE SQL SECURITY INVOKER VIEW loan_overdue AS
SELECT l.id AS loan_id, l.household_id, l.tool_id,
       l.rental_date, l.due_date, l.return_date,
       (l.return_date IS NOT NULL) AS is_returned,
       l.rental_fee_won, l.daily_late_fee_won,
       GREATEST(0, DATEDIFF(
           COALESCE(l.return_date, DATE(CONVERT_TZ(UTC_TIMESTAMP(), '+00:00', '+09:00'))),
           l.due_date)) AS overdue_days
FROM loans l;

CREATE OR REPLACE SQL SECURITY INVOKER VIEW loan_billed_totals AS
SELECT loan_id, SUM(amount_won) AS billed_amount_won
FROM loan_billing_items
GROUP BY loan_id;

CREATE OR REPLACE SQL SECURITY INVOKER VIEW loan_list AS
SELECT o.loan_id, o.household_id, h.building_no, h.unit_no,
       t.tool_no, t.tool_name,
       o.rental_date, o.due_date, o.is_returned, o.return_date, o.overdue_days,
       o.rental_fee_won,
       o.overdue_days * o.daily_late_fee_won AS late_fee_won,
       o.rental_fee_won + o.overdue_days * o.daily_late_fee_won AS assessed_amount_won,
       COALESCE(b.billed_amount_won, 0) AS billed_amount_won,
       GREATEST(0,
           CAST(o.rental_fee_won + o.overdue_days * o.daily_late_fee_won AS DECIMAL(20,0))
           - CAST(COALESCE(b.billed_amount_won, 0) AS DECIMAL(20,0))) AS amount_due_won
FROM loan_overdue o
JOIN households h ON h.id=o.household_id
JOIN rental_tools t ON t.id=o.tool_id
LEFT JOIN loan_billed_totals b ON b.loan_id=o.loan_id;

CREATE OR REPLACE SQL SECURITY INVOKER VIEW household_loan_totals AS
SELECT household_id,
       SUM(CASE WHEN is_returned=0 THEN 1 ELSE 0 END) AS unreturned_count,
       SUM(CASE WHEN is_returned=0 THEN overdue_days ELSE 0 END) AS overdue_days_total,
       SUM(amount_due_won) AS amount_due_won
FROM loan_list
GROUP BY household_id;

CREATE OR REPLACE SQL SECURITY INVOKER VIEW household_rental_summary AS
SELECT h.id AS household_id, h.building_no, h.unit_no,
       COALESCE(t.unreturned_count, 0) AS unreturned_count,
       COALESCE(t.overdue_days_total, 0) AS overdue_days_total,
       COALESCE(t.amount_due_won, 0) AS amount_due_won
FROM households h
LEFT JOIN household_loan_totals t ON t.household_id=h.id;

-- Application account already used by sql_client. No DELETE privilege for history.
GRANT SELECT, INSERT, UPDATE ON parts_rental.rental_tools TO 'parts_app'@'localhost';
GRANT SELECT, INSERT, UPDATE ON parts_rental.loans TO 'parts_app'@'localhost';
GRANT SELECT, INSERT, UPDATE ON parts_rental.loan_billing_items TO 'parts_app'@'localhost';
GRANT SELECT ON parts_rental.households TO 'parts_app'@'localhost';
GRANT SELECT ON parts_rental.loan_overdue TO 'parts_app'@'localhost';
GRANT SELECT ON parts_rental.loan_billed_totals TO 'parts_app'@'localhost';
GRANT SELECT ON parts_rental.loan_list TO 'parts_app'@'localhost';
GRANT SELECT ON parts_rental.household_loan_totals TO 'parts_app'@'localhost';
GRANT SELECT ON parts_rental.household_rental_summary TO 'parts_app'@'localhost';
