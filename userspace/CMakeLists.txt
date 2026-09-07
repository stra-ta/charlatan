cmake_minimum_required(VERSION 3.20)

project(charlatan_userspace LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CHARLATAN_SANITIZER "" CACHE STRING "Compiler sanitizer list, such as address,undefined or thread")
if(CHARLATAN_SANITIZER)
  add_compile_options(-fsanitize=${CHARLATAN_SANITIZER} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${CHARLATAN_SANITIZER})
endif()

add_executable(charlatan-smoke src/smoke.cpp)
target_compile_options(charlatan-smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(charlatan-smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/uapi)

# The integration, stress, and benchmark harnesses drive /dev/charlatan0
# through Linux-only interfaces (epoll, Linux ioctl UAPI, ECANCELED reset
# semantics), so they build only on Linux. Non-Linux hosts still build
# charlatan-smoke, which is all verify.sh asserts via ctest.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(charlatan-tests src/integration.cpp)
  target_compile_options(charlatan-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_include_directories(charlatan-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/uapi)
  target_link_libraries(charlatan-tests PRIVATE Threads::Threads)

  add_executable(charlatan-stress src/stress.cpp)
  target_compile_options(charlatan-stress PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_include_directories(charlatan-stress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/uapi)
  target_link_libraries(charlatan-stress PRIVATE Threads::Threads)

  add_executable(charlatan-bench src/benchmark.cpp)
  target_compile_options(charlatan-bench PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_include_directories(charlatan-bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include/uapi)
else()
  message(STATUS "charlatan: skipping Linux-only harnesses (tests/stress/bench) on ${CMAKE_SYSTEM_NAME}; full userspace suite requires Linux")
endif()

enable_testing()
add_test(NAME smoke_usage COMMAND charlatan-smoke --help)
