project(minetestmapper CXX C)
cmake_minimum_required(VERSION 2.6)
cmake_policy(SET CMP0003 NEW)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

if(WIN32)
	message(WARNING "Thanks for using minetestmapper on Windows.\nAs I do not have a windows computer at my disposal, I have not been able\nto test some windows-specific parts.\nPlease let me know if minetestmapper compiles OK, and if it runs fine.\nThanks!\n(Contact me via github)")
endif(WIN32)

# Try to compute a useful version number. (goal: uniquely identify the commit used to generate it)
# Prefer the output of git describe (if available)
# Else, use a stored version, and a checksum of the files (and hope that is dependable)

# Obtain stored major version number
# Make sure cmake is rerun when version has changed
configure_file(base-version base-version.dup COPYONLY)
file(REMOVE base-version.dup)
file(STRINGS "${CMAKE_HOME_DIRECTORY}/base-version" VERSION_FROM_FILE)

execute_process(COMMAND git describe --long "--match=[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]" --dirty=-WIP --abbrev=8
		RESULT_VARIABLE VERSION_EXIT OUTPUT_VARIABLE GIT_VERSION ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(VERSION_EXIT)
	set(VERSION_MAJOR "${VERSION_FROM_FILE}")
	file(GLOB MAPPER_FILES RELATIVE "${CMAKE_HOME_DIRECTORY}" *.cpp *.h CMakeLists.txt)
	string(REGEX REPLACE "[^;]" "" MAPPER_FILES_COUNT "${MAPPER_FILES};")
	string(LENGTH "${MAPPER_FILES_COUNT}" MAPPER_FILES_COUNT)
	execute_process(COMMAND cmake -E md5sum ${MAPPER_FILES} RESULT_VARIABLE VERSION_EXIT OUTPUT_VARIABLE FILES_MD5SUM ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
	if(VERSION_EXIT)
		message(STATUS "Could not compute md5sum for files")
		set(VERSION_MINOR "0")
	else(VERSION_EXIT)
		string(MD5 SUMMARY_MD5SUM "${FILES_MD5SUM}")
		string(SUBSTRING "${SUMMARY_MD5SUM}" 1 16 VERSION_MINOR)
		set(VERSION_MINOR "N${MAPPER_FILES_COUNT}-CHK${VERSION_MINOR}")
	endif(VERSION_EXIT)
	set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}")
	message(STATUS "No git tree found; stored / computed version: ${VERSION_STRING}")
else(VERSION_EXIT)
	string(REGEX REPLACE "-(.*)" ".\\1" VERSION_STRING "${GIT_VERSION}")
	string(REGEX REPLACE "^([^.]+)\\.(.*)" "\\1" VERSION_MAJOR "${VERSION_STRING}")
	string(REGEX REPLACE "^([^.]+)\\.(.*)" "\\2" VERSION_MINOR "${VERSION_STRING}")
	set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}")
	message(STATUS "Git tree found; git commit version: ${VERSION_STRING}")
	if(NOT VERSION_STRING STREQUAL VERSION_FROM_FILE AND NOT VERSION_MAJOR STREQUAL VERSION_FROM_FILE)
		message(STATUS "**NOTE: contents of file 'version' do not match output of 'git describe' (this is harmless)")
	endif(NOT VERSION_STRING STREQUAL VERSION_FROM_FILE AND NOT VERSION_MAJOR STREQUAL VERSION_FROM_FILE)
endif(VERSION_EXIT)

# try_compile does not remove the temporary build directory, so put it in CMakeFiles...
# Also: use absolute paths; cmake chokes on relative paths :-(
try_compile(CXX_C0X_SUPPORTED "${CMAKE_HOME_DIRECTORY}/CMakeFiles/CMakeTmp/c0x-test" "${CMAKE_HOME_DIRECTORY}/empty.cpp" CMAKE_FLAGS "-std=c++0x")
if(CXX_C0X_SUPPORTED)
	# Yes, I *know* c++0x is *not* an official C++ standard...
	message(STATUS "Compiler: C++ standard version 'c++0x' is supported")
else(CXX_C0X_SUPPORTED)
	message(STATUS "Compiler: C++ standard version c++0x is not supported")
	message(FATAL_ERROR "Your compiler is not modern enough to compile this code\nPlease upgrade your compiler to one supporting at least c++0x.\n(or submit a bug report)")
