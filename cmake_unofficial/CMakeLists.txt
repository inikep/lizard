PROJECT(LZ5 C)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "LZ5 compression library")
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 7)
set(CPACK_PACKAGE_VERSION_PATCH r132)
set(VERSION_STRING	" \"${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}\" ")
include(CPack)

cmake_minimum_required (VERSION 2.6)
INCLUDE (CheckTypeSize)
check_type_size("void *" SIZEOF_VOID_P)
IF(SIZEOF_VOID_P STREQUAL  "8")
    set (CMAKE_SYSTEM_PROCESSOR "64bit")
    MESSAGE( STATUS "64 bit architecture detected size of void * is " ${SIZEOF_VOID_P})
ENDIF()

option(BUILD_TOOLS "Build the command line tools" ON)
option(BUILD_LIBS  "Build the libraries in addition to the tools" ON)
option(LINK_TOOLS_WITH_LIB "Link the command line tools with the (shared) library" OFF)

IF(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR
   CMAKE_C_COMPILER_ID STREQUAL "Clang")
  SET(GNU_COMPATIBLE_COMPILER 1)
ENDIF()


set(LZ5_DIR ../lib/)
set(PRG_DIR ../programs/)
set(LZ5_SRCS_LIB ${LZ5_DIR}lz5_decompress.c ${LZ5_DIR}lz5_compress.c ${LZ5_DIR}lz5_compress.h ${LZ5_DIR}lz5_common.h ${LZ5_DIR}lz5frame.c ${LZ5_DIR}lz5frame.h ${LZ5_DIR}xxhash.c)
set(LZ5_SRCS ${LZ5_DIR}lz5frame.c ${LZ5_DIR}xxhash.c ${PRG_DIR}bench.c ${PRG_DIR}lz5cli.c ${PRG_DIR}lz5io.c)

if(BUILD_TOOLS AND NOT (LINK_TOOLS_WITH_LIB AND BUILD_LIBS))
    set(LZ5_SRCS ${LZ5_SRCS} ${LZ5_SRCS_LIB})
endif()

if(BUILD_TOOLS)
    add_executable(lz5 ${LZ5_SRCS})
    install(TARGETS lz5 RUNTIME DESTINATION "bin/")
endif()

if(BUILD_LIBS)


    SET(LIBS_TARGETS "")
    IF(WIN32)
        add_library(liblz5 STATIC ${LZ5_SRCS_LIB})
        SET(LIBS_TARGETS liblz5)
    ELSE(WIN32)
        add_library(liblz5 SHARED ${LZ5_SRCS_LIB})
        add_library(liblz5_static STATIC ${LZ5_SRCS_LIB})
        SET_TARGET_PROPERTIES(liblz5_static PROPERTIES OUTPUT_NAME lz5)
        SET(LIBS_TARGETS liblz5 liblz5_static)
    ENDIF(WIN32)

    set_target_properties(liblz5 PROPERTIES
    OUTPUT_NAME lz5
    SOVERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}"
    )

    install(TARGETS ${LIBS_TARGETS}
	RUNTIME DESTINATION lib #on Windows: cmake considers dlls as runtime component
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    )

    install(FILES
    ${LZ5_DIR}/lz5_compress.h
    ${LZ5_DIR}/lz5_common.h
    ${LZ5_DIR}/lz5frame.h
    DESTINATION include
    )

    set(PREFIX ${CMAKE_INSTALL_PREFIX})
    set(LIBDIR ${CMAKE_INSTALL_PREFIX}/lib)
    set(INCLUDEDIR ${CMAKE_INSTALL_PREFIX}/include)
    string(SUBSTRING ${CPACK_PACKAGE_VERSION_PATCH} 1 -1 VERSION)
    configure_file(${LZ5_DIR}/liblz5.pc.in liblz5.pc @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/liblz5.pc
    DESTINATION "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}/pkgconfig"
    )

  if(BUILD_TOOLS AND LINK_TOOLS_WITH_LIB)
        target_link_libraries(lz5 liblz5)
    endif()
endif()


#warnings

if(MSVC)
    ADD_DEFINITIONS("-W4")
endif()
if(GNU_COMPATIBLE_COMPILER)
    ADD_DEFINITIONS("-Wall")
endif()
if(CMAKE_COMPILER_IS_GNUCXX)
    ADD_DEFINITIONS("-Wextra")
    ADD_DEFINITIONS("-Wundef")
    ADD_DEFINITIONS("-Wshadow")
    ADD_DEFINITIONS("-Wcast-align")
    ADD_DEFINITIONS("-Wstrict-prototypes")
endif(CMAKE_COMPILER_IS_GNUCXX)
if(GNU_COMPATIBLE_COMPILER)
    # we need gnu99 instead of c99 on Linux and Solaris
    # to get C99 and POSIX definitions
    # an alternative with cmake >= 3.1/3.2 is the C_STANDARD property
    ADD_DEFINITIONS("-std=gnu99")
endif()
ADD_DEFINITIONS("-DLZ5_VERSION=\"${CPACK_PACKAGE_VERSION_PATCH}\"")
INCLUDE_DIRECTORIES (${LZ5_DIR})




