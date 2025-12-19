#include <gtest/gtest.h>
#include <persistence/SqliteLogRepository.h>

class SqliteLogRepository_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        persistence_ = std::make_unique<SqliteLogRepository>(":memory:");
    }

    void TearDown() override
    {
    }

    // 辅助函数：检查数据库中是否存在某条日志
    bool logExists(const std::string &trace_id)
    {
        const char* sql_select_log=R"(
            select log_content from raw_logs where trace_id = ?;
        )";
        sqlite3_stmt* stmt=nullptr;
        int rc=sqlite3_prepare_v2(persistence_->db_,sql_select_log,-1,&stmt,0);
        if(rc!=SQLITE_OK) return false;
        sqlite3_bind_text(stmt,1,trace_id.c_str(),-1,SQLITE_TRANSIENT);
        rc=sqlite3_step(stmt);
        if(rc!=SQLITE_ROW) return false;
        sqlite3_finalize(stmt);
        return true; 
    }

    std::string getLog(const std::string &trace_id)
    {
        const char* sql_select_log=R"(
            select log_content from raw_logs where trace_id = ?;
        )";
        sqlite3_stmt* stmt=nullptr;
        int rc=sqlite3_prepare_v2(persistence_->db_,sql_select_log,-1,&stmt,0);
        if(rc!=SQLITE_OK) return "";
        sqlite3_bind_text(stmt,1,trace_id.c_str(),-1,SQLITE_TRANSIENT);
        rc=sqlite3_step(stmt);
        if(rc!=SQLITE_ROW) return "";
        return reinterpret_cast<const char*>(sqlite3_column_text(stmt,0));
    }

    std::unique_ptr<SqliteLogRepository> persistence_;
};


//测试构造函数，是否成功构造了一个内存数据库
TEST_F(SqliteLogRepository_test, test_constructor)
{
    EXPECT_NE(persistence_,nullptr);
}

//测试保存日志功能
TEST_F(SqliteLogRepository_test, test_save_log)
{
    std::string trace_id="test_trace_id";
    std::string log_content="test log content";
    ASSERT_NO_THROW(persistence_->SaveRawLog(trace_id,log_content));
    EXPECT_TRUE(logExists(trace_id));
    EXPECT_EQ(getLog(trace_id),log_content);
}

//测试保存日志功能，保存多个日志
TEST_F(SqliteLogRepository_test, test_save_multiple_logs)
{
    std::string trace_id1="test_trace_id1";
    std::string log_content1="test log content1";
    ASSERT_NO_THROW(persistence_->SaveRawLog(trace_id1,log_content1));
    EXPECT_TRUE(logExists(trace_id1));
    EXPECT_EQ(getLog(trace_id1),log_content1);
    std::string trace_id2="test_trace_id2";
    std::string log_content2="test log content2";
    ASSERT_NO_THROW(persistence_->SaveRawLog(trace_id2,log_content2));
    EXPECT_TRUE(logExists(trace_id2));
    EXPECT_EQ(getLog(trace_id2),log_content2);
    std::string trace_id3="test_trace_id3";
    std::string log_content3="test log content3";
    ASSERT_NO_THROW(persistence_->SaveRawLog(trace_id3,log_content3));
    EXPECT_TRUE(logExists(trace_id3));
    EXPECT_EQ(getLog(trace_id3),log_content3); 
}

//测试插入重复trace_id导致抛出runtime_error
TEST_F(SqliteLogRepository_test, test_save_duplicate_trace_id)
{
    std::string trace_id="test_trace_id";
    std::string log_content1="test log content1";
    std::string log_content2="test log content2";
    ASSERT_NO_THROW(persistence_->SaveRawLog(trace_id,log_content1));
    EXPECT_TRUE(logExists(trace_id));
    EXPECT_EQ(getLog(trace_id),log_content1);
    ASSERT_THROW(persistence_->SaveRawLog(trace_id,log_content2),std::runtime_error);
    EXPECT_TRUE(logExists(trace_id));
}

// 测试构造函数失败路径：尝试在一个没有权限的目录创建数据库
TEST(SqliteLogRepositoryConstructorTest, ThrowsOnInvalidPath) {
    // 注意：这个测试不能用 Test Fixture，因为它测试的就是构造函数本身
    // 这个测试在 Docker 或 CI 环境中可能需要特殊设置才能运行
    #ifdef __linux__
    const std::string invalid_path = "/root/no_permission/test.db";
    //一个非常劣性的bug
    //必须加一个括号来解决
    //ASSERT_THROW((SqliteLogRepository(invalid_path)), std::runtime_error);
    ASSERT_THROW(SqliteLogRepository{invalid_path}, std::runtime_error);
    #endif
}
TEST_F(SqliteLogRepository_test, test_get_historical_logs_filter)
{
    // 1. Setup Data
    std::string trace1 = "t1";
    LogAnalysisResult r1; r1.risk_level = RiskLevel::CRITICAL; r1.summary = "CritLog";
    persistence_->SaveRawLog(trace1, "raw1");
    persistence_->saveAnalysisResult(trace1, r1, 10, "SUCCESS");

    std::string trace2 = "t2";
    LogAnalysisResult r2; r2.risk_level = RiskLevel::WARNING; r2.summary = "WarnLog";
    persistence_->SaveRawLog(trace2, "raw2");
    persistence_->saveAnalysisResult(trace2, r2, 10, "SUCCESS");

    std::string trace3 = "t3";
    LogAnalysisResult r3; r3.risk_level = RiskLevel::INFO; r3.summary = "InfoLog";
    persistence_->SaveRawLog(trace3, "raw3");
    persistence_->saveAnalysisResult(trace3, r3, 10, "SUCCESS");

    // 2. Test Filters
    // Case 1: Filter "Critical" (Title Case) -> Should find t1
    auto page1 = persistence_->getHistoricalLogs(1, 10, "Critical", "");
    EXPECT_EQ(page1.total_count, 1);
    ASSERT_EQ(page1.logs.size(), 1);
    EXPECT_EQ(page1.logs[0].trace_id, trace1);

    // Case 2: Filter "warning" (lowercase) -> Should find t2
    auto page2 = persistence_->getHistoricalLogs(1, 10, "warning", "");
    EXPECT_EQ(page2.total_count, 1);
    ASSERT_EQ(page2.logs.size(), 1);
    EXPECT_EQ(page2.logs[0].trace_id, trace2);

    // Case 3: Filter "Info" -> Should find t3
    auto page3 = persistence_->getHistoricalLogs(1, 10, "Info", "");
    EXPECT_EQ(page3.total_count, 1);
    ASSERT_EQ(page3.logs.size(), 1);
    EXPECT_EQ(page3.logs[0].trace_id, trace3);

    // Case 4: Filter "Safe" -> Should be empty
    auto page4 = persistence_->getHistoricalLogs(1, 10, "Safe", "");
    EXPECT_EQ(page4.total_count, 0);

    // Case 5: Empty filter -> All 3
    auto page5 = persistence_->getHistoricalLogs(1, 10, "", "");
    EXPECT_EQ(page5.total_count, 3);
}