endif(CXX_C0X_SUPPORTED)
try_compile(CXX_C11_SUPPORTED "${CMAKE_HOME_DIRECTORY}/CMakeFiles/CMakeTmp/c11-test" "${CMAKE_HOME_DIRECTORY}/empty.cpp" CMAKE_FLAGS "-std=c++11")
if(CXX_C11_SUPPORTED)
	message(STATUS "Compiler: C++ standard version 'c++11' is supported")
else(CXX_C11_SUPPORTED)
	message(STATUS "Compiler: C++ standard version 'c++11' is not supported")
endif(CXX_C11_SUPPORTED)

# Determine C++ standard to use
# If CXX_CXX_STANDARD is empty, use autodetected c++ standard version
set(CXX_CXX_STANDARD "" CACHE STRING "C++ standard version to use. Leave empty, or set to c++11 or c++0x")
string(COMPARE NOTEQUAL "${CXX_CXX_STANDARD}" "" CXX_CXX_STANDARD_OVERRIDE)
if(CXX_CXX_STANDARD_OVERRIDE)
	string(COMPARE EQUAL "${CXX_CXX_STANDARD}" "c++11" CXX_C11_REQUESTED)
	string(COMPARE EQUAL "${CXX_CXX_STANDARD}" "c++0x" CXX_C0X_REQUESTED)
	if(CXX_C11_REQUESTED AND CXX_C11_SUPPORTED)
		message(STATUS "Compiler: C++ standard version 'c++11' selected (as requested)")
		set(CXX_USE_CXX_STANDARD "c++11")
	elseif(CXX_C11_REQUESTED AND NOT CXX_C11_SUPPORTED)
		message(SEND_ERROR "Compiler: requested standard version '${CXX_CXX_STANDARD}' is not supported")
	elseif(CXX_C0X_REQUESTED)
		message(STATUS "Compiler: C++ standard version 'c++0x' selected (as requested)")
		set(CXX_USE_CXX_STANDARD "c++0x")
	else(CXX_C11_REQUESTED AND CXX_C11_SUPPORTED)
		message(FATAL_ERROR "Unrecognised c++ standard version requested: ${CXX_CXX_STANDARD}")
	endif(CXX_C11_REQUESTED AND CXX_C11_SUPPORTED)
else(CXX_CXX_STANDARD_OVERRIDE)
	if(CXX_C11_SUPPORTED)
		message(STATUS "Compiler: C++ standard version 'c++11' selected")
		set(CXX_USE_CXX_STANDARD "c++11")
	else(CXX_C11_SUPPORTED)
		message(STATUS "Compiler: C++ standard version 'c++0x' selected (c++11 not supported)")
		set(CXX_USE_CXX_STANDARD "c++0x")
	endif(CXX_C11_SUPPORTED)
endif(CXX_CXX_STANDARD_OVERRIDE)

# Clean targets after change in c++ standard, and remember current standard
string(COMPARE EQUAL "${CXX_CXX_STANDARD_LAST}" "" FIRST_CXX_STANDARD)
string(COMPARE NOTEQUAL "${CXX_CXX_STANDARD_LAST}" "${CXX_USE_CXX_STANDARD}" DIFFERENT_CXX_STANDARD)
set(CXX_CXX_STANDARD_LAST_TEMP "${CXX_CXX_STANDARD_LAST}")
set(CXX_CXX_STANDARD_LAST "${CXX_USE_CXX_STANDARD}" CACHE INTERNAL "Internal use - do not modify")
if(DIFFERENT_CXX_STANDARD AND NOT FIRST_CXX_STANDARD)
	execute_process(COMMAND "${CMAKE_BUILD_TOOL}" clean RESULT_VARIABLE CLEANING_RESULT OUTPUT_QUIET ERROR_QUIET)
	if(CLEANING_RESULT)
		message(STATUS "Clean previous build because of standard change (was: '${CXX_CXX_STANDARD_LAST_TEMP}') (exit: ${CLEANING_RESULT})")
	else(CLEANING_RESULT)
		message(STATUS "Clean previous build because of standard change (was: '${CXX_CXX_STANDARD_LAST_TEMP}')")
	endif(CLEANING_RESULT)
