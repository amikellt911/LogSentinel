#include"util/TraceIdGenerator.h"
#include<string>
#include<chrono>
#include<ctime>
#include<sstream>
#include<iomanip>
std::string generateTraceId(){
    const auto now=std::chrono::system_clock::now();
    const std::time_t t_c=std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t_c);
#else
    localtime_r(&t_c, &tm_buf);
#endif
    std::stringstream ss;
    ss<<std::put_time(&tm_buf,"%Y-%m-%d %H:%M:%S");
    std::string s=ss.str();

}