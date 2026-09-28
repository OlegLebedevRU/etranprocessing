BEGIN;

-- Running upgrade 026 -> 027

CREATE TABLE fin_archive_batches (
    id VARCHAR(128) NOT NULL, 
    source_project VARCHAR(64) NOT NULL, 
    schema_version VARCHAR(64) NOT NULL, 
    archive_month DATE NOT NULL, 
    source_types JSON NOT NULL, 
    row_count BIGINT NOT NULL, 
    min_occurred_at TIMESTAMP WITH TIME ZONE, 
    max_occurred_at TIMESTAMP WITH TIME ZONE, 
    through_cursor BIGINT, 
    consumers_passed_cursor BIGINT, 
    storage_reference TEXT NOT NULL, 
    checksum_sha256 VARCHAR(64) NOT NULL, 
    manifest JSON NOT NULL, 
    status VARCHAR(16) DEFAULT 'pending' NOT NULL, 
    verified_at TIMESTAMP WITH TIME ZONE, 
    purged_at TIMESTAMP WITH TIME ZONE, 
    retain_until TIMESTAMP WITH TIME ZONE NOT NULL, 
    actor VARCHAR(128) NOT NULL, 
    correlation_id VARCHAR(128) NOT NULL, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    CONSTRAINT fin_archive_pk PRIMARY KEY (id, source_project), 
    CONSTRAINT fin_archive_purge_ck CHECK (purged_at IS NULL OR (status = 'verified' AND verified_at IS NOT NULL AND (through_cursor IS NULL OR (consumers_passed_cursor IS NOT NULL AND consumers_passed_cursor >= through_cursor)))), 
    CONSTRAINT fin_archive_verified_ck CHECK (status != 'verified' OR verified_at IS NOT NULL), 
    CONSTRAINT fin_archive_status_ck CHECK (status IN ('pending', 'verified', 'failed')), 
    CONSTRAINT fin_archive_interval_ck CHECK (max_occurred_at >= min_occurred_at), 
    CONSTRAINT fin_archive_values_ck CHECK (row_count >= 0 AND length(checksum_sha256) = 64), 
    CONSTRAINT fin_archive_cursor_ck CHECK (through_cursor >= 0 AND consumers_passed_cursor >= 0)
);

CREATE INDEX fin_archive_project_month_ix ON fin_archive_batches (source_project, archive_month);

CREATE TABLE fin_tariff_versions (
    id BIGSERIAL NOT NULL, 
    version VARCHAR(64) NOT NULL, 
    effective_from TIMESTAMP WITH TIME ZONE NOT NULL, 
    terminal_month_kopecks BIGINT NOT NULL, 
    hourly_rate_kopecks BIGINT NOT NULL, 
    free_daily_seconds INTEGER NOT NULL, 
    actor VARCHAR(128) NOT NULL, 
    correlation_id VARCHAR(128) NOT NULL, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    CONSTRAINT fin_tariff_pk PRIMARY KEY (id), 
    CONSTRAINT fin_tariff_values_ck CHECK (terminal_month_kopecks >= 0 AND hourly_rate_kopecks >= 0 AND free_daily_seconds >= 0), 
    CONSTRAINT fin_tariff_effective_uq UNIQUE (effective_from), 
    CONSTRAINT fin_tariff_version_uq UNIQUE (version)
);

CREATE TABLE iot_consumer_checkpoints (
    consumer_id VARCHAR(64) NOT NULL, 
    feed_name VARCHAR(64) NOT NULL, 
    last_cursor BIGINT NOT NULL, 
    last_event_id VARCHAR(128), 
    last_event_occurred_at TIMESTAMP WITH TIME ZONE, 
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL, 
    PRIMARY KEY (consumer_id)
);

CREATE TABLE iot_event_inbox (
    event_id VARCHAR(128) NOT NULL, 
    cursor BIGINT NOT NULL, 
    event_type VARCHAR(64) NOT NULL, 
    event_version VARCHAR(32) NOT NULL, 
    occurred_at TIMESTAMP WITH TIME ZONE NOT NULL, 
    tenant_id INTEGER, 
    terminal_id VARCHAR(128), 
    device_id INTEGER, 
    sn VARCHAR(128) NOT NULL, 
    session_id VARCHAR(128), 
    session_type VARCHAR(32), 
    lifecycle_state VARCHAR(32), 
    reason VARCHAR(128), 
    operation_id VARCHAR(128), 
    correlation_id VARCHAR(128), 
    payload JSON, 
    received_at TIMESTAMP WITH TIME ZONE NOT NULL, 
    processed_at TIMESTAMP WITH TIME ZONE, 
    status VARCHAR(32) NOT NULL, 
    PRIMARY KEY (event_id)
);

