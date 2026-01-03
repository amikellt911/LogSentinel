#pragma once
#include<sqlite3.h>
#include<string>
#include<mutex>
#include<optional>
#include "persistence/SqliteHelper.h"
#include <persistence/ConfigTypes.h>
class SqliteLogRepository
{
public:
    SqliteLogRepository(const std::string& db_path);
    ~SqliteLogRepository();
    void SaveRawLog(const std::string& trace_id,const std::string& raw_log);
    // 未来要用的方法，可以先写个空实现
    void saveAnalysisResult(const std::string& trace_id, const LogAnalysisResult& result,int response_time_ms,const std::string& status);
    void saveAnalysisResult(const std::string &trace_id, const std::string &result, const std::string &status);
    void saveAnalysisResultError(const std::string& trace_id, const std::string& error_msg);
    // std::string queryResultByTraceId(const std::string& trace_id);
    std::optional<std::string> queryResultByTraceId(const std::string& trace_id);
    std::optional<LogAnalysisResult> queryStructResultByTraceId(const std::string& trace_id);
    DashboardStats getDashboardStats();

    // 批量保存原始日志 (BEGIN TRANSACTION ... COMMIT)
    void saveRawLogBatch(const std::vector<std::pair<std::string, std::string>>& logs);

    // 新增：保存批次宏观总结 (返回 batch_id)
    int64_t saveBatchSummary(
        const std::string& global_summary,
        const std::string& global_risk_level,
        const std::string& key_patterns,
        const DashboardStats& stats,
        int processing_time_ms
    );

    // 批量保存分析结果 (更新：增加 batch_id, 并触发内存快照更新)
    void saveAnalysisResultBatch(const std::vector<AnalysisResultItem>& items, int64_t batch_id);

    // 更新实时指标 (QPS, Backpressure)
    void updateRealtimeMetrics(double qps, double backpressure);

    HistoryPage getHistoricalLogs(int page, int pageSize, const std::string& level = "", const std::string& keyword = "");
private:
    sqlite3* db_=nullptr;
    std::mutex mutex_; // 保护 DB 访问

    // --- 内存快照 (The Snapshot) ---
    std::shared_ptr<const DashboardStats> cached_stats_;
    mutable std::mutex stats_mutex_; // 保护指针交换

    // 启动时从数据库重建缓存
    void rebuildStatsFromDb();

    friend class SqliteLogRepository_test;
};


