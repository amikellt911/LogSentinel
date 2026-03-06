#include"util/TraceIdGenerator.h"
#include<chrono>
#include<ctime>
#include<sstream>
#include<iomanip>
#include<functional>
#include<unistd.h>
#include<thread>
// std::string generateTraceId(){
//     thread_local static size_t uint64_t =0;
//     static uint32_t host_id=getHostId();
//     static pid_t p_id=getpid();
//     thread_local std::thread::id t_id= std::this_thread::get_id();
//     const auto now=std::chrono::system_clock::now();
//     const std::time_t t_c=std::chrono::system_clock::to_time_t(now);
//     std::tm tm_buf;
// #ifdef _WIN32
//     localtime_s(&tm_buf, &t_c);
// #else
//     localtime_r(&t_c, &tm_buf);
// #endif
//     std::stringstream ss;
//     ss<<std::put_time(&tm_buf,"%Y-%m-%d %H:%M:%S");
//     ss<<",host_id: "<<std::to_string(host_id)<<",pthread_id: "<<std::to_string(p_id)<<t_id;
//     return ss.str();
// }
static uint32_t getHostId() {
    char host[256];
    if(gethostname(host,sizeof(host))==0)
    {
        return std::hash<std::string>{}(host);
    }
    return 0;
}

std::string generateTraceId() {
    // ---- 不变的部分，只在首次调用时初始化一次 ----
    static uint32_t host_id = getHostId();
    static pid_t p_id = getpid();

    // ---- 易变的部分，每次调用都获取 ----

    // 1. 使用微秒级时间戳，这是唯一性的关键
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();

    // 2. 将线程 ID 转换为整数
    std::stringstream tid_ss;
    tid_ss << std::this_thread::get_id();
    size_t tid = std::hash<std::string>{}(tid_ss.str());

    // 3. (推荐) 使用 thread_local 计数器，避免锁
    thread_local static uint64_t counter = 0;
    ++counter;

    // 4. 组合成最终的字符串 (格式可以自定义)
    std::stringstream ss;
    ss << std::hex // 使用十六进制让 ID 更紧凑
       << micros << "-"
       << host_id << "-"
       << p_id << "-"
       << tid << "-"
       << counter;

    return ss.str();
}

