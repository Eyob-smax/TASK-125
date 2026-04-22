-- Migration 008: Scheduled jobs, job runs, inter-job dependencies, and worker leases.
--
-- Job pipeline for reports: REPORT_GENERATE jobs run four in-sequence stages
-- (collect, cleanse, analyze, visualize) orchestrated by SchedulerService.
-- Each stage is a separate JobRun; completion of one triggers the next.
-- job_dependencies expresses this ordering.
--
-- Export throttling: export_csv and export_pdf jobs have max_concurrency = 1
-- to prevent simultaneous heavy I/O from freezing the UI thread.

CREATE TABLE IF NOT EXISTS scheduled_jobs (
    job_id          INTEGER PRIMARY KEY,
    name            TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    job_type        TEXT    NOT NULL CHECK(job_type IN (
                        'report_generate','export_csv','export_pdf',
                        'retention_run','alert_scan','lan_sync','backup'
                    )),
    parameters_json TEXT    NOT NULL DEFAULT '{}',
    -- cron_expression: standard 5-field cron; NULL = on-demand only
    cron_expression TEXT,
    -- 1 = highest; 10 = lowest; export jobs default to 7 (lower priority)
    priority        INTEGER NOT NULL DEFAULT 5 CHECK(priority BETWEEN 1 AND 10),
    -- max concurrent instances of this job type allowed at once
    max_concurrency INTEGER NOT NULL DEFAULT 4 CHECK(max_concurrency > 0),
    is_active       INTEGER NOT NULL DEFAULT 1,
    last_run_at     INTEGER,
    next_run_at     INTEGER,
    created_by      INTEGER REFERENCES users(user_id),
    created_at      INTEGER NOT NULL    -- IMMUTABLE
);

CREATE INDEX IF NOT EXISTS idx_jobs_type        ON scheduled_jobs(job_type);
CREATE INDEX IF NOT EXISTS idx_jobs_next_run    ON scheduled_jobs(next_run_at) WHERE is_active = 1;

CREATE TABLE IF NOT EXISTS job_runs (
    run_id          INTEGER PRIMARY KEY,
    job_id          INTEGER NOT NULL REFERENCES scheduled_jobs(job_id),
    worker_id       TEXT,               -- thread identifier; set when run starts
    started_at      INTEGER NOT NULL,
    completed_at    INTEGER,
    status          TEXT    NOT NULL DEFAULT 'queued' CHECK(status IN (
                        'queued','running','completed','failed','cancelled'
                    )),
    error_message   TEXT,
    output_json     TEXT                -- job-type-specific result payload
);

CREATE INDEX IF NOT EXISTS idx_job_runs_job     ON job_runs(job_id);
CREATE INDEX IF NOT EXISTS idx_job_runs_status  ON job_runs(status);
CREATE INDEX IF NOT EXISTS idx_job_runs_started ON job_runs(started_at);

-- Defines ordering constraints: job_id must not start until depends_on_job_id completes.
-- Used for the collect → cleanse → analyze → visualize pipeline.
CREATE TABLE IF NOT EXISTS job_dependencies (
    dependency_id       INTEGER PRIMARY KEY,
    job_id              INTEGER NOT NULL REFERENCES scheduled_jobs(job_id),
    depends_on_job_id   INTEGER NOT NULL REFERENCES scheduled_jobs(job_id),
    CHECK(job_id != depends_on_job_id),
    UNIQUE(job_id, depends_on_job_id)
);

-- Worker leases: a running worker holds a lease to claim exclusive access to a job_run.
-- SchedulerService checks is_active=1 before starting a new instance of a job.
CREATE TABLE IF NOT EXISTS worker_leases (
    lease_id        INTEGER PRIMARY KEY,
    worker_id       TEXT    NOT NULL,
    job_run_id      INTEGER NOT NULL REFERENCES job_runs(run_id),
    acquired_at     INTEGER NOT NULL,   -- IMMUTABLE
    expires_at      INTEGER NOT NULL,   -- worker must renew before expiry
    is_active       INTEGER NOT NULL DEFAULT 1
);

CREATE INDEX IF NOT EXISTS idx_leases_worker ON worker_leases(worker_id, is_active);
CREATE INDEX IF NOT EXISTS idx_leases_run    ON worker_leases(job_run_id);
CREATE INDEX IF NOT EXISTS idx_leases_active ON worker_leases(is_active, expires_at);
