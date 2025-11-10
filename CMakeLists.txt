cmake_minimum_required(VERSION 3.14)
project(LogSentinel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------依赖管理 ----------------

include(FetchContent)

FetchContent_Declare(
  MiniMuduo
  GIT_REPOSITORY https://github.com/amikellt911/MiniMuduo.git
  GIT_TAG        v1.1.1
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/MiniMuduo
)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest
)

FetchContent_MakeAvailable(MiniMuduo googletest)
find_package(SQLite3 REQUIRED)

#--------------模块管理------------------
add_library(http_module STATIC
    http/HttpRequest.cpp
    http/HttpResponse.cpp
    http/HttpContext.cpp
    http/HttpServer.cpp
)

add_library(util_module STATIC
    util/TraceIdGenerator.cpp
)

add_library(core_module STATIC
    core/AnalysisTask.cpp
)
add_library(persistence_module STATIC
    persistence/SqlitePersistence.cpp
)
add_library(threadpool_module STATIC
  threadpool/ThreadPool.cpp
)
add_library(ai_module STATIC
  ai/MockAI.cpp
)

target_include_directories(http_module PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(util_module PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(core_module PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(persistence_module PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(threadpool_module PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(ai_module PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(http_module PUBLIC MiniMuduo util_module)
target_link_libraries(threadpool_module PUBLIC MiniMuduo)
target_link_libraries(persistence_module PRIVATE SQLite::SQLite3)
#-----------测试模块---------------
enable_testing()

include(GoogleTest)

add_executable(test_http_context 
  tests/http_context_test.cpp
)

add_executable(test_util_traceidgenerate
  tests/util_traceidgenerate_test.cpp
)

add_executable(test_threadpool
  tests/threadpool_test.cpp
)

add_executable(test_sqlite
  tests/SqlitePersistence_test.cpp
)

# target_include_directories(test_http_context PRIVATE
# ${CMAKE_CURRENT_SOURCE_DIR}
# )
target_link_libraries(test_http_context PRIVATE 
GTest::gtest_main
http_module
)
target_link_libraries(test_util_traceidgenerate PRIVATE 
GTest::gtest_main
util_module
)
target_link_libraries(test_threadpool PRIVATE 
GTest::gtest_main
threadpool_module
)
target_link_libraries(test_sqlite PRIVATE 
GTest::gtest_main
persistence_module
)
#给vscode的C++扩展的CTest.
add_test(NAME test_http_context COMMAND test_http_context)
add_test(NAME test_util_traceidgenerate COMMAND test_util_traceidgenerate)
add_test(NAME test_threadpool COMMAND test_threadpool)
add_test(NAME test_sqlite COMMAND test_sqlite)

gtest_discover_tests(test_http_context)
gtest_discover_tests(test_util_traceidgenerate)
gtest_discover_tests(test_threadpool)
gtest_discover_tests(test_sqlite)
#-----------不同模块测试用例---------------
#http层测试用例
add_executable(testserver
    src/testserver.cpp   
)
#http层结合线程池测试用例
add_executable(testserverpool
  src/testserverpool.cpp
)
#http层结合线程池和sqlite数据库测试用例
add_executable(testServerPoolSqlite
  src/testServerPoolSqlite.cpp  
)
#上面与mockai测试用例
add_executable(testServerPoolSqliteMockAI
  src/testserverPoolSqliteMock.cpp
)
# 因为module会继承过来
# target_include_directories(LogSentinel PRIVATE
# ${CMAKE_CURRENT_SOURCE_DIR}
# )
target_link_libraries(testserver PRIVATE 
http_module
# core_module
# persistence_module
# threadpool_module
# ai_module
)
target_link_libraries(testserverpool PRIVATE
http_module
threadpool_module
core_module
)
target_link_libraries(testServerPoolSqlite PRIVATE
http_module
threadpool_module
core_module
persistence_module
)
target_link_libraries(testServerPoolSqliteMockAI PRIVATE
http_module
threadpool_module
core_module
persistence_module
ai_module
)
#-----------主程序---------------
add_executable(LogSentinel
    src/main.cpp   
)

# 因为module会继承过来
# target_include_directories(LogSentinel PRIVATE
# ${CMAKE_CURRENT_SOURCE_DIR}
# )
target_link_libraries(LogSentinel PRIVATE 
http_module
core_module
# persistence_module
threadpool_module
# ai_module
)
