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
ECHO Usage^: %~nx0 [options] -p PROJECTDIR
ECHO.
ECHO Options^:
ECHO	-h^|--help		^: Print this message
ECHO	-v^|--verbose		^: Print minetestmapper version info
ECHO	--test			^: Instead of creating the configuration files,
ECHO				  print their would-be contents to the console.
ECHO	--use-sqlite3		^: Make minetestmapper understand SQLite3
ECHO	--use-postgresql	^: Make minetestmapper understand PostgreSQL
ECHO	--use-leveldb		^: Make minetestmapper understand LevelDB
ECHO	--use-redis		^: Make minetestmapper understand Redis
ECHO	-p PROJECTDIR		^: Specify the base directory of the project
ECHO				  ^(this option is mandatory^)
ECHO.
ECHO NOTE^: At least one of the databases must be enabled.
EXIT /B 0

:START

SET $MYNAME=%~nx0
SET $MYPATH=%~p0
SET $BASE_VERSION_FILE=base-version
SET $BUILDCONFIG_TEMPLATE=build_config.h.in
SET $BUILDCONFIG_FILE=build_config.h
SET $VERSIONINFO_FILE=MSVC\versioninfo.h

SET $USE_SQLITE3=0
SET $USE_POSTGRESQL=0
SET $USE_LEVELDB=0
SET $USE_REDIS=0
SET $USE_ICONV=0
SET $PACKAGING_FLAT=1
SET $INSTALL_PREFIX=\\
SET $PROJECTDIR=""


SET $USE_ANY=0
SET $VERBOSE=0
SET $TEST=0

:: Command-line argument parsing
:PARSEARGS
	SET $OPT=%~1
	IF /I "%$OPT%"=="-h" (
		CALL :USAGE
		EXIT /B 0
	) ELSE IF /I "%$OPT%"=="--help" (
		CALL :USAGE
		EXIT /B 0
	) ELSE IF /I "%$OPT%"=="-v" (
		SET $VERBOSE=1
	) ELSE IF /I "%$OPT%"=="--verbose" (
		SET $VERBOSE=1
	) ELSE IF /I "%$OPT%"=="--test" (
		SET $TEST=1
	) ELSE IF /I "%$OPT%"=="--use-sqlite3" (
		SET $USE_SQLITE3=1
		SET $USE_ANY=1
	) ELSE IF /I "%$OPT%"=="--use-postgresql" (
		SET $USE_POSTGRESQL=1
		SET $USE_ANY=1
	) ELSE IF /I "%$OPT%"=="--use-leveldb" (
		SET $USE_LEVELDB=1
		SET $USE_ANY=1
	) ELSE IF /I "%$OPT%"=="--use-redis" (
		SET $USE_REDIS=1
		SET $USE_ANY=1
	) ELSE IF /I "%$OPT%"=="-p" (
		SET $PROJECTDIR=%~f2
		:: SHIFT because we took a second argument here.
		SHIFT
	) ELSE IF /I "%$OPT:~0,1%"=="-" (
		ECHO %$MYNAME%^: Error^: unrecognised option^: %$OPT%
		CALL :USAGE
		EXIT /B 1
	) ELSE (
		GOTO GEN_CONFIG
	)
	SHIFT
	GOTO PARSEARGS

:GEN_CONFIG
IF NOT "%1"=="" (
	ECHO %$MYNAME%^: Error^: too many arguments ^(%*^)
	CALL :USAGE
	EXIT /B 1
)
IF NOT %$USE_ANY%==1 (
	ECHO %$MYNAME%^: Error^: at least one --use-^<database^> option must be given
	CALL :USAGE
	EXIT /B 1
)
IF %$PROJECTDIR%=="" (
	ECHO %$MYNAME%^: Error^: PROJECTDIR argument is required
	CALL :USAGE
	EXIT /B 1
)

CALL :CHECK_FOR_GIT
IF ERRORLEVEL 1 (
	CALL :GET_NONGIT_VERSION
) ELSE (
	CALL :GET_GIT_VERSION
)
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%

CALL :COMPUTE_BINARY_VERSION
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%

IF %$VERBOSE%==1 (
	CALL :REPORT_VERSION
)

set $EXITVAL=0
CALL :WRITE_BUILDCONFIG
IF ERRORLEVEL 1 SET $EXITVAL=%ERRORLEVEL%
CALL :WRITE_VERSIONINFO
IF ERRORLEVEL 1 SET $EXITVAL=%ERRORLEVEL%

:: End of main script code.
:: Remaining code consists of functions only
EXIT /B %$EXITVAL%

