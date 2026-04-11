#include "util/DbPathResolver.h"

#include <system_error>

namespace DbPathResolver {

namespace {

std::filesystem::path NormalizeWithoutRequiringTargetExists(const std::filesystem::path& path)
{
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return normalized;
    }
    return path.lexically_normal();
}

}  // namespace

std::filesystem::path ResolveDefaultDatabasePath(const std::string& db_filename,
                                                 const std::filesystem::path& executable_path)
{
    if (db_filename == ":memory:") {
        return std::filesystem::path(db_filename);
    }

    // 默认库一律挂到可执行文件目录旁边的 ../persistence/data 下：
    // 这样主程序从项目根、build 目录或脚本里启动，最终都会落到同一份 DB。
    const std::filesystem::path executable_directory = executable_path.parent_path();
    return NormalizeWithoutRequiringTargetExists(
        executable_directory / ".." / "persistence" / "data" / db_filename);
}

std::filesystem::path ResolveExplicitDatabasePath(const std::string& db_path,
                                                  const std::filesystem::path& current_working_directory)
{
    if (db_path == ":memory:") {
        return std::filesystem::path(db_path);
    }

    const std::filesystem::path candidate(db_path);
    if (candidate.is_absolute()) {
        return NormalizeWithoutRequiringTargetExists(candidate);
    }

    // 用户显式传入 --db 时，仍然保留 CLI 常见语义：
    // 相对路径按当前工作目录解释，只是在进入 Repository 之前先转成绝对路径，避免后面继续猜。
    return NormalizeWithoutRequiringTargetExists(current_working_directory / candidate);
}

std::filesystem::path ResolveCurrentExecutablePath()
{
    std::error_code ec;
    const std::filesystem::path executable_path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !executable_path.empty()) {
        return NormalizeWithoutRequiringTargetExists(executable_path);
    }

    // Linux 上正常应当能从 /proc/self/exe 拿到真实路径；
    // 这个兜底分支只是在极端环境下保证不崩，至少还能给出一个稳定的相对锚点。
    return NormalizeWithoutRequiringTargetExists(std::filesystem::current_path() / "LogSentinel");
}

}  // namespace DbPathResolver
