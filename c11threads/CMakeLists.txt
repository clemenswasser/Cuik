add_library(c11threads)
if(WIN32)
	target_sources(c11threads PRIVATE threads_msvc.c)
else()
	target_sources(c11threads PRIVATE threads_msvc.c)
endif()
target_sources(c11threads PUBLIC
	FILE_SET HEADERS
	TYPE HEADERS
	FILES threads.h)
