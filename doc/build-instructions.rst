Minetest Mapper Build Instructions
##################################

Minetestmapper can be built on posix (Linux, \*BSD, ...), OSX and Windows
platforms.

Not all platforms receive the same amount of testing:

* Gcc and clang builds on Linux are frequently tested.
* The BSD family should also give little trouble. It is not tested that I know of.
* MSVC on Windows is tested.
* Windows building using MinGW is tested intermittently and should work.
* Building on OSX has not been tested recently and may not work.

Please let me know how you fared on your platform (when you encounter problems,
but also when you're successful - so I can update this text with the latest
information).

.. Contents:: :depth: 2

Requirements
============

Libraries
---------

* zlib
* libgd
* sqlite3 (optional - enabled by default, set ENABLE_SQLITE3=0 in CMake to disable)
* postgresql (optional, set ENABLE_POSTGRESQL=1 in CMake to enable postgresql support)
* leveldb (optional, set ENABLE_LEVELDB=1 in CMake to enable leveldb support)
* hiredis (optional, set ENABLE_REDIS=1 in CMake to enable redis support)

At least one of ``sqlite3``, ``postgresql``, ``leveldb`` and ``hiredis`` is required.
Check the minetest worlds that will be mapped to know which ones should be included.

Not all database backend libraries may be obtainable for all platforms (in particular
for Windows).

The current build scripts for MSVC on windows support the SQLite3 and LevelDB backends out
of the box. Compiling with Redis or PostgreSQL support will require some extra work.

Build Environment
-----------------

* C++ compiler suite (clang or gcc (including mingw); msvc on Windows)
* cmake (not for msvc)
* make (not for msvc)

Documentation
-------------

If converting the documentation to HTML, or another format is desired:

* python-docutils

Packaging
---------

If creation of installation packages is desired:

* fakeroot (for a deb package)
* rpm-build (for an rpm package)

Install Dependencies
====================

Debian and Derivatives
----------------------

Install a compiler if not yet installed. Either gcc/g++:

::

	apt-get install make cmake cmake-curses-gui g++

Or clang:

::

	apt-get install make cmake cmake-curses-gui clang

To convert the manual to HTML (if desired), install ``python-docutils``

::

	apt-get install python-docutils

In order to make a ``.deb`` package (if desired), install the required tools:

::

	apt-get install fakeroot

Finally install the minetestmapper dependencies. At least one of ``libsqlite3-dev``,
``libpq-dev``, ``libleveldb-dev`` and ``libhiredis-dev`` is required.

::

	apt-get install zlib1g-dev libgd-dev libsqlite3-dev libpq-dev libleveldb-dev libhiredis-dev

Fedora and Derivatives
----------------------

Install a compiler if not yet installed. Either gcc/g++:

::

	yum install make cmake gcc-c++

Or clang:

::

	yum install make cmake clang

To convert the manual to HTML (if desired), install ``python-docutils``

::

	yum install python-docutils

In order to make an ``.rpm`` package (if desired), install the required tools:

::

	yum install rpm-build

Finally install the minetestmapper dependencies. At least one of ``libsqlite3x-devel``,
``postgresql-devel``, ``leveldb-devel`` and ``hiredis-devel`` is required.

::

	yum install zlib-devel gd-devel libsqlite3x-devel postgresql-devel leveldb-devel hiredis-devel

Ubuntu
------
See `Debian and Derivatives`_

Linux Mint
----------
See `Debian and Derivatives`_

Windows (MinGW)
---------------

You're probably in for a lot of work, downloading software, and
probably compiling at least some of the direct and indirect dependencies.
At the moment, regrettably, detailed instructions are not available.

Windows (MSVC)
--------------

The following must be installed to successfully compile minetestmapper using MSVC:

* Visual Studio 2015 or 2013 (lower may not work). VS Community can be obtained here:
  https://www.visualstudio.com/
* A precompiled version of the gd library. A suitable version can be downloaded from
  https://github.com/Rogier-5/minetest-mapper-cpp/wiki/Downloads#the-gd-library-for-compiling-minetestmapper-with-msvc

  Alternatively, the gd sources can be downloaded from https://github.com/libgd/libgd.
  They must be compiled using the same version of zlib that will be used when compiling
  minetestmapper.

  Version 2.2.1 of gd is verified to work, but any version 2.2.x should also work.
  And presumably any version 2.x later than 2.2 should work as well.

All other required dependencies will be downloaded automatically by MSVC.


Other
-----

At this moment, no specific instructions are available for other platforms.
Feel free to contribute...

Compilation
===========

Linux / BSD / ...
-----------------

Plain:

::

    cmake .
    make

With levelDB and Redis support:

::

    cmake -DENABLE_LEVELDB=true -DENABLE_REDIS=true .
    make

Create native installation package(s):

::

    cmake -DCMAKE_INSTALL_PREFIX=/usr -DCREATE_FLAT_PACKAGE=False
    cpack

See `CMake Variables`_ for more CMake options.

Windows (MinGW)
---------------

Unfortunately, at the moment no instructions are available for Windows building using MinGW.

Windows (MSVC)
--------------

Setting up the IDE
..................

