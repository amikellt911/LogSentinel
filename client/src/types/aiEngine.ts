export interface TokenMetrics {
  totalTokens: number;
  savingsPercentage: number;
  estimatedCost: number;
  avgTokensPerBatch: number;
}

export interface BatchArchiveItem {
  id: string; // trace_id or batch_id
  createdAt: string;
  logCount: number;
  riskLevel: 'CRITICAL' | 'ERROR' | 'WARNING' | 'INFO' | 'SAFE';
  tokenCount: number;
  avgLatency: number;
  summary: string;
  tags: string[];
}

export interface PromptDebugInfo {
  batchId: string;
  input: {
    trace_context: any;
    constraint: string;
    system_prompt: string;
  };
  output: {
    risk_level: string;
    summary: string;
    root_cause: string;
    solution: string;
  };
  meta: {
    timestamp: string;
    model: string;
    latency: number;
    tokens: number;
  };
}
