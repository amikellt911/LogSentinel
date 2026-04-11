#pragma once

#include <filesystem>
#include <string>

namespace DbPathResolver {

// 默认库路径不再依赖进程当前工作目录：
// 这里统一以“可执行文件所在目录”为锚点，把默认 DB 放到 ../persistence/data 下，
// 这样从项目根、server/build 或脚本里启动，落到的都是同一份库。
std::filesystem::path ResolveDefaultDatabasePath(const std::string& db_filename,
                                                 const std::filesystem::path& executable_path);

// 显式传入 --db 时，调用方给什么路径就按什么路径解释；
// 如果是相对路径，这里只把它转换成绝对路径，避免后面打印日志时继续让用户猜。
std::filesystem::path ResolveExplicitDatabasePath(const std::string& db_path,
                                                  const std::filesystem::path& current_working_directory);

// Linux 下优先走 /proc/self/exe，拿到真实可执行文件路径；
// 这样就算 argv[0] 是相对路径、软链接，或者用户从别的目录启动，默认 DB 路径也不会漂。
std::filesystem::path ResolveCurrentExecutablePath();

}  // namespace DbPathResolver
