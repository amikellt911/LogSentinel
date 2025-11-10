#include <gtest/gtest.h>
#include <persistence/SqlitePersistence.h>

class SqlitePersistence_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        persistence_ = std::make_unique<SqlitePersistence>(":memory:");
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

    std::unique_ptr<SqlitePersistence> persistence_;
};


//测试构造函数，是否成功构造了一个内存数据库
TEST_F(SqlitePersistence_test, test_constructor)
{
    EXPECT_NE(persistence_,nullptr);
}

//测试保存日志功能
TEST_F(SqlitePersistence_test, test_save_log)
{
    std::string trace_id="test_trace_id";
    std::string log_content="test log content";
    ASSERT_NO_THROW(persistence_->SaveRawLog(trace_id,log_content));
    EXPECT_TRUE(logExists(trace_id));
    EXPECT_EQ(getLog(trace_id),log_content);
}

//测试保存日志功能，保存多个日志
TEST_F(SqlitePersistence_test, test_save_multiple_logs)
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
TEST_F(SqlitePersistence_test, test_save_duplicate_trace_id)
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
TEST(SqlitePersistenceConstructorTest, ThrowsOnInvalidPath) {
    // 注意：这个测试不能用 Test Fixture，因为它测试的就是构造函数本身
    // 这个测试在 Docker 或 CI 环境中可能需要特殊设置才能运行
    #ifdef __linux__
    const std::string invalid_path = "/root/no_permission/test.db";
    //一个非常劣性的bug
    //必须加一个括号来解决
    //ASSERT_THROW((SqlitePersistence(invalid_path)), std::runtime_error);
    ASSERT_THROW(SqlitePersistence{invalid_path}, std::runtime_error);
    #endif
}