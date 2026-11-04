# Locate htslib and expose it as HTSlib::htslib.
#
# Preference order:
#   1. pkg-config metadata (version, include and library hints)
#   2. conventional include/library search paths
#
# With HUSK_STATIC=ON the module selects libhts.a and propagates the static
# link dependencies reported by pkg-config.

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_HTSLIB QUIET htslib)
endif()

set(_htslib_include_hints ${PC_HTSLIB_INCLUDE_DIRS})
set(_htslib_library_hints ${PC_HTSLIB_LIBRARY_DIRS})
foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
    list(APPEND _htslib_include_hints "${_prefix}/include")
    list(APPEND _htslib_library_hints "${_prefix}/lib")
endforeach()

find_path(
    HTSlib_INCLUDE_DIR
    NAMES htslib/sam.h
    HINTS ${_htslib_include_hints}
)

if(HUSK_STATIC)
    find_file(
        HTSlib_LIBRARY
        NAMES libhts.a
        HINTS ${_htslib_library_hints}
    )
else()
    find_library(
        HTSlib_LIBRARY
        NAMES hts
        HINTS ${_htslib_library_hints}
    )
endif()

set(HTSlib_VERSION "${PC_HTSLIB_VERSION}")

if(NOT HTSlib_VERSION AND HTSlib_INCLUDE_DIR)
    file(
        STRINGS "${HTSlib_INCLUDE_DIR}/htslib/hts.h"
        _htslib_version_line
        REGEX "^#define[ \t]+HTS_VERSION[ \t]+[0-9]+"
    )
    if(_htslib_version_line MATCHES "([0-9]+)")
        math(EXPR _htslib_major "${CMAKE_MATCH_1} / 100000")
        math(EXPR _htslib_minor "(${CMAKE_MATCH_1} / 100) % 1000")
        math(EXPR _htslib_patch "${CMAKE_MATCH_1} % 100")
        set(
            HTSlib_VERSION
            "${_htslib_major}.${_htslib_minor}.${_htslib_patch}"
        )
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    HTSlib
    REQUIRED_VARS HTSlib_LIBRARY HTSlib_INCLUDE_DIR
    VERSION_VAR HTSlib_VERSION
)

if(HTSlib_FOUND AND NOT TARGET HTSlib::htslib)
    add_library(HTSlib::htslib UNKNOWN IMPORTED)
    set_target_properties(
        HTSlib::htslib
        PROPERTIES
            IMPORTED_LOCATION "${HTSlib_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${HTSlib_INCLUDE_DIR}"
    )

    if(HUSK_STATIC AND PC_HTSLIB_STATIC_LIBRARIES)
        set(_htslib_link_libraries ${PC_HTSLIB_STATIC_LIBRARIES})
        list(FILTER _htslib_link_libraries EXCLUDE REGEX "^hts$")
        set_property(
            TARGET HTSlib::htslib
            PROPERTY INTERFACE_LINK_LIBRARIES ${_htslib_link_libraries}
        )
    elseif(PC_HTSLIB_LIBRARIES)
        set(_htslib_link_libraries ${PC_HTSLIB_LIBRARIES})
        list(FILTER _htslib_link_libraries EXCLUDE REGEX "^hts$")
        set_property(
            TARGET HTSlib::htslib
            PROPERTY INTERFACE_LINK_LIBRARIES ${_htslib_link_libraries}
        )
    endif()
endif()

mark_as_advanced(HTSlib_INCLUDE_DIR HTSlib_LIBRARY)