:: Check for git. This does two things:
:: - verify that git is installed
:: - verify that we are in a git tree (instead of just an unpacked archive)
:CHECK_FOR_GIT
	CALL git describe > NUL 2>&1
	EXIT /B %ERRORLEVEL%

:: Obtain the git version information
:GET_GIT_VERSION

	FOR /F "usebackq tokens=*" %%V IN (`git describe --long "--match=[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]" "--dirty=-WIP" "--abbrev=8"`) DO (
		SET $VERSION_FULL=%%V
	)
	FOR /F "tokens=1 delims=-" %%M in ("%$VERSION_FULL%") DO (
		SET $VERSION_MAJOR=%%M
	)
	FOR /F "tokens=2 delims=-" %%M in ("%$VERSION_FULL%") DO (
		SET $VERSION_MINOR_COMMITS=%%M
	)
	FOR /F "tokens=3 delims=-" %%M in ("%$VERSION_FULL%") DO (
		SET $VERSION_MINOR_SHA1=%%M
	)
	FOR /F "tokens=4 delims=-" %%M in ("%$VERSION_FULL%") DO (
		SET $VERSION_MINOR_WIP=%%M
	)
	IF DEFINED $VERSION_MINOR_WIP (
		SET $VERSION_MINOR=%$VERSION_MINOR_COMMITS%-%$VERSION_MINOR_SHA1%-%$VERSION_MINOR_WIP%
	) ELSE (
		SET $VERSION_MINOR=%$VERSION_MINOR_COMMITS%-%$VERSION_MINOR_SHA1%
	)

	EXIT /B 0

:: Obtain version information when git cannot be used
:GET_NONGIT_VERSION

	IF NOT EXIST %$PROJECTDIR%\%$BASE_VERSION_FILE% (
		ECHO %$MYNAME%^: Error^: base version file ^(%$PROJECTDIR%\%$BASE_VERSION_FILE%^) not found.
		EXIT /B 2
	)
	FOR /F "tokens=*" %%V IN (%$PROJECTDIR%\%$BASE_VERSION_FILE%) DO (
		SET $VERSION_MAJOR=%%V
	)
	SET $VERSION_MINOR=-X
	EXIT /B 0

