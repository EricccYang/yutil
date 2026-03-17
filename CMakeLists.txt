add_library(yutil_ds INTERFACE)

find_package(Threads REQUIRED)

target_include_directories(yutil_ds
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/ds
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../SPSCQueue/include")
  target_include_directories(yutil_ds INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../SPSCQueue/include")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../MPMCQueue/include")
  target_include_directories(yutil_ds INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../MPMCQueue/include")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../EASTL/include")
  target_include_directories(yutil_ds INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../EASTL/include")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../kiwaku/include")
  target_include_directories(yutil_ds INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../kiwaku/include")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../immer")
  target_include_directories(yutil_ds INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../immer")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../taskflow/taskflow")
  target_include_directories(yutil_ds INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../taskflow")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../concurrentqueue/concurrentqueue.h")
  target_include_directories(yutil_ds INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../concurrentqueue")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../thread-pool/include")
  target_include_directories(yutil_ds INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../thread-pool/include")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../oneTBB/include")
  target_include_directories(yutil_ds INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../oneTBB/include")
endif()

include(CTest)

file(GLOB_RECURSE YUTIL_BENCHMARK_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp"
)

if(YUTIL_BENCHMARK_SOURCES)
  foreach(BENCHMARK_SOURCE ${YUTIL_BENCHMARK_SOURCES})
    get_filename_component(BENCHMARK_NAME ${BENCHMARK_SOURCE} NAME_WE)
    add_executable(${BENCHMARK_NAME} ${BENCHMARK_SOURCE})
    target_link_libraries(${BENCHMARK_NAME} PRIVATE yutil_ds Threads::Threads)
    if(TARGET benchmark::benchmark)
      target_link_libraries(${BENCHMARK_NAME} PRIVATE benchmark::benchmark)
    endif()
    target_compile_features(${BENCHMARK_NAME} PRIVATE cxx_std_17)
  endforeach()
endif()

file(GLOB_RECURSE YUTIL_TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

if(YUTIL_TEST_SOURCES)
  foreach(TEST_SOURCE ${YUTIL_TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE yutil_ds Threads::Threads)
    target_compile_features(${TEST_NAME} PRIVATE cxx_std_17)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
  endforeach()
endif()
