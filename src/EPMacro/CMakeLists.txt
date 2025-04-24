cmake_minimum_required(VERSION 3.19)

project(EPMacro)

add_executable(EPMacro main.cpp)

# See https://en.cppreference.com/w/cpp/filesystem#Notes
if((CMAKE_COMPILER_IS_GNUCXX AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
   OR (("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang") AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS
                                                                                                              9.0)))
  target_link_libraries(EPMacro PRIVATE stdc++fs)
endif()
set_target_properties(EPMacro PROPERTIES VERSION ${ENERGYPLUS_VERSION})
set_target_properties(EPMacro PROPERTIES FOLDER Auxiliary)

install(TARGETS EPMacro DESTINATION ./)

if(APPLE AND CPACK_CODESIGNING_DEVELOPPER_ID_APPLICATION)
  include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CodeSigning.cmake")
  register_install_codesign_target(EPMacro ".")
endif()