endif(DIFFERENT_CXX_STANDARD AND NOT FIRST_CXX_STANDARD)

set(CMAKE_CONFIGURATION_TYPES "Release|Debug")
string(COMPARE EQUAL "${CMAKE_BUILD_TYPE}" "" EMPTY_BUILD_TYPE)
if(EMPTY_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release")
	message(STATUS "Build type not set ('Debug' or 'Release') - selecting 'Release'")
endif(EMPTY_BUILD_TYPE)

set(CMAKE_CXX_FLAGS         "-std=${CXX_USE_CXX_STANDARD} -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -Wextra -DDEBUG")


# Find libgd
find_library(LIBGD_LIBRARY gd)
find_path(LIBGD_INCLUDE_DIR gd.h)
message (STATUS "libgd library: ${LIBGD_LIBRARY}")
message (STATUS "libgd headers: ${LIBGD_INCLUDE_DIR}")
if(NOT LIBGD_LIBRARY OR NOT LIBGD_INCLUDE_DIR)
	message(SEND_ERROR "libgd not found!")
endif(NOT LIBGD_LIBRARY OR NOT LIBGD_INCLUDE_DIR)

# Find zlib
find_library(ZLIB_LIBRARY z)
find_path(ZLIB_INCLUDE_DIR zlib.h)
message (STATUS "zlib library: ${ZLIB_LIBRARY}")
message (STATUS "zlib headers: ${ZLIB_INCLUDE_DIR}")
if(NOT ZLIB_LIBRARY OR NOT ZLIB_INCLUDE_DIR)
	message(SEND_ERROR "zlib not found!")
endif(NOT ZLIB_LIBRARY OR NOT ZLIB_INCLUDE_DIR)

# Find iconv
OPTION(ENABLE_ICONV "Enable character encoding conversion of text written on the map" True)
if(ENABLE_ICONV)
	find_package(Iconv)
	if(ICONV_FOUND)
		set(USE_ICONV 1)
		set(MAPPER_SRCS_ICONV CharEncodingConverterIConv.cpp)
		message (STATUS "iconv libraries: ${ICONV_LIBRARIES}")
	else(ICONV_FOUND)
		message (SEND_ERROR "iconv libraries not found")
	endif(ICONV_FOUND)
else(ENABLE_ICONV)
	set(USE_ICONV 0)
endif(ENABLE_ICONV)

find_package(PkgConfig)
include(FindPackageHandleStandardArgs)

# Find database(s)
set(USE_SQLITE3 0)
set(USE_POSTGRESQL 0)
set(USE_LEVELDB 0)
set(USE_REDIS 0)

OPTION(ENABLE_ANY_DATABASE "Enable any available database backends" True)
OPTION(ENABLE_ALL_DATABASES "Enable all possible database backends")
OPTION(ENABLE_SQLITE3 "Enable sqlite3 backend" True)
OPTION(ENABLE_POSTGRESQL "Enable postgresql backend")
OPTION(ENABLE_LEVELDB "Enable LevelDB backend")
OPTION(ENABLE_REDIS "Enable redis backend")

# Find sqlite3
if(ENABLE_SQLITE3 OR ENABLE_ANY_DATABASE OR ENABLE_ALL_DATABASES)
	find_library(SQLITE3_LIBRARY sqlite3)
	find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
	message (STATUS "sqlite3 library: ${SQLITE3_LIBRARY}")
	message (STATUS "sqlite3 headers: ${SQLITE3_INCLUDE_DIR}")
	if(SQLITE3_LIBRARY AND SQLITE3_INCLUDE_DIR)
		set(USE_SQLITE3 1)
		message(STATUS "sqlite3 backend enabled")
		include_directories(${SQLITE3_INCLUDE_DIR})
	else(SQLITE3_LIBRARY AND SQLITE3_INCLUDE_DIR)
		set(USE_SQLITE3 0)
		if(ENABLE_SQLITE3 OR ENABLE_ALL_DATABASES)
			message(SEND_ERROR "sqlite3 backend requested but sqlite3 libraries not found!")
		else(ENABLE_SQLITE3 OR ENABLE_ALL_DATABASES)
			message(STATUS "sqlite3 not enabled (sqlite3 libraries and/or headers not found)")
		endif(ENABLE_SQLITE3 OR ENABLE_ALL_DATABASES)
	endif(SQLITE3_LIBRARY AND SQLITE3_INCLUDE_DIR)
endif(ENABLE_SQLITE3 OR ENABLE_ANY_DATABASE OR ENABLE_ALL_DATABASES)

# Find postgresql
if(ENABLE_POSTGRESQL OR ENABLE_ANY_DATABASE OR ENABLE_ALL_DATABASES)
	find_library(POSTGRESQL_LIBRARY pq)
	find_path(POSTGRESQL_INCLUDE_DIR libpq-fe.h PATH_SUFFIXES postgresql)
	message (STATUS "postgresql library: ${POSTGRESQL_LIBRARY}")
	message (STATUS "postgresql headers: ${POSTGRESQL_INCLUDE_DIR}")
	if(POSTGRESQL_LIBRARY AND POSTGRESQL_INCLUDE_DIR)
		set(USE_POSTGRESQL 1)
		message(STATUS "postgresql backend enabled")
		include_directories(${POSTGRESQL_INCLUDE_DIR})
	else(POSTGRESQL_LIBRARY AND POSTGRESQL_INCLUDE_DIR)
		set(USE_POSTGRESQL 0)
		if(ENABLE_POSTGRESQL OR ENABLE_ALL_DATABASES)
			message(SEND_ERROR "postgresql backend requested but postgresql libraries not found!")
		else(ENABLE_POSTGRESQL OR ENABLE_ALL_DATABASES)
			message(STATUS "postgresql not enabled (postgresql libraries and/or headers not found)")
		endif(ENABLE_POSTGRESQL OR ENABLE_ALL_DATABASES)
	endif(POSTGRESQL_LIBRARY AND POSTGRESQL_INCLUDE_DIR)
endif(ENABLE_POSTGRESQL OR ENABLE_ANY_DATABASE OR ENABLE_ALL_DATABASES)

# Find leveldb
if(ENABLE_LEVELDB OR ENABLE_ANY_DATABASE OR ENABLE_ALL_DATABASES)
	find_library(LEVELDB_LIBRARY leveldb)
	find_path(LEVELDB_INCLUDE_DIR db.h PATH_SUFFIXES leveldb)
	message (STATUS "LevelDB library: ${LEVELDB_LIBRARY}")
	message (STATUS "LevelDB headers: ${LEVELDB_INCLUDE_DIR}")
	if(LEVELDB_LIBRARY AND LEVELDB_INCLUDE_DIR)
		set(USE_LEVELDB 1)
		message(STATUS "LevelDB backend enabled")
		include_directories(${LEVELDB_INCLUDE_DIR})
	else(LEVELDB_LIBRARY AND LEVELDB_INCLUDE_DIR)
		set(USE_LEVELDB 0)
		if(ENABLE_LEVELDB OR ENABLE_ALL_DATABASES)
			message(SEND_ERROR "leveldb backend requested but leveldb libraries not found!")
		else(ENABLE_LEVELDB OR ENABLE_ALL_DATABASES)
			message(STATUS "leveldb not enabled (leveldb libraries and/or headers not found)")
		endif(ENABLE_LEVELDB OR ENABLE_ALL_DATABASES)
	endif(LEVELDB_LIBRARY AND LEVELDB_INCLUDE_DIR)
endif(ENABLE_LEVELDB OR ENABLE_ANY_DATABASE OR ENABLE_ALL_DATABASES)

# Find redis
if(ENABLE_REDIS OR ENABLE_ANY_DATABASE OR ENABLE_ALL_DATABASES)
	find_library(REDIS_LIBRARY hiredis)
	find_path(REDIS_INCLUDE_DIR hiredis.h PATH_SUFFIXES hiredis)
	message (STATUS "redis library: ${REDIS_LIBRARY}")
	message (STATUS "redis headers: ${REDIS_INCLUDE_DIR}")
	if(REDIS_LIBRARY AND REDIS_INCLUDE_DIR)
		set(USE_REDIS 1)
		message(STATUS "redis backend enabled")
		include_directories(${REDIS_INCLUDE_DIR})
	else(REDIS_LIBRARY AND REDIS_INCLUDE_DIR)
		set(USE_REDIS 0)
		if(ENABLE_REDIS OR ENABLE_ALL_DATABASES)
			message(SEND_ERROR "redis backend requested but redis libraries not found!")
		else(ENABLE_REDIS OR ENABLE_ALL_DATABASES)
			message(STATUS "redis not enabled (redis libraries and/or headers not found)")
		endif(ENABLE_REDIS OR ENABLE_ALL_DATABASES)
	endif(REDIS_LIBRARY AND REDIS_INCLUDE_DIR)
endif(ENABLE_REDIS OR ENABLE_ANY_DATABASE OR ENABLE_ALL_DATABASES)

if(NOT USE_SQLITE3 AND NOT USE_POSTGRESQL AND NOT USE_LEVELDB AND NOT USE_REDIS)
	message(SEND_ERROR "No database backends are configured, or none could be found")
endif(NOT USE_SQLITE3 AND NOT USE_POSTGRESQL AND NOT USE_LEVELDB AND NOT USE_REDIS)

include_directories(
	"${PROJECT_BINARY_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_BINARY_DIR}"
	${LIBGD_INCLUDE_DIR}
	${ZLIB_INCLUDE_DIR}
	${ICONV_INCLUDE_DIR}
)

set(mapper_SRCS
	PixelAttributes.cpp
	PlayerAttributes.cpp
	TileGenerator.cpp
	ZlibDecompressor.cpp
	Color.cpp
	Settings.cpp
	BlockPos.cpp
	mapper.cpp
	CharEncodingConverter.cpp
	${MAPPER_SRCS_ICONV}
)

set(LINK_LIBRARIES
	minetestmapper
	${LIBGD_LIBRARY}
	${ZLIB_LIBRARY}
	${ICONV_LIBRARIES}
)

if(USE_SQLITE3)
	set(mapper_SRCS ${mapper_SRCS} db-sqlite3.cpp)
	set(LINK_LIBRARIES ${LINK_LIBRARIES} ${SQLITE3_LIBRARY})
endif(USE_SQLITE3)

if(USE_POSTGRESQL)
	set(mapper_SRCS ${mapper_SRCS} db-postgresql.cpp)
	set(LINK_LIBRARIES ${LINK_LIBRARIES} ${POSTGRESQL_LIBRARY})
endif(USE_POSTGRESQL)

if(USE_LEVELDB)
	set(mapper_SRCS ${mapper_SRCS} db-leveldb.cpp)
	set(LINK_LIBRARIES ${LINK_LIBRARIES} ${LEVELDB_LIBRARY})
endif(USE_LEVELDB)

if(USE_REDIS)
	set(mapper_SRCS ${mapper_SRCS} db-redis.cpp)
	set(LINK_LIBRARIES ${LINK_LIBRARIES} ${REDIS_LIBRARY})
endif(USE_REDIS)

add_executable(minetestmapper
	${mapper_SRCS}
)
target_link_libraries(
	${LINK_LIBRARIES}
)

add_subdirectory(doc)

# CPack

file(GLOB META_FILES RELATIVE "${CMAKE_HOME_DIRECTORY}" AUTHORS COPYING LICENSE.* README.rst)
file(GLOB DOC_RST_FILES RELATIVE "${CMAKE_HOME_DIRECTORY}" doc/*.rst)
string(REPLACE ".rst" ".html" DOC_HTML_FILES "${DOC_RST_FILES}")
file(GLOB DOC_IMAGE_FILES RELATIVE "${CMAKE_HOME_DIRECTORY}" doc/images/*)
if(USE_RST2HTML)
	set(DOC_HTML_FILES_PACKAGE "${DOC_HTML_FILES}")
else(USE_RST2HTML)
	set(DOC_HTML_FILES_PACKAGE "")
endif(USE_RST2HTML)
set(COLORS_FILES
	colors.txt
	heightmap-nodes.txt
	heightmap-colors.txt
	heightmap-colors-rainbow.txt
	colors-average-alpha.txt
	colors-cumulative-alpha.txt
)
set(DUMPNODES_FILES
	dumpnodes/README.dumpnodes
	dumpnodes/init.lua
)
set(DUMPNODES_MOD_FILES
	dumpnodes/init.lua
)
set(DUMPNODES_SCRIPTS
	dumpnodes/avgcolor.py
	dumpnodes/mkcolors
)

#set(CPACK_SET_DESTDIR ON)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Map generator for Minetest")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_HOME_DIRECTORY}/package-description.txt")
set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH 1)
set(CPACK_PACKAGE_VENDOR "Minetestmapper")
set(CPACK_PACKAGE_CONTACT "(Unknown)")
set(CPACK_PACKAGE_URL "https://github.com/Rogier-5/minetest-mapper-cpp")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}-${VERSION_STRING}")
set(CPACK_PACKAGE_EXECUTABLES minetestmapper)
set(CPACK_PROJECT_CONFIG_FILE "${PROJECT_BINARY_DIR}/CMakeCPackLists.txt")

# Make install as root leaves a root-owned file behind that interferes
# with package creation - remove it.
if(EXISTS install_manifest.txt)
	file(REMOVE install_manifest.txt)
endif(EXISTS install_manifest.txt)

set(ARCHIVE_PACKAGE_NAME "" CACHE STRING "Name of the .zip / .tar.gz archive package to be created by cpack (without extension)")
if(NOT WIN32)
	set(CREATE_FLAT_PACKAGE TRUE CACHE BOOL "Create the .tar.gz package without included directory hierarchy (must be OFF for 'make install' and to create .deb, .rpm packages)")
endif(NOT WIN32)

if(WIN32)
	set(PACKAGING_FLAT 1)
	set(CPACK_GENERATOR ZIP)
	set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${VERSION_STRING}-win32")

	install(FILES ${META_FILES} DESTINATION ".")
	install(FILES ${COLORS_FILES} DESTINATION ".")
	install(FILES ${DOC_RST_FILES} DESTINATION "doc")
	install(FILES ${DOC_HTML_FILES_PACKAGE} DESTINATION "doc")
	install(FILES ${DOC_IMAGE_FILES} DESTINATION "doc/images")
	install(FILES ${DUMPNODES_FILES} DESTINATION "dumpnodes")
	install(PROGRAMS ${DUMPNODES_SCRIPTS} DESTINATION "dumpnodes")
	install(PROGRAMS "${PROJECT_BINARY_DIR}/minetestmapper.exe" DESTINATION ".")

elseif(CREATE_FLAT_PACKAGE)
	set(PACKAGING_FLAT 1)
	message(WARNING "CREATE_FLAT_PACKAGE is set ON: creating a flat package.\nFor 'make install', .deb and .rpm packages, CREATE_FLAT_PACKAGE must be OFF")
	set(CPACK_GENERATOR TGZ)
	set(CPACK_SOURCE_GENERATOR TGZ)
	if(ARCHIVE_PACKAGE_NAME)
	    set(CPACK_TGZ_PACKAGE_FILE_NAME "${ARCHIVE_PACKAGE_NAME}")
	else(ARCHIVE_PACKAGE_NAME)
		if (APPLE)
			set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${VERSION_MAJOR}-MacOSX-flat")
		else(APPLE)
			set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${VERSION_MAJOR}-Linux-flat")
		endif(APPLE)
	endif(ARCHIVE_PACKAGE_NAME)
	message(STATUS "Flat archive package name: ${CPACK_PACKAGE_FILE_NAME}.tar.gz")

	install(FILES ${META_FILES} DESTINATION ".")
	install(FILES ${COLORS_FILES} DESTINATION ".")
	install(FILES ${DOC_RST_FILES} DESTINATION "doc")
	install(FILES ${DOC_HTML_FILES_PACKAGE} DESTINATION "doc")
	install(FILES ${DOC_IMAGE_FILES} DESTINATION "doc/images")
	install(FILES ${DUMPNODES_FILES} DESTINATION "dumpnodes")
	install(PROGRAMS ${DUMPNODES_SCRIPTS} DESTINATION "dumpnodes")
	install(PROGRAMS "${PROJECT_BINARY_DIR}/minetestmapper" DESTINATION ".")

else(WIN32)
	set(PACKAGING_FLAT 0)
	set(CPACK_GENERATOR TGZ)
	set(CPACK_SOURCE_GENERATOR TGZ)

	# Determine binary architecture
	execute_process(COMMAND ${CMAKE_CXX_COMPILER} -v
			RESULT_VARIABLE VERSION_EXIT
			OUTPUT_VARIABLE PACKAGE_TARGET_ARCHITECTURE
			ERROR_VARIABLE PACKAGE_TARGET_ARCHITECTURE
			OUTPUT_STRIP_TRAILING_WHITESPACE)
	string(REGEX REPLACE ".*[\n\r]Target:[ \t\n\r]*([^ \t\n\r]+).*" "\\1" PACKAGE_TARGET_ARCHITECTURE "${PACKAGE_TARGET_ARCHITECTURE}")
	if(VERSION_EXIT)
		message(FATAL_ERROR "Could not determine target architecture")
	elseif(PACKAGE_TARGET_ARCHITECTURE MATCHES "[ \t\n\r]")
		message(FATAL_ERROR "Could not determine target architecture - error parsing compiler messages")
	else(VERSION_EXIT)
		message(STATUS "Target architecture: ${PACKAGE_TARGET_ARCHITECTURE}")
	endif(VERSION_EXIT)

	# default package name (i.e. for .tar.gz)
	if(ARCHIVE_PACKAGE_NAME)
	    set(CPACK_TGZ_PACKAGE_FILE_NAME "${ARCHIVE_PACKAGE_NAME}")
	else(ARCHIVE_PACKAGE_NAME)
	    set(CPACK_TGZ_PACKAGE_FILE_NAME "${PROJECT_NAME}-${VERSION_STRING}-${PACKAGE_TARGET_ARCHITECTURE}")
	endif(ARCHIVE_PACKAGE_NAME)
	message(STATUS "Archive package name: ${CPACK_TGZ_PACKAGE_FILE_NAME}.tar.gz")

	install(FILES ${META_FILES} DESTINATION "share/doc/${PROJECT_NAME}" COMPONENT mapper)
	install(FILES ${COLORS_FILES} DESTINATION "share/games/${PROJECT_NAME}" COMPONENT mapper)
	install(FILES ${DOC_RST_FILES} DESTINATION "share/doc/${PROJECT_NAME}/doc" COMPONENT mapper)
	install(FILES ${DOC_HTML_FILES_PACKAGE} DESTINATION "share/doc/${PROJECT_NAME}/doc" COMPONENT mapper)
	install(FILES ${DOC_IMAGE_FILES} DESTINATION "share/doc/${PROJECT_NAME}/doc/images" COMPONENT mapper)
	install(FILES ${DUMPNODES_FILES} DESTINATION "share/doc/${PROJECT_NAME}/dumpnodes" COMPONENT mapper)
	install(PROGRAMS ${DUMPNODES_SCRIPTS} DESTINATION "share/doc/${PROJECT_NAME}/dumpnodes" COMPONENT mapper)
	install(TARGETS minetestmapper RUNTIME DESTINATION bin COMPONENT mapper)

	if(CMAKE_INSTALL_PREFIX STREQUAL "/usr")
		# Require install prefix to be /usr when building .deb and .rpm packages
		# (else minetestmapper will not find the default colors.txt file)

		# When installing into /usr, assume minetest is also installed in /usr.
		install(FILES ${DUMPNODES_MOD_FILES} DESTINATION "share/games/minetest/mods/dumpnodes" COMPONENT mapper)

		# .deb settings
		# Debian package building needs xxxxx, but cpack doesn't check for it first...
		find_program(DPKG_AVAILABLE "dpkg")
		# Fakeroot is needed to get correct file ownership (root/root) in the package
		find_program(FAKEROOT_AVAILABLE "fakeroot")
		if(DPKG_AVAILABLE AND FAKEROOT_AVAILABLE)
			message(STATUS "dpkg and fakeroot found - enabling .deb package generation")
			set(CPACK_GENERATOR ${CPACK_GENERATOR} DEB)

			# Cmake does not support delayed variable expansion. No alternative but to compute things ourselves :-((((
			# Maybe we should be checking PACKAGE_TARGET_ARCHITECTURE as well (and add specific code for every architecture :-()
			# (on a multiarch system, the only compiler installed may not be the native one...)
			execute_process(COMMAND dpkg --print-architecture
					RESULT_VARIABLE VERSION_EXIT
					OUTPUT_VARIABLE DEBIAN_PACKAGE_ARCHITECTURE
					ERROR_QUIET
					OUTPUT_STRIP_TRAILING_WHITESPACE)
			if(VERSION_EXIT)
				message(FATAL_ERROR "Could not determine target architecture for debian package")
			else(VERSION_EXIT)
				message(STATUS "Debian package architecture: ${DEBIAN_PACKAGE_ARCHITECTURE}")
			endif(VERSION_EXIT)

			set(CPACK_DEBIAN_PACKAGE_FILE_NAME "${PROJECT_NAME}_${VERSION_STRING}-${CPACK_PACKAGE_VERSION_PATCH}_${DEBIAN_PACKAGE_ARCHITECTURE}")
			message(STATUS "Debian package name: ${CPACK_DEBIAN_PACKAGE_FILE_NAME}.deb")
			set(CPACK_DEBIAN_PACKAGE_VERSION "${VERSION_STRING}")
			file(READ "${CPACK_PACKAGE_DESCRIPTION_FILE}" CPACK_DEBIAN_PACKAGE_DESCRIPTION)
			# Unfortunately, cpack does not use (and adequately format) the description file - must do it ourselves
			string(STRIP "${CPACK_DEBIAN_PACKAGE_DESCRIPTION}" CPACK_DEBIAN_PACKAGE_DESCRIPTION)
			string(REGEX REPLACE "\n" "\n " CPACK_DEBIAN_PACKAGE_DESCRIPTION " ${CPACK_DEBIAN_PACKAGE_DESCRIPTION}")
			string(REGEX REPLACE "(^ |\n )[ \t]*\n" "\\1.\n" CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_DEBIAN_PACKAGE_DESCRIPTION}")
			set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}\n${CPACK_DEBIAN_PACKAGE_DESCRIPTION}")
			set(CPACK_DEBIAN_PACKAGE_ENHANCES "minetest")
			set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS 1)
			set(CPACK_DEBIAN_PACKAGE_SECTION "games")
			set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_URL}")
		else(DPKG_AVAILABLE AND FAKEROOT_AVAILABLE)
			if(NOT DPKG_AVAILABLE)
				message(STATUS "dpkg not found - no .deb package will be generated")
			endif(NOT DPKG_AVAILABLE)
			if(NOT FAKEROOT_AVAILABLE)
				message(STATUS "fakeroot not found - no .deb package will be generated")
			endif(NOT FAKEROOT_AVAILABLE)
		endif(DPKG_AVAILABLE AND FAKEROOT_AVAILABLE)
		unset(FAKEROOT_AVAILABLE CACHE)
		unset(DPKG_AVAILABLE CACHE)

		# .rpm settings
		# RPM package building needs rpmbuild, but cpack doesn't check for it first...
		find_program(RPM_AVAILABLE "rpmbuild")
		if(RPM_AVAILABLE)
			message(STATUS "rpmbuild found - enabling .rpm package generation")
			set(CPACK_GENERATOR ${CPACK_GENERATOR} RPM)

			set(CPACK_RPM_PACKAGE_FILE_NAME "${PROJECT_NAME}-${VERSION_STRING}-${CPACK_PACKAGE_VERSION_PATCH}-${PACKAGE_TARGET_ARCHITECTURE}")
			message(STATUS "Rpm package name: ${CPACK_RPM_PACKAGE_FILE_NAME}.rpm")
			set(CPACK_RPM_PACKAGE_VERSION "${VERSION_STRING}")
			set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
			set(CPACK_RPM_PACKAGE_LICENSE "GPLv2.1+")
			set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_URL}")
		else(RPM_AVAILABLE)
			message(STATUS "rpmbuild not found - no .rpm package will be generated")
		endif(RPM_AVAILABLE)
		unset(RPM_AVAILABLE CACHE)
	else(CMAKE_INSTALL_PREFIX STREQUAL "/usr")
		message(STATUS "Install prefix is not '/usr' - not building .deb / .rpm packages")
	endif(CMAKE_INSTALL_PREFIX STREQUAL "/usr")

endif(WIN32)

include(CPack)

# DO this near the end - to make sure all variables have been computed
# and are final.
set(INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
configure_file(
	"${PROJECT_SOURCE_DIR}/build_config.h.in"
	"${PROJECT_BINARY_DIR}/build_config.h"
)

