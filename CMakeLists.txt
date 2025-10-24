cmake_minimum_required(VERSION 3.14)
project(LogSentinel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------依赖管理 ----------------

include(FetchContent)

FetchContent_Declare(
  MiniMuduo
  GIT_REPOSITORY https://github.com/amikellt911/MiniMuduo.git
  GIT_TAG        v1.0.2
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/MiniMuduo
)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest
)

FetchContent_MakeAvailable(MiniMuduo googletest)

#--------------模块管理------------------
add_library(http_module STATIC
    http/HttpRequest.cpp
    http/HttpResponse.cpp
    http/HttpContext.cpp
    #http/HttpServer.cpp
)

# add_library(core_module STATIC

# )
# add_library(persistence_module STATIC

# )
# add_library(worker_module STATIC

# )
# add_library(ai_module STATIC

# )

target_include_directories(http_module PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
# target_include_directories(core_module PUBLIC
#   ${CMAKE_CURRENT_SOURCE_DIR}
# )
# target_include_directories(persistence_module PUBLIC
#   ${CMAKE_CURRENT_SOURCE_DIR}
# )
# target_include_directories(worker_module PUBLIC
#   ${CMAKE_CURRENT_SOURCE_DIR}
# )
# target_include_directories(ai_module PUBLIC
#   ${CMAKE_CURRENT_SOURCE_DIR}
# )

target_link_libraries(http_module PUBLIC MiniMuduo)

#-----------测试模块---------------
enable_testing()
include(GoogleTest)
add_executable(test_http_context 
  tests/http_context_test.cpp
)

# target_include_directories(test_http_context PRIVATE
# ${CMAKE_CURRENT_SOURCE_DIR}
# )
target_link_libraries(test_http_context PRIVATE 
GTest::gtest_main
http_module
)
gtest_add_tests(TARGET test_http_context)



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
# core_module
# persistence_module
# worker_module
# ai_module
sqlite3
)
