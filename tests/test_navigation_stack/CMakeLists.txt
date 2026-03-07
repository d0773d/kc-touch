cmake_minimum_required(VERSION 3.16)

set(IDF_COMPONENT_MANAGER 0)

set(EXTRA_COMPONENT_DIRS
	"${CMAKE_SOURCE_DIR}/../../components"
	"${CMAKE_SOURCE_DIR}/../../managed_components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_navigation_stack)