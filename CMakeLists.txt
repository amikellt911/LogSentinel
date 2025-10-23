cmake_minimum_required(VERSION 3.14)
project(LogSentinel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)


add_executable(LogSentinel
    src/main.cpp   
)


include(FetchContent)

FetchContent_Declare(
  MiniMuduo
  GIT_REPOSITORY https://github.com/amikellt911/MiniMuduo.git
  GIT_TAG        v1.0.0
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext/MiniMuduo
)

FetchContent_MakeAvailable(MiniMuduo)


target_link_libraries(LogSentinel PRIVATE MiniMuduo)
