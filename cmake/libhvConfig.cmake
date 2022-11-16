
include(SelectLibraryConfigurations)

find_path(libhv_INCLUDE_DIRS hv/hv.h)
message("libhv_INCLUDE_DIRS: " ${libhv_INCLUDE_DIRS})

find_library(libhv_LIBRARY_RELEASE NAMES hv PATHS "${CMAKE_CURRENT_LIST_DIR}/../../lib" NO_DEFAULT_PATH)

find_library(libhv_LIBRARY_DEBUG NAMES hv PATHS "${CMAKE_CURRENT_LIST_DIR}/../../debug/lib" NO_DEFAULT_PATH)
select_library_configurations(libhv)

if(NOT libhv_LIBRARY)
  set(libhv_FOUND FALSE)
  set(LIBHV_FOUND FALSE)
  return()
endif()

if(WIN32)
  find_file(libhv_LIBRARY_RELEASE_DLL NAMES hv.dll PATHS "${CMAKE_CURRENT_LIST_DIR}/../../bin" NO_DEFAULT_PATH)
  find_file(libhv_LIBRARY_DEBUG_DLL NAMES hv.dll PATHS "${CMAKE_CURRENT_LIST_DIR}/../../debug/bin" NO_DEFAULT_PATH)
endif()

# Manage Release Windows shared
if(EXISTS "${libhv_LIBRARY_RELEASE_DLL}")
  add_library(libhv SHARED IMPORTED)
  set_target_properties(libhv PROPERTIES
    IMPORTED_CONFIGURATIONS Release
    IMPORTED_LOCATION_RELEASE "${libhv_LIBRARY_RELEASE_DLL}"
    IMPORTED_IMPLIB_RELEASE "${libhv_LIBRARY_RELEASE}"
    INTERFACE_INCLUDE_DIRECTORIES "${libhv_INCLUDE_DIRS}"
  )
endif()

# Manage Debug Windows shared
if(EXISTS "${libhv_LIBRARY_DEBUG_DLL}")
  if(EXISTS "${libhv_LIBRARY_RELEASE_DLL}")
    #message("Debug mode")
    set_target_properties(libhv PROPERTIES
      IMPORTED_CONFIGURATIONS "Release;Debug"
      IMPORTED_LOCATION_RELEASE "${libhv_LIBRARY_RELEASE_DLL}"
      IMPORTED_IMPLIB_RELEASE "${libhv_LIBRARY_RELEASE}"
      IMPORTED_LOCATION_DEBUG "${libhv_LIBRARY_DEBUG_DLL}"
      IMPORTED_IMPLIB_DEBUG "${libhv_LIBRARY_DEBUG}"
      INTERFACE_INCLUDE_DIRECTORIES "${libhv_INCLUDE_DIRS}"    
    )
  else()  
    add_library(libhv SHARED IMPORTED)
    set_target_properties(libhv PROPERTIES
      IMPORTED_CONFIGURATIONS Debug
      IMPORTED_LOCATION_DEBUG "${libhv_LIBRARY_DEBUG_DLL}"
      IMPORTED_IMPLIB_DEBUG "${libhv_LIBRARY_DEBUG}"
      INTERFACE_INCLUDE_DIRECTORIES "${libhv_INCLUDE_DIRS}"
    )
  endif()
endif()

# Manage Release Windows static and Linux shared/static
if((NOT EXISTS "${libhv_LIBRARY_RELEASE_DLL}") AND (EXISTS "${libhv_LIBRARY_RELEASE}"))
  add_library(libhv UNKNOWN IMPORTED)
  set_target_properties(libhv PROPERTIES
    IMPORTED_CONFIGURATIONS Release
    IMPORTED_LOCATION_RELEASE "${libhv_LIBRARY_RELEASE}"
    INTERFACE_INCLUDE_DIRECTORIES "${libhv_INCLUDE_DIRS}"
  )
endif()

# Manage Debug Windows static and Linux shared/static
if((NOT EXISTS "${libhv_LIBRARY_DEBUG_DLL}") AND (EXISTS "${libhv_LIBRARY_DEBUG}"))
  if(EXISTS "${libhv_LIBRARY_RELEASE}")
    set_target_properties(libhv PROPERTIES
      IMPORTED_CONFIGURATIONS "Release;Debug"
      IMPORTED_LOCATION_RELEASE "${libhv_LIBRARY_RELEASE}"
      IMPORTED_LOCATION_DEBUG "${libhv_LIBRARY_DEBUG}"
      INTERFACE_INCLUDE_DIRECTORIES "${libhv_INCLUDE_DIRS}"   
    )
  else()
    add_library(libhv UNKNOWN IMPORTED)
    set_target_properties(libhv PROPERTIES
      IMPORTED_CONFIGURATIONS Debug
      IMPORTED_LOCATION_DEBUG "${libhv_LIBRARY_DEBUG}"
      INTERFACE_INCLUDE_DIRECTORIES "${libhv_INCLUDE_DIRS}"
    )
  endif()
endif()

set(libhv_FOUND TRUE)
set(LIBHV_FOUND TRUE)
