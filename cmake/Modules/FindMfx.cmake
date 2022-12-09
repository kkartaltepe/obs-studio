# * Try to find libvpl/libmfx, prefers vpl
#
# Once done this will define
#
# MFX_FOUND - system has intel media sdk
# MFX_INCLUDE_DIRS - the intel media sdk include directory
# MFX_LIBRARIES - the libraries needed to use intel media sdk
# MFX_DEFINITIONS - Compiler switches required for using intel media sdk

# Use pkg-config to get the directories and then use these values in the
# find_path() and find_library() calls

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(_MFX mfx)
	pkg_check_modules(_VPL vpl)
endif()

find_path(
  MFX_INCLUDE_DIR
  NAMES mfxstructures.h
  HINTS ${_VPL_INCLUDE_DIRS} ${_MFX_INCLUDE_DIRS}
  PATHS /usr/include /usr/local/include /opt/local/include /sw/include
  PATH_SUFFIXES vpl/ mfx/)

find_library(
  MFX_LIB
  NAMES ${_VPL_LIBRARIES} ${_MFX_LIBRARIES} vpl mfx
  HINTS ${_VPL_LIBRARY_DIRS} ${_MFX_LIBRARY_DIRS}
  PATHS /usr/lib /usr/local/lib /opt/local/lib /sw/lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Mfx REQUIRED_VARS MFX_LIB MFX_INCLUDE_DIR)
mark_as_advanced(MFX_INCLUDE_DIR MFX_LIB)

if(MFX_FOUND)
  set(MFX_INCLUDE_DIRS ${MFX_INCLUDE_DIR})
  set(MFX_LIBRARIES ${MFX_LIB})

  if(NOT TARGET MFX::MFX)
    if(IS_ABSOLUTE "${MFX_LIBRARIES}")
      add_library(MFX::MFX UNKNOWN IMPORTED)
      set_target_properties(MFX::MFX PROPERTIES IMPORTED_LOCATION
                                                "${MFX_LIBRARIES}")
    else()
      add_library(MFX::MFX INTERFACE IMPORTED)
      set_target_properties(MFX::MFX PROPERTIES IMPORTED_LIBNAME
                                                "${MFX_LIBRARIES}")
    endif()

    set_target_properties(MFX::MFX PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                              "${MFX_INCLUDE_DIRS}")
  endif()
endif()
