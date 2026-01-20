cmake_minimum_required(VERSION 3.16)

project(course_work_parallel_computing LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_WERROR "Treat warnings as errors" OFF)

# Threads (pthread on Linux, etc.)
find_package(Threads REQUIRED)

# -----------------------
# Common compile options
# -----------------------
function(set_common_options target_name)
  target_compile_features(${target_name} PRIVATE cxx_std_17)

  if (MSVC)
    target_compile_options(${target_name} PRIVATE /W4)
    if (ENABLE_WERROR)
      target_compile_options(${target_name} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_WERROR)
      target_compile_options(${target_name} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# Root include: allow includes like "utils/logging.h"
set(PROJECT_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# -----------------------
# Libraries
# -----------------------
add_library(concurrency
  ${PROJECT_SRC_DIR}/concurrency/thread_pool.cpp
  ${PROJECT_SRC_DIR}/concurrency/blocking_queue.cpp
)
target_include_directories(concurrency PUBLIC ${PROJECT_SRC_DIR})
target_link_libraries(concurrency PUBLIC Threads::Threads)
set_common_options(concurrency)

add_library(utils
  ${PROJECT_SRC_DIR}/utils/time_utils.cpp
  ${PROJECT_SRC_DIR}/utils/logging.cpp
  ${PROJECT_SRC_DIR}/utils/config.cpp
)
target_include_directories(utils PUBLIC ${PROJECT_SRC_DIR})
target_link_libraries(utils PUBLIC Threads::Threads)
set_common_options(utils)

add_library(net
  ${PROJECT_SRC_DIR}/net/http_server.cpp
  ${PROJECT_SRC_DIR}/net/request_router.cpp
)
target_include_directories(net PUBLIC ${PROJECT_SRC_DIR})
target_link_libraries(net PUBLIC utils Threads::Threads)

# ws2_32 for Windows sockets
if (WIN32)
  target_link_libraries(net PUBLIC ws2_32)
endif()

set_common_options(net)

add_library(core
  ${PROJECT_SRC_DIR}/core/inverted_index.cpp
  ${PROJECT_SRC_DIR}/core/tokenizer.cpp
  ${PROJECT_SRC_DIR}/core/document_store.cpp
  ${PROJECT_SRC_DIR}/core/index_builder.cpp
  ${PROJECT_SRC_DIR}/core/file_scanner.cpp
)
target_include_directories(core PUBLIC ${PROJECT_SRC_DIR})
target_link_libraries(core PUBLIC concurrency utils Threads::Threads)
set_common_options(core)

# -----------------------
# Apps
# -----------------------
add_executable(server
  ${PROJECT_SRC_DIR}/apps/server_main.cpp
)
target_link_libraries(server PRIVATE core net utils concurrency Threads::Threads)
set_common_options(server)

add_executable(client_cli
  ${PROJECT_SRC_DIR}/apps/client_cli_main.cpp
)
target_link_libraries(client_cli PRIVATE Threads::Threads)
if (WIN32)
  target_link_libraries(client_cli PRIVATE ws2_32)
endif()
set_common_options(client_cli)

add_executable(load_test
  ${PROJECT_SRC_DIR}/apps/load_test_main.cpp
)
target_link_libraries(load_test PRIVATE core utils concurrency Threads::Threads)
if (WIN32)
  target_link_libraries(load_test PRIVATE ws2_32)
endif()
set_common_options(load_test)

add_executable(dataset_selector
  ${PROJECT_SRC_DIR}/apps/dataset_selector_main.cpp
)
target_link_libraries(dataset_selector PRIVATE Threads::Threads)
set_common_options(dataset_selector)

# -----------------------
# Copy web/ to build dir рядом с server
# -----------------------
set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")
add_custom_command(TARGET server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:server>/web"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${WEB_SRC_DIR}/index.html" "$<TARGET_FILE_DIR:server>/web/index.html"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${WEB_SRC_DIR}/app.js" "$<TARGET_FILE_DIR:server>/web/app.js"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${WEB_SRC_DIR}/styles.css" "$<TARGET_FILE_DIR:server>/web/styles.css"
  COMMENT "Copying web assets next to server binary"
)

# -----------------------
# Convenience target: plot_results
# -----------------------
add_custom_target(plot_results
  COMMAND ${CMAKE_COMMAND} -E echo "Run: python3 tools/plot_results.py --input build.csv --out build.png"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
