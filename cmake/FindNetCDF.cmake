#[==[
 Try to find NetCDF. Once done this will define:
  NetCDF_FOUND - System has NetCDF
  NetCDF_INCLUDE_DIRS - The NetCDF include directories
  NetCDF_LIBRARIES - The libraries needed to use NetCDF
  NetCDF_VERSION: The version of NetCDF found.
  NetCDF::NetCDF: A target to use with `target_link_libraries`.
#]==]

set(NetCDF_PREFIX "" CACHE STRING "NetCDF install directory")
if(NetCDF_PREFIX)
  message(STATUS "NetCDF_PREFIX ${NetCDF_PREFIX}")
endif()

find_path(NetCDF_INCLUDE_DIR NAMES netcdf PATHS "${NetCDF_PREFIX}/include" NO_DEFAULT_PATH)

find_library(NetCDF_LIBRARY NAMES netcdf-cxx4 PATHS "${NetCDF_PREFIX}/lib64" NO_DEFAULT_PATH)

if (NetCDF_FOUND)
  set(NetCDF_VERSION ${netCDF_VERSION})
  set(NetCDF_LIBRARIES ${NetCDF_LIBRARY} )
  set(NetCDF_INCLUDE_DIRS ${NetCDF_INCLUDE_DIR} )
endif()

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set NetCDF_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(
    NetCDF
    DEFAULT_MSG
    NetCDF_LIBRARY NetCDF_INCLUDE_DIR
)

mark_as_advanced(NetCDF_INCLUDE_DIR NetCDF_LIBRARY )

if (NetCDF_FOUND)
  message(STATUS "NetCDF version: ${NetCDF_VERSION} : ${netCDF_VERSION}")
  message(STATUS "NetCDF lib: ${NetCDF_LIBRARY}")
  message(STATUS "NetCDF include: ${NetCDF_INCLUDE_DIR}")
  if (NOT TARGET NetCDF::NetCDF)
    add_library(NetCDF::NetCDF UNKNOWN IMPORTED)
    set_target_properties(NetCDF::NetCDF PROPERTIES
      IMPORTED_LOCATION "${NetCDF_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${NetCDF_INCLUDE_DIR}")
  endif ()
endif ()

