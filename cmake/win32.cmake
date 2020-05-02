if(NOT WIN32)
  return()
endif()

include(CheckCXXSourceCompiles)

enable_language(RC)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

if(NOT MSVC_VERSION)
  add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-bad-function-cast>)
  add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-cast-function-type>)
  # unlike unix where you get a *single* compiler ID string in .comment
  # GNU ld sees fit to merge *all* the .ident sections in object files
  # to .r[o]data section one after the other!
  add_compile_options(-fno-ident -Wa,-mbig-obj)
  link_libraries( -lws2_32 -lshlwapi -ldbghelp -luser32 -liphlpapi -lpsapi -luserenv )
  add_definitions(-DWINVER=0x0500 -D_WIN32_WINNT=0x0500)
  if (CMAKE_C_COMPILER_AR AND STATIC_LINK_RUNTIME)
    set(CMAKE_AR ${CMAKE_C_COMPILER_AR})
    set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> qcs <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_C_ARCHIVE_FINISH "true")
    set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qcs <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_CXX_ARCHIVE_FINISH "true")
    link_libraries( -static-libstdc++ -static-libgcc -static ${CMAKE_CXX_FLAGS} ${CRYPTO_FLAGS} )
  endif()
endif()

if(EMBEDDED_CFG)
  link_libatomic()
endif()

list(APPEND LIBTUNTAP_SRC ${TT_ROOT}/tuntap-windows.c)
get_filename_component(EV_SRC "llarp/ev/ev_libuv.cpp" ABSOLUTE)
add_definitions(-DWIN32_LEAN_AND_MEAN -DWIN32)
set(EXE_LIBS ${STATIC_LIB} ws2_32 iphlpapi)

if(RELEASE_MOTTO)
  add_definitions(-DLLARP_RELEASE_MOTTO="${RELEASE_MOTTO}")
endif()

if (NOT STATIC_LINK_RUNTIME AND NOT MSVC)
  message("must ship compiler runtime libraries with this build: libwinpthread-1.dll, libgcc_s_dw2-1.dll, and libstdc++-6.dll")
  message("for release builds, turn on STATIC_LINK_RUNTIME in cmake options")
endif()

if (STATIC_LINK_RUNTIME)
  set(LIBUV_USE_STATIC ON)
endif()

# Figure out if we need -lstdc++fs or -lc++fs and add it to the `filesystem` interface, if needed
# (otherwise just leave it an empty interface library; linking to it will do nothing).  The former
# is needed for gcc before v9, and the latter with libc++ before llvm v9.  But this gets more
# complicated than just using the compiler, because clang on linux by default uses libstdc++, so
# we'll just give up and see what works.

add_library(filesystem INTERFACE)

set(filesystem_code [[
#include <filesystem>

int main() {
    auto cwd = std::filesystem::current_path();
    return !cwd.string().empty();
}
]])

check_cxx_source_compiles("${filesystem_code}" filesystem_compiled)
if(filesystem_compiled)
  message(STATUS "No extra link flag needed for std::filesystem")
else()
  foreach(fslib stdc++fs c++fs)
    set(CMAKE_REQUIRED_LIBRARIES -l${fslib})
    check_cxx_source_compiles("${filesystem_code}" filesystem_compiled_${fslib})
    if (filesystem_compiled_${fslib})
      message(STATUS "Using -l${fslib} for std::filesystem support")
      target_link_libraries(filesystem INTERFACE ${fslib})
      break()
    endif()
  endforeach()
endif()
unset(CMAKE_REQUIRED_LIBRARIES)

if(LIBUV_ROOT)
  add_subdirectory(${LIBUV_ROOT})
  set(LIBUV_INCLUDE_DIRS ${LIBUV_ROOT}/include)
  set(LIBUV_LIBRARY uv_a)
  add_definitions(-D_LARGEFILE_SOURCE)
  add_definitions(-D_FILE_OFFSET_BITS=64)
elseif(NOT LIBUV_IN_SOURCE)
  find_package(LibUV 1.28.0 REQUIRED)
endif()

include_directories(${LIBUV_INCLUDE_DIRS})
