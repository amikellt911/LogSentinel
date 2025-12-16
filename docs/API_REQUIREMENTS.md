# API Requirements and Contract

This document outlines the API endpoints required by the frontend client and details the existing backend implementation.

## Backend Base URL
Assume `http://localhost:8080` (or configured via environment/proxy).

## Endpoints

### 1. Dashboard Stats
**Endpoint:** `GET /dashboard`

**Description:** Returns aggregated statistics for the dashboard.

**Backend Implementation (`DashboardStats`):**
```json
{
  "total_logs": 12345,
  "high_risk": 5,
  "medium_risk": 10,
  "low_risk": 20,
  "info_risk": 100,
  "unknown_risk": 0,
  "avg_response_time": 0.45,
  "recent_alerts": [
    {
      "trace_id": "uuid-1",
      "summary": "SQL Injection detected",
      "time": "2023-10-27 10:00:00"
    }
  ]
}
```

**Frontend Requirements (Mapping):**
- `totalLogsProcessed` -> `total_logs`
- `netLatency` -> `avg_response_time` (partially maps, frontend expects ms)
- `riskStats`:
    - `Critical` -> `high_risk`
    - `Error` -> `medium_risk`
    - `Warning` -> `low_risk`
    - `Info` -> `info_risk`
    - `Safe` -> (Calculated: `total_logs` - sum(risks))
- `recentAlerts` -> `recent_alerts`

**Missing/Gaps:**
- **System Metrics:** The frontend displays Server Memory Usage, Queue Depth/Backpressure status. These are currently not provided by the backend.
- **AI Metrics:** `aiLatency`, `aiTriggerCount`. These are not in `DashboardStats`.
- **Time-Series Chart:** The frontend displays a real-time chart of QPS (Ingest Rate) and AI Rate. The backend currently only returns a snapshot.

### 2. Historical Logs
**Endpoint:** `GET /history`
**Query Params:** `?page=1&pageSize=10`

**Backend Implementation (`HistoryPage`):**
```json
{
  "logs": [
    {
      "trace_id": "uuid-123",
      "risk_level": "Critical",
      "summary": "Detected malicious payload",
      "processed_at": "2023-10-27 10:05:00"
    }
  ],
  "total_count": 100
}
```

**Frontend Requirements (Mapping):**
- `logs` array in Store.
- Frontend Item Structure:
  ```ts
  interface LogEntry {
      id: number | string
      timestamp: string
      level: 'INFO' | 'WARN' | 'RISK'
      message: string
  }
  ```
- Mapping:
    - `id` -> `trace_id`
    - `timestamp` -> `processed_at`
    - `level` -> `risk_level` (needs normalization, backend returns string like "Critical", frontend expects specific enum)
    - `message` -> `summary`

### 3. Log Submission
**Endpoint:** `POST /logs`

**Request Body:**
```json
{
    "trace_id": "optional-client-trace-id",
    "body_": "Raw log content..."
}
```

**Response:**
```json
{
    "trace_id": "uuid-generated-or-echoed"
}
```

### 4. Analysis Result
**Endpoint:** `GET /results/:trace_id`

**Response:**
Returns the detailed analysis result for a specific log.

## Action Items for Backend (Future)
To fully support the frontend dashboard, the backend needs to provide:
1.  **System Health Metrics:** Add memory usage, CPU usage, and queue status to `GET /dashboard`.
2.  **Time Series Data:** Provide a way to fetch QPS history (e.g., last 60 seconds) for the main chart.
