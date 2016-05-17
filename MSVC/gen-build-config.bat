@ECHO OFF
SETLOCAL
GOTO START

::	Script for generating build_config.h and versioninfo.h
::
::	Copyright (C) 2016 Rogier <rogier777@gmail.com>
::
::	This program is free software; you can redistribute it and/or modify
::	it under the terms of the GNU Lesser General Public License as published by
::	the Free Software Foundation; either version 2.1 of the License, or
::	(at your option) any later version.
::
::	This program is distributed in the hope that it will be useful,
::	but WITHOUT ANY WARRANTY; without even the implied warranty of
::	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
::	GNU Lesser General Public License for more details.
::
::	You should have received a copy of the GNU Lesser General Public License along
::	with this program; if not, write to the Free Software Foundation, Inc.,
::	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


:USAGE
ECHO Usage: %~nx0 [options] [PROJECTDIR]
ECHO.
ECHO Options:
ECHO	-h^|--help		: Print this message
ECHO	-v^|--verbose		: Print minetestmapper version info
ECHO	--test			: Print the file contents to the console
ECHO	--use-sqlite3		: Make minetestmapper understand SQLite3
ECHO	--use-postgresql	: Make minetestmapper understand PostgreSQL
ECHO	--use-leveldb		: Make minetestmapper understand LevelDB
ECHO	--use-redis		: Make minetestmapper understand Redis
ECHO.
ECHO	PROJECTDIR		: Base directory of the project
ECHO.
ECHO When both filenames are omitted, the information is printed to
ECHO the console.
ECHO.
ECHO NOTE: At least one of the databases must be enabled.

:START

SET $MYNAME=%~nx0
SET $MYPATH=%~p0
SET $BASE_VERSION_FILE=base-version
SET $BUILDCONFIG_TEMPLATE=build_config.h.in
SET $BUILDCONFIG_FILE=build_config.h
SET $VERSIONINFO_FILE=MSVC\versioninfo.h

SET $USE_SQLITE3 0
SET $USE_POSTGRESQL 0
SET $USE_LEVELDB 0
SET $USE_REDIS 0
SET $USE_ICONV 0
SET $PACKAGING_FLAT 1
SET $INSTALL_PREFIX ""

SET $USE_ANY 0
SET $VERBOSE 0
SET $TEST 0

:: Command-line argument parsing
:PARSEARGS
	IF /I "%~1" == "-h" (
		CALL USAGE
		EXIT /B 0
	) ELSE IF /I "%~1" == "--help" (
		CALL USAGE
		EXIT /B 0
	) ELSE IF /I "%~1" == "-v" (
		SET $VERBOSE 1
	) ELSE IF /I "%~1" == "--verbose" (
		SET $VERBOSE 1
	) ELSE IF /I "%~1" == "--test" (
		SET $TEST 1
	) ELSE IF /I "%~1" == "--use-sqlite3" (
		SET $USE_SQLITE3 1
		SET $USE_ANY 1
	) ELSE IF /I "%~1" == "--use-postgresql" (
		SET $USE_POSTGRESQL 1
		SET $USE_ANY 1
	) ELSE IF /I "%~1" == "--use-leveldb" (
		SET $USE_LEVELDB 1
		SET $USE_ANY 1
	) ELSE IF /I "%~1" == "--use-redis" (
		SET $USE_REDIS 1
		SET $USE_ANY 1
	) ELSE (
		GOTO GEN_CONFIG
	)
	SHIFT
	GOTO PARSEARGS

:GEN_CONFIG

IF %$USE_ANY% NEQ 1 (
	ECHO %$MYNAME%: Error: at least one --use-^<database^> option must be given
	CALL USAGE
	EXIT /B 1
)

IF "%~1" == "" (
	ECHO %$MYNAME%: Error: PROJECTDIR argument is required
	CALL USAGE
	EXIT /B 1
) ELSE (
	SET $PROJECTDIR=%~f1
)

CALL CHECK_FOR_GIT
IF ERRORLEVEL 1 (
	CALL GET_NONGIT_VERSION
) ELSE
	CALL GET_GIT_VERSION
)
CALL COMPUTE_BINARY_VERSION

IF %$VERBOSE% EQU 1 (
	call REPORT_VERSION
)
call CREATE_BUILDCONFIG
call CREATE_VERSIONFILE

:: End of main script code.
:: Remaining code consists of functions only
EXIT /B %ERRORLEVEL%

:: Check for git. This does two things:
:: - verify that git is installed
:: - verify that we are in a git tree (instead of just an unpacked archive)
:CHECK_FOR_GIT
	CALL git describe > NUL 2>&1
	EXIT /B %ERRORLEVEL%

