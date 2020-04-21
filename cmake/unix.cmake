if(NOT UNIX)
  return()
endif()

# because apple means embracing bitrot
if(APPLE)
  add_compile_options(-mmacosx-version-min=10.13)
endif()

include(CheckCXXSourceCompiles)
include(CheckLibraryExists)


option(DOWNLOAD_CURL "download and statically compile in CURL" OFF)
# Allow -DDOWNLOAD_CURL=FORCE to download without even checking for a local libcurl
if(NOT DOWNLOAD_CURL STREQUAL "FORCE")
  include(FindCURL)
endif()

if(CURL_FOUND)
  message(STATUS "using system curl")
elseif(DOWNLOAD_CURL)
  message(STATUS "libcurl not found, but DOWNLOAD_CURL specified, so downloading it")
  include(DownloadLibCurl)
  set(CURL_LIBRARIES curl_vendor)
else()
  message(FATAL_ERROR "Could not find libcurl; either install it on your system or use -DDOWNLOAD_CURL=ON to download and build an internal copy")
endif()

add_definitions(-DUNIX)
add_definitions(-DPOSIX)
list(APPEND LIBTUNTAP_SRC ${TT_ROOT}/tuntap-unix.c)

if (STATIC_LINK_RUNTIME OR STATIC_LINK)
  set(LIBUV_USE_STATIC ON)
endif()


option(DOWNLOAD_UV "statically compile in libuv" OFF)
# Allow -DDOWNLOAD_UV=FORCE to download without even checking for a local libuv
if(NOT DOWNLOAD_UV STREQUAL "FORCE")
  find_package(LibUV 1.28.0)
endif()
if(LibUV_FOUND)
  message(STATUS "using system libuv")
elseif(DOWNLOAD_UV)
  message(STATUS "using libuv submodule")
  set(LIBUV_ROOT ${CMAKE_SOURCE_DIR}/external/libuv)
  add_subdirectory(${LIBUV_ROOT})
  set(LIBUV_INCLUDE_DIRS ${LIBUV_ROOT}/include)
  set(LIBUV_LIBRARY uv_a)
  add_definitions(-D_LARGEFILE_SOURCE)
  add_definitions(-D_FILE_OFFSET_BITS=64)
endif()

include_directories(${LIBUV_INCLUDE_DIRS})

if(EMBEDDED_CFG OR ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  link_libatomic()
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  list(APPEND LIBTUNTAP_SRC ${TT_ROOT}/tuntap-unix-linux.c)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Android")
  list(APPEND LIBTUNTAP_SRC ${TT_ROOT}/tuntap-unix-linux.c)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD")
  add_definitions(-D_BSD_SOURCE)
  add_definitions(-D_GNU_SOURCE)
  add_definitions(-D_XOPEN_SOURCE=700)
  list(APPEND LIBTUNTAP_SRC ${TT_ROOT}/tuntap-unix-openbsd.c ${TT_ROOT}/tuntap-unix-bsd.c)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "NetBSD")
  list(APPEND LIBTUNTAP_SRC ${TT_ROOT}/tuntap-unix-netbsd.c ${TT_ROOT}/tuntap-unix-bsd.c)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD" OR ${CMAKE_SYSTEM_NAME} MATCHES "DragonFly")
  list(APPEND LIBTUNTAP_SRC ${TT_ROOT}/tuntap-unix-freebsd.c ${TT_ROOT}/tuntap-unix-bsd.c)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin" OR ${CMAKE_SYSTEM_NAME} MATCHES "iOS")
  list(APPEND LIBTUNTAP_SRC ${TT_ROOT}/tuntap-unix-darwin.c ${TT_ROOT}/tuntap-unix-bsd.c)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
  list(APPEND LIBTUNTAP_SRC ${TT_ROOT}/tuntap-unix-sunos.c)
  # Apple C++ screws up name decorations in stdc++fs, causing link to fail
  # Samsung does not build c++experimental or c++fs in their Apple libc++ pkgsrc build
  if (LIBUV_USE_STATIC)
    link_libraries(-lkstat -lsendfile)
  endif()
else()
  message(FATAL_ERROR "Your operating system - ${CMAKE_SYSTEM_NAME} is not supported yet")
endif()

set(EXE_LIBS ${STATIC_LIB})

if(RELEASE_MOTTO)
  add_definitions(-DLLARP_RELEASE_MOTTO="${RELEASE_MOTTO}")
endif()
