# TRACE_SCHEMA

以下为 SQLite 的最小建表语句占位，用于 TraceExplorer 的列表与详情查询。

```sql
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS trace_summary (
  trace_id TEXT PRIMARY KEY,
  service_name TEXT NOT NULL,
  start_time_ms INTEGER NOT NULL,
  end_time_ms INTEGER,
  duration_ms INTEGER NOT NULL,
  span_count INTEGER NOT NULL,
  token_count INTEGER NOT NULL,
  risk_level TEXT NOT NULL,
  tags_json TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS trace_span (
  trace_id TEXT NOT NULL,
  span_id TEXT NOT NULL,
  parent_id TEXT,
  service_name TEXT NOT NULL,
  operation TEXT NOT NULL,
  start_time_ms INTEGER NOT NULL,
  duration_ms INTEGER NOT NULL,
  status TEXT NOT NULL,
  attributes_json TEXT NOT NULL,
  PRIMARY KEY (trace_id, span_id),
  FOREIGN KEY (trace_id) REFERENCES trace_summary(trace_id)
);

CREATE TABLE IF NOT EXISTS trace_analysis (
  trace_id TEXT PRIMARY KEY,
  risk_level TEXT NOT NULL,
  summary TEXT NOT NULL,
  root_cause TEXT NOT NULL,
  solution TEXT NOT NULL,
  confidence REAL NOT NULL,
  tags_json TEXT NOT NULL,
  FOREIGN KEY (trace_id) REFERENCES trace_summary(trace_id)
);

CREATE TABLE IF NOT EXISTS prompt_debug (
  trace_id TEXT PRIMARY KEY,
  input_json TEXT NOT NULL,
  output_json TEXT NOT NULL,
  model TEXT NOT NULL,
  duration_ms INTEGER NOT NULL,
  total_tokens INTEGER NOT NULL,
  timestamp TEXT NOT NULL,
  FOREIGN KEY (trace_id) REFERENCES trace_summary(trace_id)
);

-- 最小索引（按常见查询维度）
CREATE INDEX IF NOT EXISTS idx_trace_summary_start_time
  ON trace_summary(start_time_ms);
CREATE INDEX IF NOT EXISTS idx_trace_summary_service_name
  ON trace_summary(service_name);
```
