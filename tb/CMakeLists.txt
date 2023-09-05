add_library(tb)
target_link_libraries(tb PRIVATE common c11threads)

target_sources(tb PRIVATE
    src/libtb.c
    src/x64/x64.c)
target_sources(tb PUBLIC
    FILE_SET HEADERS
    TYPE HEADERS
    BASE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include"
    FILES
    include/tb_coff.h
    include/tb_elf.h
    include/tb_formats.h
    include/tb_x64.h
    include/tb.h)

cuik_install_component(TildeBackend tb common c11threads)