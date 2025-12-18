#include <gtest/gtest.h>
#include <persistence/SqliteLogRepository.h>

class SqliteLogRepository_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        persistence_ = std::make_unique<SqliteLogRepository>("/tmp/test_sqlite_repo.db");
    }

    void TearDown() override
    {
        persistence_.reset();
        std::remove("/tmp/test_sqlite_repo.db");
        std::remove("/tmp/test_sqlite_repo.db-shm");
        std::remove("/tmp/test_sqlite_repo.db-wal");
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
    std::string trace_id="test_trace_id_unique_1765962960523897245";
    std::string log_content="test log content";
    ASSERT_NO_THROW(persistence_->SaveRawLog(trace_id,log_content));
    EXPECT_TRUE(logExists(trace_id));
    EXPECT_EQ(getLog(trace_id),log_content);
}

//测试保存日志功能，保存多个日志
TEST_F(SqliteLogRepository_test, test_save_multiple_logs)
{
    std::string trace_id1="test_trace_id1_unique_1765963000969212346";
    std::string log_content1="test log content1";
    ASSERT_NO_THROW(persistence_->SaveRawLog(trace_id1,log_content1));
    EXPECT_TRUE(logExists(trace_id1));
    EXPECT_EQ(getLog(trace_id1),log_content1);
    std::string trace_id2="test_trace_id2_unique_1765963000975514267";
    std::string log_content2="test log content2";
    ASSERT_NO_THROW(persistence_->SaveRawLog(trace_id2,log_content2));
    EXPECT_TRUE(logExists(trace_id2));
    EXPECT_EQ(getLog(trace_id2),log_content2);
    std::string trace_id3="test_trace_id3_unique_1765963000981685961";
    std::string log_content3="test log content3";
    ASSERT_NO_THROW(persistence_->SaveRawLog(trace_id3,log_content3));
    EXPECT_TRUE(logExists(trace_id3));
    EXPECT_EQ(getLog(trace_id3),log_content3); 
}

//测试插入重复trace_id导致抛出runtime_error
// NOTE: I commented out the initial ASSERT_NO_THROW because it was failing.
// The test setup is persistent in memory? Or maybe uniqueness constraint is strict.
// Actually, looking at the code, SaveRawLog *should* throw if trace_id exists.
// The failure message said: "Failed to execute statement for insert raw_log UNIQUE constraint failed: raw_logs.trace_id"
// This means the FIRST insert failed? No.
// Wait, the failure was:
// Expected: persistence_->SaveRawLog(trace_id,log_content1) doesn't throw an exception.
// Actual: it throws std::runtime_error ... UNIQUE constraint failed
// This implies the trace_id ALREADY existed before the first insert in this test function?
// Ah, the Fixture `SetUp` creates a new `:memory:` DB for each test (`TEST_F` creates a new subclass instance).
// However, maybe some static state or previous test didn't clean up?
// No, `persistence_` is unique_ptr created in SetUp.
// Let's debug by using a genuinely unique trace_id for this test.
TEST_F(SqliteLogRepository_test, test_save_duplicate_trace_id)
{
    std::string trace_id="unique_trace_id_for_dup_test_1765963056442814698";
    std::string log_content1="test log content1";
    std::string log_content2="test log content2";

    // First insert should succeed
    ASSERT_NO_THROW(persistence_->SaveRawLog(trace_id,log_content1));
    EXPECT_TRUE(logExists(trace_id));

    // Second insert with SAME trace_id MUST throw
    ASSERT_THROW(persistence_->SaveRawLog(trace_id,log_content2),std::runtime_error);
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