cmake_minimum_required(VERSION 3.20)
project(course_work_parallel_computing LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TOOLS "Build tools (dataset selector, load test)" ON)

add_library(core
  src/core/inverted_index.cpp
  src/core/tokenizer.cpp
  src/core/document_store.cpp
  src/core/index_builder.cpp
  src/core/file_scanner.cpp
)
target_include_directories(core PUBLIC src)

add_library(concurrency
  src/concurrency/thread_pool.cpp
  src/concurrency/blocking_queue.cpp
)
target_include_directories(concurrency PUBLIC src)

add_library(net
  src/net/http_server.cpp
  src/net/request_router.cpp
)
target_include_directories(net PUBLIC src)

add_executable(server src/apps/server_main.cpp)
target_link_libraries(server PRIVATE core concurrency net)

add_executable(client_cli src/apps/client_cli_main.cpp)
target_link_libraries(client_cli PRIVATE net)

if (BUILD_TOOLS)
  add_executable(load_test src/apps/load_test_main.cpp)
  target_link_libraries(load_test PRIVATE net concurrency)

  add_executable(dataset_selector src/apps/dataset_selector_main.cpp)
  target_link_libraries(dataset_selector PRIVATE core)
endif()