CREATE INDEX ix_iot_event_inbox_event_type ON iot_event_inbox (event_type);

CREATE INDEX ix_iot_event_inbox_tenant_id ON iot_event_inbox (tenant_id);

CREATE INDEX ix_iot_event_inbox_cursor ON iot_event_inbox (cursor);

CREATE INDEX ix_iot_event_inbox_operation_id ON iot_event_inbox (operation_id);

CREATE INDEX ix_iot_event_inbox_sn ON iot_event_inbox (sn);

CREATE INDEX ix_iot_event_inbox_session_id ON iot_event_inbox (session_id);

CREATE TABLE iot_event_quarantine (
    id SERIAL NOT NULL, 
    event_id VARCHAR(128) NOT NULL, 
    cursor BIGINT NOT NULL, 
    error_code VARCHAR(64) NOT NULL, 
    error_detail TEXT NOT NULL, 
    raw_event JSON NOT NULL, 
    quarantined_at TIMESTAMP WITH TIME ZONE NOT NULL, 
    retry_count INTEGER NOT NULL, 
    resolved BOOLEAN NOT NULL, 
    PRIMARY KEY (id)
);

CREATE INDEX ix_iot_event_quarantine_event_id ON iot_event_quarantine (event_id);

CREATE TABLE fin_accounts (
    id BIGSERIAL NOT NULL, 
    tenant_id INTEGER, 
    kind VARCHAR(32) NOT NULL, 
    currency VARCHAR(3) DEFAULT 'RUB' NOT NULL, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    CONSTRAINT fin_account_pk PRIMARY KEY (id), 
    CONSTRAINT fin_account_scope_ck CHECK ((kind = 'tenant_settlement' AND tenant_id IS NOT NULL) OR (kind IN ('payment_clearing', 'usage_revenue') AND tenant_id IS NULL)), 
    CONSTRAINT fin_account_currency_ck CHECK (currency = 'RUB'), 
    CONSTRAINT fin_account_tenant_fk FOREIGN KEY(tenant_id) REFERENCES orgs (org_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_account_tenant_kind_uq UNIQUE (tenant_id, kind)
);

CREATE UNIQUE INDEX fin_account_global_kind_uq ON fin_accounts (kind) WHERE tenant_id IS NULL;

CREATE TABLE fin_ledger_transactions (
    id BIGSERIAL NOT NULL, 
    tenant_id INTEGER NOT NULL, 
    operation_id VARCHAR(128) NOT NULL, 
    kind VARCHAR(32) NOT NULL, 
    status VARCHAR(16) DEFAULT 'draft' NOT NULL, 
    corrects_transaction_id BIGINT, 
    debit_kopecks BIGINT NOT NULL, 
    credit_kopecks BIGINT NOT NULL, 
    source_project VARCHAR(64) NOT NULL, 
    source_type VARCHAR(64) NOT NULL, 
    source_id VARCHAR(128) NOT NULL, 
    source_event_id VARCHAR(128), 
    source_events_hash VARCHAR(64) NOT NULL, 
    archive_batch_id VARCHAR(128), 
    calculation_snapshot JSON, 
    actor VARCHAR(128) NOT NULL, 
    correlation_id VARCHAR(128) NOT NULL, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    posted_at TIMESTAMP WITH TIME ZONE, 
    CONSTRAINT fin_transaction_pk PRIMARY KEY (id), 
    CONSTRAINT fin_transaction_correction_ck CHECK ((kind IN ('adjustment', 'reversal')) = (corrects_transaction_id IS NOT NULL)), 
    CONSTRAINT fin_transaction_posted_ck CHECK ((status = 'posted') = (posted_at IS NOT NULL)), 
    CONSTRAINT fin_transaction_kind_ck CHECK (kind IN ('payment', 'usage', 'terminal_month', 'adjustment', 'reversal')), 
    CONSTRAINT fin_transaction_status_ck CHECK (status IN ('draft', 'posted')), 
    CONSTRAINT fin_transaction_self_ck CHECK (corrects_transaction_id IS NULL OR corrects_transaction_id != id), 
    CONSTRAINT fin_transaction_balance_ck CHECK (debit_kopecks >= 0 AND debit_kopecks = credit_kopecks AND debit_kopecks % 100 = 0), 
    CONSTRAINT fin_transaction_corrects_fk FOREIGN KEY(corrects_transaction_id, tenant_id) REFERENCES fin_ledger_transactions (id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_transaction_tenant_fk FOREIGN KEY(tenant_id) REFERENCES orgs (org_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_transaction_tenant_uq UNIQUE (id, tenant_id), 
    CONSTRAINT fin_transaction_operation_uq UNIQUE (tenant_id, operation_id), 
    CONSTRAINT fin_transaction_source_uq UNIQUE (tenant_id, source_project, source_type, source_id)
);

CREATE INDEX fin_transaction_tenant_time_ix ON fin_ledger_transactions (tenant_id, created_at);

CREATE INDEX fin_transaction_correlation_ix ON fin_ledger_transactions (correlation_id);

CREATE TABLE fin_reconciliation_runs (
    id BIGSERIAL NOT NULL, 
    tenant_id INTEGER, 
    operation_id VARCHAR(128) NOT NULL, 
    period_start TIMESTAMP WITH TIME ZONE NOT NULL, 
    period_end TIMESTAMP WITH TIME ZONE NOT NULL, 
    status VARCHAR(16) DEFAULT 'pending' NOT NULL, 
    calculated_kopecks BIGINT, 
    posted_kopecks BIGINT, 
    discarded_kopecks BIGINT, 
    debit_kopecks BIGINT, 
    credit_kopecks BIGINT, 
    balance_difference_kopecks BIGINT, 
    mismatch_count BIGINT, 
    source_events_hash VARCHAR(64), 
    details JSON, 
    actor VARCHAR(128) NOT NULL, 
    correlation_id VARCHAR(128) NOT NULL, 
    started_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    finished_at TIMESTAMP WITH TIME ZONE, 
    CONSTRAINT fin_reconciliation_pk PRIMARY KEY (id), 
    CONSTRAINT fin_reconciliation_matched_ck CHECK (status != 'matched' OR (finished_at IS NOT NULL AND mismatch_count IS NOT NULL AND mismatch_count = 0 AND calculated_kopecks IS NOT NULL AND posted_kopecks IS NOT NULL AND discarded_kopecks IS NOT NULL AND calculated_kopecks = posted_kopecks + discarded_kopecks AND debit_kopecks IS NOT NULL AND credit_kopecks IS NOT NULL AND debit_kopecks = credit_kopecks AND balance_difference_kopecks IS NOT NULL AND balance_difference_kopecks = 0)), 
    CONSTRAINT fin_reconciliation_status_ck CHECK (status IN ('pending', 'matched', 'mismatch', 'failed')), 
    CONSTRAINT fin_reconciliation_count_ck CHECK (mismatch_count >= 0), 
    CONSTRAINT fin_reconciliation_period_ck CHECK (period_end > period_start), 
    CONSTRAINT fin_reconciliation_tenant_fk FOREIGN KEY(tenant_id) REFERENCES orgs (org_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_reconciliation_operation_uq UNIQUE (operation_id)
);

CREATE INDEX fin_reconciliation_tenant_time_ix ON fin_reconciliation_runs (tenant_id, started_at);

CREATE TABLE l4desk_audit_events (
    id BIGSERIAL NOT NULL, 
    tenant_id INTEGER, 
    actor VARCHAR(128) NOT NULL, 
    event_type VARCHAR(64) NOT NULL, 
    subject_type VARCHAR(64) NOT NULL, 
    subject_id VARCHAR(128) NOT NULL, 
    operation_id VARCHAR(128), 
    correlation_id VARCHAR(128) NOT NULL, 
    outcome VARCHAR(32) NOT NULL, 
    details JSON, 
    occurred_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    PRIMARY KEY (id), 
    FOREIGN KEY(tenant_id) REFERENCES orgs (org_id) ON DELETE SET NULL
);

CREATE INDEX l4desk_audit_tenant_time_ix ON l4desk_audit_events (tenant_id, occurred_at);

CREATE INDEX l4desk_audit_correlation_ix ON l4desk_audit_events (correlation_id);

CREATE TABLE l4desk_tenant_profiles (
    tenant_id INTEGER NOT NULL, 
    timezone VARCHAR(64) NOT NULL, 
    pending_timezone VARCHAR(64), 
    timezone_effective_at TIMESTAMP WITH TIME ZONE, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    PRIMARY KEY (tenant_id), 
    CONSTRAINT l4desk_timezone_pending_ck CHECK ((pending_timezone IS NULL) = (timezone_effective_at IS NULL)), 
    FOREIGN KEY(tenant_id) REFERENCES orgs (org_id) ON DELETE RESTRICT
);

CREATE TABLE fin_balance_projections (
    tenant_id INTEGER NOT NULL, 
    account_id BIGINT NOT NULL, 
    balance_kopecks BIGINT DEFAULT '0' NOT NULL, 
    version BIGINT DEFAULT '0' NOT NULL, 
    last_transaction_id BIGINT, 
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    CONSTRAINT fin_balance_pk PRIMARY KEY (tenant_id), 
    CONSTRAINT fin_balance_rubles_ck CHECK (balance_kopecks % 100 = 0), 
    CONSTRAINT fin_balance_version_ck CHECK (version >= 0), 
    CONSTRAINT fin_balance_account_fk FOREIGN KEY(account_id) REFERENCES fin_accounts (id) ON DELETE RESTRICT, 
    CONSTRAINT fin_balance_transaction_fk FOREIGN KEY(last_transaction_id, tenant_id) REFERENCES fin_ledger_transactions (id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_balance_tenant_fk FOREIGN KEY(tenant_id) REFERENCES orgs (org_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_balance_account_uq UNIQUE (account_id)
);

CREATE TABLE fin_billing_profiles (
    tenant_id INTEGER NOT NULL, 
    anchor_at TIMESTAMP WITH TIME ZONE, 
    anchor_day INTEGER, 
    anchor_timezone VARCHAR(64), 
    first_payment_transaction_id BIGINT, 
    entitlement VARCHAR(16) DEFAULT 'free' NOT NULL, 
    entitlement_changed_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    CONSTRAINT fin_profile_pk PRIMARY KEY (tenant_id), 
    CONSTRAINT fin_profile_entitlement_ck CHECK (entitlement IN ('free', 'active', 'grace', 'blocked')), 
    CONSTRAINT fin_profile_paid_ck CHECK (entitlement NOT IN ('active', 'grace') OR anchor_at IS NOT NULL), 
    CONSTRAINT fin_profile_anchor_ck CHECK ((anchor_at IS NULL AND anchor_day IS NULL AND anchor_timezone IS NULL AND first_payment_transaction_id IS NULL) OR (anchor_at IS NOT NULL AND anchor_day IS NOT NULL AND anchor_timezone IS NOT NULL AND first_payment_transaction_id IS NOT NULL)), 
    CONSTRAINT fin_profile_anchor_day_ck CHECK (anchor_day BETWEEN 1 AND 31), 
    CONSTRAINT fin_profile_first_payment_fk FOREIGN KEY(first_payment_transaction_id, tenant_id) REFERENCES fin_ledger_transactions (id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_profile_tenant_fk FOREIGN KEY(tenant_id) REFERENCES orgs (org_id) ON DELETE RESTRICT
);

CREATE TABLE fin_ledger_entries (
    id BIGSERIAL NOT NULL, 
    transaction_id BIGINT NOT NULL, 
    tenant_id INTEGER NOT NULL, 
    line_number INTEGER NOT NULL, 
    account_id BIGINT NOT NULL, 
    debit_kopecks BIGINT DEFAULT '0' NOT NULL, 
    credit_kopecks BIGINT DEFAULT '0' NOT NULL, 
    CONSTRAINT fin_entry_pk PRIMARY KEY (id), 
    CONSTRAINT fin_entry_side_ck CHECK ((debit_kopecks > 0 AND credit_kopecks = 0) OR (credit_kopecks > 0 AND debit_kopecks = 0)), 
    CONSTRAINT fin_entry_rubles_ck CHECK (debit_kopecks % 100 = 0 AND credit_kopecks % 100 = 0), 
    CONSTRAINT fin_entry_line_ck CHECK (line_number > 0), 
    CONSTRAINT fin_entry_account_fk FOREIGN KEY(account_id) REFERENCES fin_accounts (id) ON DELETE RESTRICT, 
    CONSTRAINT fin_entry_transaction_fk FOREIGN KEY(transaction_id, tenant_id) REFERENCES fin_ledger_transactions (id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_entry_line_uq UNIQUE (transaction_id, line_number)
);

CREATE INDEX fin_entry_account_transaction_ix ON fin_ledger_entries (account_id, transaction_id);

CREATE TABLE fin_manual_payments (
    id BIGSERIAL NOT NULL, 
    tenant_id INTEGER NOT NULL, 
    operation_id VARCHAR(128) NOT NULL, 
    amount_kopecks BIGINT NOT NULL, 
    received_on DATE NOT NULL, 
    document_number VARCHAR(128) NOT NULL, 
    purpose TEXT NOT NULL, 
    payer VARCHAR(500) NOT NULL, 
    comment TEXT, 
    evidence_reference TEXT, 
    created_by_user_id INTEGER NOT NULL, 
    ledger_transaction_id BIGINT NOT NULL, 
    correlation_id VARCHAR(128) NOT NULL, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    CONSTRAINT fin_manual_pk PRIMARY KEY (id), 
    CONSTRAINT fin_manual_amount_ck CHECK (amount_kopecks > 0 AND amount_kopecks % 100 = 0), 
    CONSTRAINT fin_manual_creator_fk FOREIGN KEY(created_by_user_id) REFERENCES users (id) ON DELETE RESTRICT, 
    CONSTRAINT fin_manual_transaction_fk FOREIGN KEY(ledger_transaction_id, tenant_id) REFERENCES fin_ledger_transactions (id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_manual_transaction_uq UNIQUE (ledger_transaction_id), 
    CONSTRAINT fin_manual_operation_uq UNIQUE (tenant_id, operation_id)
);

CREATE INDEX fin_manual_tenant_date_ix ON fin_manual_payments (tenant_id, received_on);

CREATE TABLE fin_payments (
    id BIGSERIAL NOT NULL, 
    tenant_id INTEGER NOT NULL, 
    operation_id VARCHAR(128) NOT NULL, 
    provider VARCHAR(32) DEFAULT 'yookassa' NOT NULL, 
    provider_payment_id VARCHAR(128), 
    status VARCHAR(32) DEFAULT 'pending' NOT NULL, 
    amount_kopecks BIGINT NOT NULL, 
    currency VARCHAR(3) DEFAULT 'RUB' NOT NULL, 
    confirmation_url TEXT, 
    provider_receipt_id VARCHAR(128), 
    receipt_status VARCHAR(32), 
    receipt_snapshot JSON, 
    verified_at TIMESTAMP WITH TIME ZONE, 
    succeeded_at TIMESTAMP WITH TIME ZONE, 
    ledger_transaction_id BIGINT, 
    actor VARCHAR(128) NOT NULL, 
    correlation_id VARCHAR(128) NOT NULL, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    CONSTRAINT fin_payment_pk PRIMARY KEY (id), 
    CONSTRAINT fin_payment_amount_ck CHECK (amount_kopecks > 0 AND amount_kopecks % 100 = 0 AND currency = 'RUB'), 
    CONSTRAINT fin_payment_posting_ck CHECK (ledger_transaction_id IS NULL OR status = 'succeeded'), 
    CONSTRAINT fin_payment_verified_ck CHECK (status != 'succeeded' OR (verified_at IS NOT NULL AND succeeded_at IS NOT NULL AND provider_payment_id IS NOT NULL)), 
    CONSTRAINT fin_payment_status_ck CHECK (status IN ('pending', 'waiting_for_capture', 'succeeded', 'canceled')), 
    CONSTRAINT fin_payment_transaction_fk FOREIGN KEY(ledger_transaction_id, tenant_id) REFERENCES fin_ledger_transactions (id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_payment_tenant_fk FOREIGN KEY(tenant_id) REFERENCES orgs (org_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_payment_transaction_uq UNIQUE (ledger_transaction_id), 
    CONSTRAINT fin_payment_provider_uq UNIQUE (provider, provider_payment_id), 
    CONSTRAINT fin_payment_operation_uq UNIQUE (tenant_id, operation_id)
);

CREATE INDEX fin_payment_status_time_ix ON fin_payments (status, created_at);

CREATE INDEX fin_payment_tenant_time_ix ON fin_payments (tenant_id, created_at);

CREATE TABLE l4desk_memberships (
    tenant_id INTEGER NOT NULL, 
    user_id INTEGER NOT NULL, 
    role_id INTEGER DEFAULT '5' NOT NULL, 
    is_owner BOOLEAN DEFAULT 'false' NOT NULL, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    PRIMARY KEY (tenant_id, user_id), 
    CONSTRAINT l4desk_membership_role_ck CHECK (role_id = 5), 
    FOREIGN KEY(tenant_id) REFERENCES orgs (org_id) ON DELETE RESTRICT, 
    FOREIGN KEY(user_id) REFERENCES users (id) ON DELETE RESTRICT
);

CREATE INDEX l4desk_membership_user_ix ON l4desk_memberships (user_id);

CREATE UNIQUE INDEX l4desk_membership_owner_uq ON l4desk_memberships (tenant_id) WHERE is_owner;

CREATE TABLE l4desk_registrations (
    id BIGSERIAL NOT NULL, 
    email_normalized VARCHAR(255) NOT NULL, 
    password_hash VARCHAR(255) NOT NULL, 
    token_hash VARCHAR(64) NOT NULL, 
    terms_version VARCHAR(64) NOT NULL, 
    timezone VARCHAR(64) NOT NULL, 
    source VARCHAR(128), 
    correlation_id VARCHAR(128) NOT NULL, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    expires_at TIMESTAMP WITH TIME ZONE NOT NULL, 
    consumed_at TIMESTAMP WITH TIME ZONE, 
    user_id INTEGER, 
    tenant_id INTEGER, 
    PRIMARY KEY (id), 
    CONSTRAINT l4desk_registration_consumed_ck CHECK (consumed_at IS NULL OR consumed_at >= created_at), 
    CONSTRAINT l4desk_registration_expiry_ck CHECK (expires_at > created_at), 
    FOREIGN KEY(tenant_id) REFERENCES orgs (org_id) ON DELETE SET NULL, 
    FOREIGN KEY(user_id) REFERENCES users (id) ON DELETE SET NULL, 
    UNIQUE (email_normalized), 
    UNIQUE (token_hash)
);

CREATE INDEX l4desk_registration_expiry_ix ON l4desk_registrations (expires_at);

CREATE TABLE l4desk_terminals (
    terminal_id INTEGER NOT NULL, 
    tenant_id INTEGER NOT NULL, 
    runtime_terminal_id INTEGER, 
    ordinal BIGINT NOT NULL, 
    sn VARCHAR(128) NOT NULL, 
    external_terminal_id VARCHAR(128) NOT NULL, 
    device_id INTEGER, 
    operation_id VARCHAR(128) NOT NULL, 
    correlation_id VARCHAR(128) NOT NULL, 
    provisioning_state VARCHAR(32) DEFAULT 'pending' NOT NULL, 
    pin_state VARCHAR(32) DEFAULT 'pending' NOT NULL, 
    certificate_reference VARCHAR(128), 
    last_error VARCHAR(500), 
    first_online_at TIMESTAMP WITH TIME ZONE, 
    last_online_at TIMESTAMP WITH TIME ZONE, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    deleted_at TIMESTAMP WITH TIME ZONE, 
    PRIMARY KEY (terminal_id), 
    CONSTRAINT l4desk_terminal_pin_ck CHECK (pin_state IN ('pending', 'issued', 'consumed', 'expired', 'failed')), 
    CONSTRAINT l4desk_terminal_provisioning_ck CHECK (provisioning_state IN ('pending', 'ready', 'failed')), 
    CONSTRAINT l4desk_terminal_ordinal_ck CHECK (ordinal > 0), 
    FOREIGN KEY(runtime_terminal_id) REFERENCES terminals (id) ON DELETE SET NULL, 
    FOREIGN KEY(tenant_id) REFERENCES orgs (org_id) ON DELETE RESTRICT, 
    UNIQUE (operation_id), 
    UNIQUE (runtime_terminal_id), 
    CONSTRAINT l4desk_terminal_external_uq UNIQUE (tenant_id, external_terminal_id), 
    CONSTRAINT l4desk_terminal_ordinal_uq UNIQUE (tenant_id, ordinal), 
    CONSTRAINT l4desk_terminal_tenant_uq UNIQUE (terminal_id, tenant_id)
);

CREATE INDEX l4desk_terminal_correlation_ix ON l4desk_terminals (correlation_id);

CREATE TABLE fin_billing_cycles (
    id BIGSERIAL NOT NULL, 
    tenant_id INTEGER NOT NULL, 
    sequence INTEGER NOT NULL, 
    starts_at TIMESTAMP WITH TIME ZONE NOT NULL, 
    ends_at TIMESTAMP WITH TIME ZONE NOT NULL, 
    grace_deadline TIMESTAMP WITH TIME ZONE NOT NULL, 
    timezone VARCHAR(64) NOT NULL, 
    closed_at TIMESTAMP WITH TIME ZONE, 
    CONSTRAINT fin_cycle_pk PRIMARY KEY (id), 
    CONSTRAINT fin_cycle_sequence_ck CHECK (sequence >= 0), 
    CONSTRAINT fin_cycle_interval_ck CHECK (starts_at < grace_deadline AND grace_deadline < ends_at), 
    CONSTRAINT fin_cycle_profile_fk FOREIGN KEY(tenant_id) REFERENCES fin_billing_profiles (tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_cycle_tenant_uq UNIQUE (id, tenant_id), 
    CONSTRAINT fin_cycle_sequence_uq UNIQUE (tenant_id, sequence), 
    CONSTRAINT fin_cycle_start_uq UNIQUE (tenant_id, starts_at)
);

CREATE INDEX fin_cycle_end_ix ON fin_billing_cycles (ends_at);

CREATE TABLE fin_usage_daily (
    id BIGSERIAL NOT NULL, 
    tenant_id INTEGER NOT NULL, 
    terminal_id INTEGER NOT NULL, 
    local_date DATE NOT NULL, 
    timezone VARCHAR(64) NOT NULL, 
    tariff_version_id BIGINT NOT NULL, 
    source_seconds BIGINT NOT NULL, 
    video_seconds BIGINT NOT NULL, 
    console_seconds BIGINT NOT NULL, 
    free_seconds BIGINT NOT NULL, 
    billable_seconds BIGINT NOT NULL, 
    rounded_billable_hours INTEGER NOT NULL, 
    rate_kopecks BIGINT NOT NULL, 
    calculated_kopecks BIGINT NOT NULL, 
    posted_kopecks BIGINT NOT NULL, 
    discarded_kopecks BIGINT NOT NULL, 
    source_project VARCHAR(64) NOT NULL, 
    source_event_id VARCHAR(128), 
    source_events_hash VARCHAR(64) NOT NULL, 
    archive_batch_id VARCHAR(128), 
    ledger_transaction_id BIGINT, 
    actor VARCHAR(128) NOT NULL, 
    correlation_id VARCHAR(128) NOT NULL, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    posted_at TIMESTAMP WITH TIME ZONE, 
    CONSTRAINT fin_usage_pk PRIMARY KEY (id), 
    CONSTRAINT fin_usage_posted_ck CHECK ((posted_at IS NULL) = (ledger_transaction_id IS NULL)), 
    CONSTRAINT fin_usage_rounding_ck CHECK (posted_kopecks >= 0 AND calculated_kopecks = posted_kopecks + discarded_kopecks AND discarded_kopecks >= 0 AND discarded_kopecks < 100 AND posted_kopecks % 100 = 0), 
    CONSTRAINT fin_usage_seconds_ck CHECK (source_seconds = video_seconds + console_seconds AND source_seconds = free_seconds + billable_seconds), 
    CONSTRAINT fin_usage_values_ck CHECK (video_seconds >= 0 AND console_seconds >= 0 AND free_seconds >= 0 AND billable_seconds >= 0 AND rounded_billable_hours >= 0 AND rate_kopecks >= 0), 
    CONSTRAINT fin_usage_transaction_fk FOREIGN KEY(ledger_transaction_id, tenant_id) REFERENCES fin_ledger_transactions (id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_usage_tariff_fk FOREIGN KEY(tariff_version_id) REFERENCES fin_tariff_versions (id) ON DELETE RESTRICT, 
    CONSTRAINT fin_usage_terminal_fk FOREIGN KEY(terminal_id, tenant_id) REFERENCES l4desk_terminals (terminal_id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_usage_transaction_uq UNIQUE (ledger_transaction_id), 
    CONSTRAINT fin_usage_day_uq UNIQUE (terminal_id, local_date)
);

CREATE INDEX fin_usage_pending_ix ON fin_usage_daily (local_date) WHERE posted_at IS NULL;

CREATE INDEX fin_usage_tenant_date_ix ON fin_usage_daily (tenant_id, local_date);

CREATE TABLE l4desk_remote_sessions (
    id BIGSERIAL NOT NULL, 
    tenant_id INTEGER NOT NULL, 
    terminal_id INTEGER NOT NULL, 
    operation_id VARCHAR(128) NOT NULL, 
    correlation_id VARCHAR(128) NOT NULL, 
    provider_session_id VARCHAR(128), 
    requested_by_user_id INTEGER, 
    session_type VARCHAR(32) NOT NULL, 
    state VARCHAR(32) DEFAULT 'reserved' NOT NULL, 
    requested_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    active_at TIMESTAMP WITH TIME ZONE, 
    closed_at TIMESTAMP WITH TIME ZONE, 
    reason VARCHAR(128), 
    last_event_id VARCHAR(128), 
    last_cursor BIGINT DEFAULT '0' NOT NULL, 
    source_events_hash VARCHAR(64), 
    PRIMARY KEY (id), 
    CONSTRAINT l4desk_session_type_ck CHECK (session_type IN ('console', 'video')), 
    CONSTRAINT l4desk_session_active_ck CHECK (state != 'active' OR active_at IS NOT NULL), 
    CONSTRAINT l4desk_session_state_ck CHECK (state IN ('reserved', 'start_requested', 'active', 'stop_requested', 'closed', 'failed')), 
    CONSTRAINT l4desk_session_closed_ck CHECK (state NOT IN ('closed', 'failed') OR closed_at IS NOT NULL), 
    CONSTRAINT l4desk_session_interval_ck CHECK (closed_at IS NULL OR active_at IS NULL OR closed_at >= active_at), 
    CONSTRAINT l4desk_session_cursor_ck CHECK (last_cursor >= 0), 
    FOREIGN KEY(requested_by_user_id) REFERENCES users (id) ON DELETE SET NULL, 
    CONSTRAINT l4desk_session_terminal_fk FOREIGN KEY(terminal_id, tenant_id) REFERENCES l4desk_terminals (terminal_id, tenant_id) ON DELETE RESTRICT, 
    UNIQUE (provider_session_id), 
    CONSTRAINT l4desk_session_operation_uq UNIQUE (tenant_id, operation_id)
);

CREATE INDEX l4desk_session_tenant_time_ix ON l4desk_remote_sessions (tenant_id, requested_at);

CREATE INDEX l4desk_session_correlation_ix ON l4desk_remote_sessions (correlation_id);

CREATE UNIQUE INDEX l4desk_session_reservation_uq ON l4desk_remote_sessions (terminal_id) WHERE state IN ('reserved', 'start_requested', 'active', 'stop_requested');

CREATE INDEX l4desk_session_state_ix ON l4desk_remote_sessions (state);

CREATE TABLE fin_notification_deliveries (
    id BIGSERIAL NOT NULL, 
    tenant_id INTEGER NOT NULL, 
    billing_cycle_id BIGINT NOT NULL, 
    notification_type VARCHAR(32) NOT NULL, 
    scheduled_at TIMESTAMP WITH TIME ZONE NOT NULL, 
    status VARCHAR(16) DEFAULT 'pending' NOT NULL, 
    attempts INTEGER DEFAULT '0' NOT NULL, 
    sent_at TIMESTAMP WITH TIME ZONE, 
    provider_message_id VARCHAR(128), 
    last_error VARCHAR(500), 
    correlation_id VARCHAR(128) NOT NULL, 
    CONSTRAINT fin_notification_pk PRIMARY KEY (id), 
    CONSTRAINT fin_notification_type_ck CHECK (notification_type IN ('cycle_minus_7', 'cycle_minus_3', 'cycle_minus_1', 'grace', 'blocked')), 
    CONSTRAINT fin_notification_sent_ck CHECK (status != 'sent' OR sent_at IS NOT NULL), 
    CONSTRAINT fin_notification_status_ck CHECK (status IN ('pending', 'sending', 'sent', 'failed') AND attempts >= 0), 
    CONSTRAINT fin_notification_cycle_fk FOREIGN KEY(billing_cycle_id, tenant_id) REFERENCES fin_billing_cycles (id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_notification_cycle_type_uq UNIQUE (tenant_id, billing_cycle_id, notification_type)
);

CREATE INDEX fin_notification_due_ix ON fin_notification_deliveries (status, scheduled_at);

CREATE TABLE fin_terminal_monthly_charges (
    id BIGSERIAL NOT NULL, 
    tenant_id INTEGER NOT NULL, 
    terminal_id INTEGER NOT NULL, 
    billing_cycle_id BIGINT NOT NULL, 
    tariff_version_id BIGINT NOT NULL, 
    is_free BOOLEAN NOT NULL, 
    first_online_at TIMESTAMP WITH TIME ZONE NOT NULL, 
    calculated_kopecks BIGINT NOT NULL, 
    posted_kopecks BIGINT NOT NULL, 
    discarded_kopecks BIGINT NOT NULL, 
    source_project VARCHAR(64) NOT NULL, 
    source_event_id VARCHAR(128) NOT NULL, 
    source_events_hash VARCHAR(64) NOT NULL, 
    archive_batch_id VARCHAR(128), 
    ledger_transaction_id BIGINT, 
    actor VARCHAR(128) NOT NULL, 
    correlation_id VARCHAR(128) NOT NULL, 
    created_at TIMESTAMP WITH TIME ZONE DEFAULT now() NOT NULL, 
    posted_at TIMESTAMP WITH TIME ZONE, 
    CONSTRAINT fin_monthly_pk PRIMARY KEY (id), 
    CONSTRAINT fin_monthly_posted_ck CHECK ((posted_at IS NULL) = (ledger_transaction_id IS NULL)), 
    CONSTRAINT fin_monthly_free_ck CHECK (NOT is_free OR calculated_kopecks = 0), 
    CONSTRAINT fin_monthly_rounding_ck CHECK (posted_kopecks >= 0 AND calculated_kopecks = posted_kopecks + discarded_kopecks AND discarded_kopecks >= 0 AND discarded_kopecks < 100 AND posted_kopecks % 100 = 0), 
    CONSTRAINT fin_monthly_cycle_fk FOREIGN KEY(billing_cycle_id, tenant_id) REFERENCES fin_billing_cycles (id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_monthly_transaction_fk FOREIGN KEY(ledger_transaction_id, tenant_id) REFERENCES fin_ledger_transactions (id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_monthly_tariff_fk FOREIGN KEY(tariff_version_id) REFERENCES fin_tariff_versions (id) ON DELETE RESTRICT, 
    CONSTRAINT fin_monthly_terminal_fk FOREIGN KEY(terminal_id, tenant_id) REFERENCES l4desk_terminals (terminal_id, tenant_id) ON DELETE RESTRICT, 
    CONSTRAINT fin_monthly_transaction_uq UNIQUE (ledger_transaction_id), 
    CONSTRAINT fin_monthly_terminal_cycle_uq UNIQUE (terminal_id, billing_cycle_id)
);

CREATE INDEX fin_monthly_tenant_cycle_ix ON fin_terminal_monthly_charges (tenant_id, billing_cycle_id);

UPDATE alembic_version SET version_num='027' WHERE alembic_version.version_num = '026';

COMMIT;