1. Open ``minetestmapper.sln`` or ``MSVC\mintestmapper.vcxproj`` with Visual Studio.
2. Configure the gd libary:
    1. Open projectsettings `ALT+F7`.
    2. Select `All Configurations` and `All Platforms`.
    3. Click `C/C++` -> `additional include directories` and enter the path to the include directory of libGD.
    4. Click `Apply`
    5. Select a configuration (``Debug|Release``) and a platform (``x86|x64``)
    6. Click `Linker` --> `additional libary directories` Enter the path to libgd that fits to your configuration and platform.

       Do this step for all configurations and platforms you want to use.

       WARNING: You will get a linker error if you select a version of libgd that does not fit to your configuration and platform.

Building Minetestmapper
.......................

With everything set up, Minetestmapper can be built.

Debugging Minetestmapper
........................

1. In projectsettings (`ALT+F7`) click `Debugging`.
2. Specify the Arguments in `Command arguments`.
3. Every time you launch the debugger, minetstmapper will be executed with those arguments.

OSX
---

Probably quite similar to Linux, BSD, etc. Unfortunately no detailed instructions
are available.

CMake Variables
---------------

ENABLE_SQLITE3:
    Whether to enable sqlite3 backend support (on by default)

ENABLE_POSTGRESQL:
    Whether to enable postresql backend support (off by default)

ENABLE_LEVELDB:
    Whether to enable leveldb backend support (off by default)

ENABLE_REDIS:
    Whether to enable redis backend support (off by default)

ENABLE_ALL_DATABASES:
    Whether to enable support for all backends (off by default)

CMAKE_BUILD_TYPE:
    Type of build: 'Release' or 'Debug'. Defaults to 'Release'.

CREATE_FLAT_PACKAGE:
    Whether to create a .tar.gz package suitable for installation in a user's private
    directory.
    The archive will unpack into a single directory, with the mapper's files inside
    (this is the default).

    If off, ``.tar.gz``, ``.deb`` and/or ``.rpm`` packages suitable for system-wide installation
    will be created if possible. The ``tar.gz`` package will unpack into a directory hierarchy.

    For creation of ``.deb`` and ``.rpm packages``, CMAKE_INSTALL_PREFIX must be '/usr'.

    For ``.deb`` package creation, dpkg and fakeroot are required.

    For ``.rpm`` package creation, rpmbuild is required.

CMAKE_INSTALL_PREFIX:
    The install location. Should probably be ``/usr`` or ``/usr/local`` on Linux and BSD variants.

ARCHIVE_PACKAGE_NAME:
    Name of the ``.zip`` or ``.tar.gz`` package (without extension). This will also be
    the name of the directory into which the archive unpacks.

    Defaults to ``minetestmapper-<version>-<os-type>``

    The names of ``.deb`` and ``.rpm`` packages are *not* affected by this variable.

REQUIRE_HTML_DOCUMENTATION:
    Whether HTML documentation must be generated. If enabled, and python-docutils is not
    installed, building will fail.

    By default, HTML documentation will be generated if python-docutils is found, else
    it will not be generated.

    See also the note below.

DISABLE_HTML_DOCUMENTATION:
    Whether to skip generation of HTML documentation, even if python-docutils could be
    found.

    Note that if HTML documentation is not generated at build time, it will also not
    be included in the packages, even if python-docutils is in fact installed and
    even if the converted documentation is available (e.g. because it was generated
    manually).

    See also the note below.

HTML Documentation note:
    If both REQUIRE_HTML_DOCUMENTATION and DISABLE_HTML_DOCUMENTATION are disabled,
    then the question of whether HTML documentation will be generated depends on
    whether python-docutils is installed. If installed, then henceforth HTML
    documentation will be generated. If not installed, then it will not be generated.

    As long as REQUIRE_HTML_DOCUMENTATION and DISABLE_HTML_DOCUMENTATION are both
    disabled then, for consistency, once python-docutils has been found to be installed
    and the decision has been made to generate HTML documentation, this decision persists.
    If subsequently python-docutils is deinstalled, or can no longer be found, later
    builds will fail, until the situation has been fixed. This can be done in several
    ways:

    - (Obviously:) Reinstalling python-docutils, or making sure it can be found.

    - Enabling both REQUIRE_HTML_DOCUMENTATION and DISABLE_HTML_DOCUMENTATION. As this
      is not a sensible combination, the build system will disable both, and it will
      then also reevaluate the persistent decision to generate HTML documentation.

    - Setting DISABLE_HTML_DOCUMENTATION to True to permanently disable generation of
      HTML documentation.

    - Setting DISABLE_HTML_DOCUMENTATION to True, running cmake, and then setting it
      back to false. This will disable HTML generation until python-docutils is
      available again.

Converting the Documentation
============================

Using python-docutils, the manual can be converted to a variety of formats.

HTML
----

By default, documentation is converted to HTML when building minetestmapper,  provided
python-docutils is installed.

If automatic documentation conversion at build time is disabled, but python-docutils
is installed, non-automatic conversion is still possible. Either using make:

::

	make hmtldoc

Or by manually invoking ``rst2html``

::

	cd doc
	rst2html manual.rst > manual.html

Unix manpage
------------

Conversion to unix man format has acceptable, but not perfect results:

::

	cd doc
	rst2man manual.rst > minetestmapper.1

PDF
---

The results of using ``rst2pdf`` (which, as an aside, is not part of python-docutils,
and needs to be obtained separately) to convert to PDF directly are not good: random
images are scaled down, some even to almost invisibly small. If PDF is desired, a
good option is to open the HTML file in a webbrowser, and print it to PDF.  

Other
-----

Other conversions are possible using python-docutils. If you tried any, and
they warrant specific instructions, feel free to contribute.
