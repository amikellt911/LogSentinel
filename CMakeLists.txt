cmake_minimum_required(VERSION 3.14)
project(LogSentinel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_SRCS

)

set(HTTP_SRCS

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
  GIT_TAG        v1.0.0
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/MiniMuduo
)

FetchContent_MakeAvailable(MiniMuduo)

add_executable(LogSentinel
    src/main.cpp   
    ${CORE_SRCS}
    ${HTTP_SRCS}
    ${PERSISTENCE_SRCS}
    ${WORKER_SRCS}
    ${AI_SRCS}
)




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
