# Story 034: the deathmatch rules of the field validation server as a
# Metamod-FWGS plugin (csdm.cpp). Built against the ReGameDLL_CS sources the
# game library comes from (cs16-client), with the definitions of its build,
# so that the classes have the game library's layout.
#
# cmake -S scripts/server/csdm -B build -DREGAMEDLL_DIR=<ReGameDLL_CS>/regamedll \
#	-DMETAMOD_INCLUDE=<metamod-plugin-template>/include/metamod
cmake_minimum_required(VERSION 3.12)
project(csdm LANGUAGES CXX)

set(REGAMEDLL_DIR "" CACHE PATH "regamedll directory of ReGameDLL_CS")
set(METAMOD_INCLUDE "" CACHE PATH "Metamod-FWGS SDK headers")

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

add_library(csdm SHARED csdm.cpp)
set_target_properties(csdm PROPERTIES
	OUTPUT_NAME csdm_mm_amd64
	PREFIX ""
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON
	CXX_VISIBILITY_PRESET hidden)

target_include_directories(csdm SYSTEM PRIVATE
	${METAMOD_INCLUDE}
	${REGAMEDLL_DIR}
	${REGAMEDLL_DIR}/regamedll
	${REGAMEDLL_DIR}/common
	${REGAMEDLL_DIR}/dlls
	${REGAMEDLL_DIR}/engine
	${REGAMEDLL_DIR}/game_shared
	${REGAMEDLL_DIR}/pm_shared
	${REGAMEDLL_DIR}/public
	${REGAMEDLL_DIR}/public/regamedll)

# those of ReGameDLL_CS' own build (regamedll/CMakeLists.txt)
target_compile_definitions(csdm PRIVATE
	REGAMEDLL_FIXES REGAMEDLL_API REGAMEDLL_ADD UNICODE_FIXES BUILD_LATEST CLIENT_WEAPONS USE_QSTRING NDEBUG
	_LINUX LINUX _GLIBCXX_USE_CXX11_ABI=0
	_stricmp=strcasecmp _strnicmp=strncasecmp _strdup=strdup _unlink=unlink
	_snprintf=snprintf _vsnprintf=vsnprintf _write=write _close=close _access=access _vsnwprintf=vswprintf)

target_compile_options(csdm PRIVATE -Wall -Wno-unused-variable -Wno-unused-function -Wno-write-strings -Wno-unknown-pragmas)
target_link_libraries(csdm PRIVATE dl)
