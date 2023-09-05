add_executable(lexgen)
target_sources(lexgen PRIVATE meta/lexgen.c)

list(APPEND LEXGEN_HEADERS
    "${CMAKE_CURRENT_BINARY_DIR}/keywords.h"
    "${CMAKE_CURRENT_BINARY_DIR}/dfa.h")
add_custom_command(OUTPUT ${LEXGEN_HEADERS}
    COMMAND lexgen ${LEXGEN_HEADERS}
    VERBATIM)
add_custom_target(generate_keywords_dfa DEPENDS ${LEXGEN_HEADERS})
add_library(keywords_dfa INTERFACE)
add_dependencies(keywords_dfa generate_keywords_dfa)
target_sources(keywords_dfa INTERFACE FILE_SET HEADERS TYPE HEADERS BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}" FILES ${LEXGEN_HEADERS})

add_executable(hexembed)
target_sources(hexembed PRIVATE meta/hexembed.c)

list(APPEND hexembed_headers
    "${CMAKE_CURRENT_LIST_DIR}/../headers/emmintrin.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/float.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/immintrin.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/intrin.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/limits.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/mm_malloc.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/stdalign.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/stdarg.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/stdatomic.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/stdbool.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/stddef.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/stdint.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/x86intrin.h"
    "${CMAKE_CURRENT_LIST_DIR}/../headers/xmmintrin.h")

add_custom_command(OUTPUT freestanding.c
    COMMAND hexembed "${CMAKE_CURRENT_BINARY_DIR}/freestanding.c" ${hexembed_headers}
    DEPENDS ${hexembed_headers}
    VERBATIM)
add_library(freestanding OBJECT freestanding.c)

add_library(cuik)

if((CMAKE_C_COMPILER_ID STREQUAL [[GNU]] OR CMAKE_C_COMPILER_ID STREQUAL [[Clang]]) AND CMAKE_SYSTEM_PROCESSOR STREQUAL [[AMD64]])
    target_compile_options(cuik PRIVATE [[-msse4.2]])
endif()

target_link_libraries(cuik PRIVATE common freestanding keywords_dfa c11threads $<TARGET_NAME_IF_EXISTS:tb>)
target_compile_definitions(cuik PRIVATE CUIK_ALLOW_THREADS $<$<TARGET_EXISTS:tb>:CUIK_USE_TB>)

target_sources(cuik PRIVATE
    lib/libcuik.c
    lib/toolchains/msvc.c
    lib/toolchains/gnu.c
    lib/toolchains/darwin.c)
target_sources(cuik PUBLIC
    FILE_SET HEADERS
    TYPE HEADERS
    BASE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include"
    FILES
    include/cuik_ast.h
    include/cuik_driver.h
    include/cuik_fs.h
    include/cuik_irgen.h
    include/cuik_lex.h
    include/cuik_parse.h
    include/cuik_prelude.h
    include/cuik_private.h
    include/cuik_symtab.h
    include/cuik.h)

cuik_install_component(Cuik cuik freestanding keywords_dfa common c11threads)