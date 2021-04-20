project(source-copy VERSION 0.1.1)
set(PROJECT_FULL_NAME "Source Copy")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

set(source-copy_HEADERS
	source-copy.hpp
	version.h)
set(source-copy_SOURCES
	source-copy.cpp)

if(WIN32)
	get_filename_component(ISS_FILES_DIR "${CMAKE_BINARY_DIR}\\..\\package" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_FILES_DIR}" ISS_FILES_DIR)
	get_filename_component(ISS_PACKAGE_DIR "${CMAKE_PACKAGE_PREFIX}\\.." ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_PACKAGE_DIR}" ISS_PACKAGE_DIR)
	get_filename_component(ISS_SOURCE_DIR "${PROJECT_SOURCE_DIR}" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_SOURCE_DIR}" ISS_SOURCE_DIR)
	configure_file("installer.iss.in"
			"${PROJECT_BINARY_DIR}/installer.iss"
		)
	configure_file(resource.rc.in source-copy.rc)
	list(APPEND source-copy_SOURCES
		source-copy.rc)
endif()

add_library(source-copy MODULE
	${source-copy_HEADERS}
	${source-copy_SOURCES})

target_link_libraries(source-copy
		obs-frontend-api
		Qt5::Widgets
		libobs)

set_target_properties(source-copy PROPERTIES FOLDER "plugins/exeldro")
install_obs_plugin_with_data(source-copy data)