:: Obtain the git version information
:GET_GIT_VERSION
	FOR /F "usebackq tokens=*" %%V IN (`git describe --long "--match=[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]" --dirty=-WIP --abbrev=8`) DO (
		SET $FULL_VERSION=%%V
	)
	FOR /F "tokens=1 delims=-" %%M in (%$FULL_VERSION%) DO (
		SET $MAJOR_VERSION=%%M
	)
	FOR /F "tokens=1,* delims=-" %%M in (%$FULL_VERSION%) DO (
		SET $MINOR_VERSION=%%M
	)
	FOR /F "tokens=1 delims=-" %%M in (%$MINOR_VERSION%) DO (
		SET $MINOR_VERSION_COMMITS=%%M
	)
	FOR /F "tokens=2 delims=-" %%M in (%$MINOR_VERSION%) DO (
		SET $MINOR_VERSION_SHA1=%%M
	)
	FOR /F "tokens=2 delims=-" %%M in (%$MINOR_VERSION%) DO (
		SET $MINOR_VERSION_WIP=%%M
	)
	EXIT /B 0

:: Obtain version information when git cannot be used
:GET_NONGIT_VERSION
	IF NOT EXIST %$PROJECTDIR%\%$BASE_VERSION_FILE% (
		ECHO %$MYNAME%: Error: base version file (%$PROJECTDIR%\%$BASE_VERSION_FILE%) not found.
		exit /B 1
	)
	FOR /F "tokens=*" %%V IN (%$PROJECTDIR%\%$BASE_VERSION_FILE%) DO (
		SET $MAJOR_VERSION=%%V
	)
	SET $MINOR_VERSION=-X
	EXIT /B 0