:: Compute binary version for MSVC
:COMPUTE_BINARY_VERSION
	:: This assumes the version is of the form:
	::	YYYYMMDD-nn-cccccccc[-WIP]
	:: or:
	::	YYYYMMDD-X
	:: Binary version consists of 4 16-bit numbers.
	:: Conversion:
	:: - 1st 16-bit number: 0
	:: - 2st 16-bit number: 1024*YY+32*MM+DD (usable until 2064)
	:: - 3nd 16-bit number: 2 * nn (# commits since major version)
	::   2 * nn + 1 if the working tree was dirty ('-WIP')
	::   65535 if unknown ('X')
	:: - 4rd 16-bit number: 1st 4 hex digits of cccccccc (commit SHA1)
	SET /A $BV2=1024*%$VERSION_MAJOR:~2,2%+32*%$VERSION_MAJOR:~4,2%+%$VERSION_MAJOR:~6,2%
	IF /I "%$VERSION_MINOR_COMMITS%"=="X" (
		SET /A $BV3=65535
	) ELSE IF DEFINED $VERSION_MINOR_WIP (
		SET /A $BV3=2*%$VERSION_MINOR_COMMITS%+1
	) ELSE (
		SET /A $BV3=2*%$VERSION_MINOR_COMMITS%
	)
	IF /I "%$VERSION_MINOR_COMMITS%"=="X" (
		SET /A $BV4=0
	) ELSE (
		SET /A $BV4=0x%$VERSION_MINOR_SHA1:~1,4%
	)
	SET $VERSION_BINARY=0,%$BV2%,%$BV3%,%$BV4%
	EXIT /B 0

:: Report version info to the console
:REPORT_VERSION
	ECHO Minetestmapper version information^:
	ECHO.
	IF /I "%$VERSION_MINOR_COMMITS%"=="X" (
		ECHO WARNING^: git executable not found, or no git tree found.
		ECHO           No exact version information could be obtained.
		ECHO.
	)
	ECHO Full version^:                  %$VERSION_FULL%
	ECHO Major version^:                 %$VERSION_MAJOR%
	ECHO Minor version^:                 %$VERSION_MINOR%
	
	IF /I NOT "%$VERSION_MINOR_COMMITS%"=="X" (
		ECHO Commits since full version^:    %$VERSION_MINOR_COMMITS%
		ECHO Latest commit SHA1 id ^(short^)^: %$VERSION_MINOR_SHA1%
		IF DEFINED $VERSION_MINOR_WIP (
			ECHO Working tree is clean^:         NO
		) ELSE (
			ECHO Working tree is clean^:         YES
		)
	)
	ECHO Binary version^:                %$VERSION_BINARY%
	EXIT /B 0
	
:: Create the build file
:WRITE_BUILDCONFIG
	SET "$INDENT="
	IF NOT EXIST %$PROJECTDIR%\%$BUILDCONFIG_TEMPLATE% (
		ECHO %$MYNAME%^: Error^: template file ^(%$PROJECTDIR%\%$BUILDCONFIG_TEMPLATE%^) not found.
		EXIT /B 2
	)
	IF %$VERBOSE%==1 (
		ECHO ---- Generating %$PROJECTDIR%\%$BUILDCONFIG_FILE%
	)
	SET $OUTPUT=%$PROJECTDIR%\%$BUILDCONFIG_FILE%
	IF %$TEST%==1 (
		ECHO The build config file ^(%$BUILDCONFIG_FILE%^) contents would be^:
		SET "$INDENT=    "
		SET $OUTPUT=CON
	) ELSE (
		:: If not in testmode, delete the file, because its regenerated
		COPY NUL "%$OUTPUT%" >NUL
	)
	FOR /F "tokens=*" %%L in (%$PROJECTDIR%\%$BUILDCONFIG_TEMPLATE%) DO CALL :WRITE_LINE %%L
	EXIT /B 0

:: This funktion writes 1 line with the correct build parameter into the build_config.h file. 
:: Usage: CALL :WRITE_LINE <string>
:WRITE_LINE
	SETLOCAL ENABLEDELAYEDEXPANSION
	SET $LINE=%*
	
	SET $LINE=!$LINE:@BUILD_CONFIG_GENDATE@=%DATE% %TIME%!
	SET $LINE=!$LINE:@BUILD_CONFIG_GENTOOL@=%$MYNAME%!
	SET $LINE=!$LINE:@USE_SQLITE3@=%$USE_SQLITE3%!
	SET $LINE=!$LINE:@USE_POSTGRESQL@=%$USE_POSTGRESQL%!
	SET $LINE=!$LINE:@USE_LEVELDB@=%$USE_LEVELDB%!
	SET $LINE=!$LINE:@USE_REDIS@=%$USE_REDIS%!
	SET $LINE=!$LINE:@USE_ICONV@=%$USE_ICONV%!
	SET $LINE=!$LINE:@VERSION_MAJOR@=%$VERSION_MAJOR%!
	SET $LINE=!$LINE:@VERSION_MINOR@=%$VERSION_MINOR%!
	SET $LINE=!$LINE:@PACKAGING_FLAT@=%$PACKAGING_FLAT%!
	SET $LINE=!$LINE:@INSTALL_PREFIX@=%$INSTALL_PREFIX%!
	
	ECHO %$INDENT%%$LINE% >> %$OUTPUT%
	ENDLOCAL
	EXIT /B 0
	
:: Create the versioninfo file
:WRITE_VERSIONINFO
	SETLOCAL
	SET "$INDENT="
	IF %$VERBOSE%==1 (
		ECHO ---- Generating %$PROJECTDIR%\%$VERSIONINFO_FILE%%
	)
	SET $OUTPUT=%$PROJECTDIR%\%$VERSIONINFO_FILE%
	IF %$TEST%==1 (
		ECHO The versioninfo file ^(%$VERSIONINFO_FILE%^) contents would be^:
		SET "$INDENT=    "
		SET $OUTPUT=CON
	)
	> %$OUTPUT% (
		ECHO %$INDENT%// This file is auto-generated. Any changes to it will be overwritten.
		ECHO %$INDENT%// Modify the following build-system specific script instead^:
		ECHO %$INDENT%//	MSVC/generate_build_config.bat
		ECHO.
		ECHO %$INDENT%#define MINETESTMAPPER_VERSION_FULL "%$VERSION_FULL%"
		ECHO %$INDENT%#define MINETESTMAPPER_VERSION_MAJOR "%$VERSION_MAJOR%"
		ECHO %$INDENT%#define MINETESTMAPPER_VERSION_MINOR "%$VERSION_MINOR%"
		ECHO %$INDENT%#define MINETESTMAPPER_VERSION_BINARY %$VERSION_BINARY%
		IF DEFINED $VERSION_MINOR_WIP (
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
	ENDLOCAL
	EXIT /B 0
