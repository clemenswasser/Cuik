add_library(common)

if(WIN32)
    target_compile_definitions(common PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

target_compile_features(common PUBLIC c_std_11)
target_link_libraries(common PRIVATE c11threads)
target_sources(common PRIVATE common.c perf.c)
target_sources(common PUBLIC
    FILE_SET HEADERS
    TYPE HEADERS
    FILES
    arena.h
    common.h
    dyn_array.h
    file_map.h
    futex.h
    hash_map.h
    hash_set.h
    hashes.h
    log.h
    perf.h)