:: Compute digital version for MSVC
:COMPUTE_BINARY_VERSION
	:: This assumes the version is of the form:
	::	YYYYMMDD-nn-cccccccc[-WIP]
	:: or:
	::	YYYYMMDD-X
	:: Digital version consists of 4 16-bit numbers.
	:: Conversion:
	:: - 1st 16-bit number: 0
	:: - 2st 16-bit number: YYMMDD	(usable until 2065)
	:: - 3nd 16-bit number: 2 * nn (# commits since major version)
	::   2 * nn + 1 if the working tree was dirty ('-WIP')
	::   65535 if unknown ('X')
	:: - 4rd 16-bit number: 1st 4 hex digits of cccccccc (commit SHA1)
	IF /I "%$MINOR_VERSION_COMMITS%" == "X" (
		SET /A $MIV3=65535
	) ELSE IF EXIST $MINOR_VERSION_WIP (
		SET /A $MIV3=2*%$MINOR_VERSION_COMMITS%+1
	) ELSE (
		SET /A $MIV3=2*%$MINOR_VERSION_COMMITS%
	)
	IF /I "%$MINOR_VERSION_COMMITS%" == "X" (
		SET /A $MIV4=0
	) ELSE (
		SET /A $MIV4=0x%$MINOR_VERSION_SHA1:~0,4%
	)
	SET $BINARY_VERSION=0,%$MAJOR_VERSION:~2,6%,%$MIV3%,%$MIV4%
	EXIT /B 0

:: Report version info to the console
:REPORT_VERSION
	ECHO Minetestmapper version information:
	ECHO.
	IF /I "%$MINOR_VERSION_COMMITS%" == "X" (
		ECHO WARNING: git executable not found, or no git tree found.
		ECHO          No exact version information could be obtained.
		ECHO.
	)
	ECHO Full version:                   %$FULL_VERSION%
	ECHO Major version:                  %$MAJOR_VERSION%
	ECHO Minor version:                  %$MINOR_VERSION%
	IF /I NOT "%$MINOR_VERSION_COMMITS%" == "X" (
		ECHO Commits since full version:     %$MINOR_VERSION_COMMITS%
		ECHO Latest commit SHA1 id (short):  %$MINOR_VERSION_SHA1%
		IF EXIST $MINOR_VERSION_WIP (
			ECHO Working tree is clean:          NO
		) ELSE (
			ECHO Working tree is clean:          YES
		)
	)
	ECHO Digital version:                %$BINARY_VERSION%
	exit /B 0
	
:: Create the build file
:WRITE_BUILDCONFIG
	SET $INDENT=
	IF NOT EXIST %$PROJECTDIR%\%$BUILDCONFIG_TEMPLATE% (
		ECHO %$MYNAME%: Error: template file (%$PROJECTDIR%\%$BUILDCONFIG_TEMPLATE%) not found.
		exit /B 1
	)
	IF %$TEST% EQU 1 (
		ECHO The build file (build_config.h) contents would be:
		CALL "SET $INDENT=    "
	) ELSE IF %$VERBOSE% EQU 1 (
		ECHO ---- Generating %$PROJECTDIR%\%$BUILDCONFIG%
	)
	> %$PROJECTDIR%\%$BUILDCONFIG% (
		FOR /F "tokens=*" %%L in (%$BUILDCONFIG_TEMPLATE%) DO (
			SET $LINE=%%L
			:: Fortunately, C++ does not care about extra trailing spaces
			CALL SET $LINE=%%$LINE:@USE_SQLITE3@=%$USE_SQLITE3% %%
			CALL SET $LINE=%%$LINE:@USE_POSTGRESQL@=%$USE_POSTGRESQL% %%
			CALL SET $LINE=%%$LINE:@USE_LEVELDB@=%$USE_LEVELDB% %%
			CALL SET $LINE=%%$LINE:@USE_REDIS@=%$USE_REDIS% %%
			CALL SET $LINE=%%$LINE:@USE_ICONV@=%$USE_ICONV% %%
			CALL SET $LINE=%%$LINE:@VERSION_MAJOR@=%$VERSION_MAJOR% %%
			CALL SET $LINE=%%$LINE:@VERSION_MINOR@=%$VERSION_MINOR% %%
			CALL SET $LINE=%%$LINE:@PACKAGING_FLAT@=%$PACKAGING_FLAT% %%
			CALL SET $LINE=%%$LINE:@INSTALL_PREFIX@=%$INSTALL_PREFIX% %%
			ECHO %$LINE%
		)
	)
	EXIT /B 0
	:: Alternate code to generate the file from scratch
	> %$BUILDCONFIG% (
		ECHO %$INDENT%// The file 'build_config.h' is auto-generated. Any changes to it
		ECHO %$INDENT%// will be overwritten.
		ECHO %$INDENT%// Modify the following build-system specific file and script instead:
		ECHO %$INDENT%//	build_config.h.in               (used by CMake)
		ECHO %$INDENT%//	MSVC/generate_build_config.bat  (used by MSVC)
		ECHO.
		ECHO %$INDENT%#ifndef BUILD_CONFIG_H
		ECHO %$INDENT%#define BUILD_CONFIG_H
		ECHO.
		ECHO %$INDENT%#define USE_SQLITE3 %$USE_SQLITE3%
		ECHO %$INDENT%#define USE_POSTGRESQL %$USE_POSTGRESQL%
		ECHO %$INDENT%#define USE_LEVELDB %$USE_LEVELDB%
		ECHO %$INDENT%#define USE_REDIS %$USE_REDIS%
		ECHO.
		ECHO %$INDENT%#define USE_ICONV %$USE_ICONV%
		ECHO.
		ECHO %$INDENT%#define VERSION_MAJOR "%$VERSION_MAJOR%"
		ECHO %$INDENT%#define VERSION_MINOR "%$VERSION_MINOR%"
		ECHO.
		ECHO %$INDENT%#define PACKAGING_FLAT %$PACKAGING_FLAT%
		ECHO %$INDENT%#define INSTALL_PREFIX "%$INSTALL_PREFIX%"
		ECHO.
		ECHO %$INDENT%#endif
	)
	EXIT /B 0

:: Create the versioninfo file
:WRITE_VERSIONINFO
	SET $INDENT=
	IF %$TEST% EQU 1 (
		ECHO The versioninfo file (versioninfo.h) contents would be:
		CALL "SET $INDENT=    "
	) ELSE IF %$VERBOSE% EQU 1 (
		ECHO ---- Generating %$PROJECTDIR%\%$VERSIONINFO%
	)
	> %$PROJECTDIR%\%$VERSIONINFO% (
		ECHO %$INDENT%// This file is auto-generated. Any changes to it will be overwritten.
		ECHO %$INDENT%// Modify the following build-system specific script instead:
		ECHO %$INDENT%//	MSVC/generate_build_config.bat
		ECHO.
		ECHO %$INDENT%#define MINETESTMAPPER_VERSION_FULL "%$VERSION_FULL%"
		ECHO %$INDENT%#define MINETESTMAPPER_VERSION_MAJOR "%$VERSION_MAJOR%"
		ECHO %$INDENT%#define MINETESTMAPPER_VERSION_MINOR "%$VERSION_MINOR%"
		ECHO %$INDENT%#define MINETESTMAPPER_VERSION_BINARY %$VERSION_BINARY%
		IF EXIST $MINOR_VERSION_WIP% (
			ECHO %$INDENT%#define MINETESTMAPPER_WIP_FLAG ^| VS_FF_PATCHED
		) ELSE (
			:: Clear 'VS_FF_PATCHED' only if the WIP part is empty
			ECHO %$INDENT%#define MINETESTMAPPER_WIP_FLAG
		)
		ECHO.
		ECHO %$INDENT%#ifdef _DEBUG
		ECHO %$INDENT%#define MINETESTMAPPER_DEBUG_FLAG ^| VS_FF_DEBUG
		ECHO %$INDENT%#else
		ECHO %$INDENT%#define MINETESTMAPPER_DEBUG_FLAG
		ECHO %$INDENT%#endif
	)
	EXIT /B 0
