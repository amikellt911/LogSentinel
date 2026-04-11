#include "util/DbPathResolver.h"

#include <gtest/gtest.h>

#include <filesystem>

TEST(DbPathResolverTest, DefaultDatabasePathUsesExecutableDirectoryInsteadOfCurrentWorkingDirectory)
{
    const std::filesystem::path executable_path("/tmp/demo/server/build/LogSentinel");
    const std::filesystem::path resolved =
        DbPathResolver::ResolveDefaultDatabasePath("LogSentinel.db", executable_path);

    // 这里锁死默认路径语义：
    // 不管调用者当前在哪个 cwd 启动，默认库都必须跟着可执行文件目录走，
    // 否则用户删错库、读错库时根本没法排查。
    EXPECT_EQ(resolved, std::filesystem::path("/tmp/demo/server/persistence/data/LogSentinel.db"));
}

TEST(DbPathResolverTest, ExplicitRelativeDatabasePathUsesCurrentWorkingDirectory)
{
    const std::filesystem::path current_working_directory("/tmp/demo/project");
    const std::filesystem::path resolved =
        DbPathResolver::ResolveExplicitDatabasePath("custom.db", current_working_directory);

    // 显式 --db 属于用户主动指定：
    // 这里就不再替用户偷偷改成别的目录，只把相对路径规整成绝对路径，方便日志打印和排障。
    EXPECT_EQ(resolved, std::filesystem::path("/tmp/demo/project/custom.db"));
}

TEST(DbPathResolverTest, MemoryDatabasePathIsPreserved)
{
    const std::filesystem::path current_working_directory("/tmp/demo/project");
    const std::filesystem::path resolved =
        DbPathResolver::ResolveExplicitDatabasePath(":memory:", current_working_directory);

    EXPECT_EQ(resolved, std::filesystem::path(":memory:"));
}
