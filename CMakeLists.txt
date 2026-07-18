cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
        set(CMAKE_CUDA_ARCHITECTURES native CACHE STRING "CUDA architectures")
    else()
        execute_process(
            COMMAND nvidia-smi --query-gpu=compute_cap --format=csv,noheader
            OUTPUT_VARIABLE CUDA_COMPUTE_CAPABILITY
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY
        )
        string(REGEX MATCH "^[0-9]+\\.[0-9]+" CUDA_COMPUTE_CAPABILITY "${CUDA_COMPUTE_CAPABILITY}")
        string(REPLACE "." "" CUDA_COMPUTE_CAPABILITY "${CUDA_COMPUTE_CAPABILITY}")
        set(CMAKE_CUDA_ARCHITECTURES "${CUDA_COMPUTE_CAPABILITY}" CACHE STRING "CUDA architectures")
    endif()
endif()

project(ed25519_gpu_vanity LANGUAGES C CXX CUDA)

find_package(OpenSSL REQUIRED COMPONENTS Crypto)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
set(VANITY_ATTEMPTS_PER_EXECUTION 10000 CACHE STRING "Candidates processed by each thread per kernel launch")
set(VANITY_MAX_REGISTER_COUNT 128 CACHE STRING "CUDA register limit (auto or 128)")
set(VANITY_THREADS_PER_BLOCK 512 CACHE STRING "CUDA threads per block (256 or 512)")

set(VANITY_REGISTER_CHOICES auto 128)
if(NOT VANITY_MAX_REGISTER_COUNT IN_LIST VANITY_REGISTER_CHOICES)
    message(FATAL_ERROR "VANITY_MAX_REGISTER_COUNT must be one of: ${VANITY_REGISTER_CHOICES}")
endif()

set(VANITY_BLOCK_SIZE_CHOICES 256 512)
if(NOT VANITY_THREADS_PER_BLOCK IN_LIST VANITY_BLOCK_SIZE_CHOICES)
    message(FATAL_ERROR "VANITY_THREADS_PER_BLOCK must be one of: ${VANITY_BLOCK_SIZE_CHOICES}")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/release")

add_library(fixed16-host-field STATIC
    src/sgx-ecc-ed25519/fe.c
)
target_include_directories(fixed16-host-field PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/sgx-ecc-ed25519"
)
set_property(TARGET fixed16-host-field PROPERTY NO_SYSTEM_FROM_IMPORTED TRUE)

add_executable(fixed16_table_tool
    tools/fixed16_table_tool.cpp
    tools/fixed16_table_main.cpp
    tools/fixed16_sha256.cpp
)
target_include_directories(fixed16_table_tool PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fixed16_table_tool PRIVATE fixed16-host-field OpenSSL::Crypto)
set_property(TARGET fixed16_table_tool PROPERTY NO_SYSTEM_FROM_IMPORTED TRUE)

set(PROJECT_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/cuda-headers"
    "${CMAKE_SOURCE_DIR}/tools"
)

add_library(vanity-engine STATIC
    src/ed25519-cuda/scalar/sc.cu
    src/ed25519-cuda/field/fe.cu
    src/ed25519-cuda/group/ge.cu
    src/ed25519-cuda/hash/sha512.cu
    src/search/fixed16_table_loader.cu
    src/search/result_writer.cpp
    src/search/vanity_kernel.cu
    src/search/vanity_search.cu
    tools/fixed16_table_tool.cpp
    tools/fixed16_sha256.cpp
)
set_target_properties(vanity-engine PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
)
target_compile_options(vanity-engine PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:--ptxas-options=-v>
)
if(NOT VANITY_MAX_REGISTER_COUNT STREQUAL "auto")
    target_compile_options(vanity-engine PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--maxrregcount=${VANITY_MAX_REGISTER_COUNT}>
    )
    set(VANITY_USE_LAUNCH_BOUNDS 0)
else()
    set(VANITY_USE_LAUNCH_BOUNDS 1)
endif()
target_include_directories(vanity-engine PUBLIC ${PROJECT_INCLUDE_DIRS})
target_compile_definitions(vanity-engine PRIVATE
    VANITY_ATTEMPTS_PER_EXECUTION=${VANITY_ATTEMPTS_PER_EXECUTION}
    VANITY_THREADS_PER_BLOCK=${VANITY_THREADS_PER_BLOCK}
    VANITY_REGISTER_LIMIT="${VANITY_MAX_REGISTER_COUNT}"
    VANITY_USE_LAUNCH_BOUNDS=${VANITY_USE_LAUNCH_BOUNDS}
)
target_link_libraries(vanity-engine PUBLIC OpenSSL::Crypto)
target_link_libraries(vanity-engine PRIVATE fixed16-host-field)
set_property(TARGET vanity-engine PROPERTY NO_SYSTEM_FROM_IMPORTED TRUE)

add_executable(solana-vanity-cuda src/main.cpp)
target_link_libraries(solana-vanity-cuda PRIVATE vanity-engine)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME fixed16_table_tool_help
        COMMAND fixed16_table_tool --help)
    add_test(NAME fixed16_table_memory_generation
        COMMAND fixed16_table_tool memory-test)
endif()
