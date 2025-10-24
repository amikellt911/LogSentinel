cmake_minimum_required(VERSION 3.14)
project(LogSentinel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_SRCS

)

set(HTTP_SRCS
  http/HttpRequest.cpp
  http/HttpResponse.cpp
  http/HttpContext.cpp
  #http/HttpServer.cpp
)

set(PERSISTENCE_SRCS

)

set(WORKER_SRCS

)
set(AI_SRCS

)

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

FetchContent_MakeAvailable(MiniMuduo)
FetchContent_MakeAvailable(googletest)
add_executable(LogSentinel
    src/main.cpp   
    ${CORE_SRCS}
    ${HTTP_SRCS}
    ${PERSISTENCE_SRCS}
    ${WORKER_SRCS}
    ${AI_SRCS}
)
enable_testing()
include(GoogleTest)
add_executable(test_http_context 
  tests/http_context_test.cpp
  ${HTTP_SRCS}
)

target_include_directories(test_http_context PRIVATE
${CMAKE_CURRENT_SOURCE_DIR}
http
)
target_link_libraries(test_http_context PRIVATE 
MiniMuduo
GTest::gtest_main
)
gtest_add_tests(TARGET test_http_context)
target_include_directories(LogSentinel PRIVATE
${CMAKE_CURRENT_SOURCE_DIR}
core
http
persistence
worker
ai
)
target_link_libraries(LogSentinel PRIVATE 
MiniMuduo
sqlite3
)
