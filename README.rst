Minetest Mapper
###############

.. WARNING::
This is a temporary branch. Commits may be randomly split, squashed or mangled
in every conceivable way, and history may be rewritten wantonly. Stuff from this
this branch might even end up being committed to the master branch :-)

A tool to generate maps of minetest and freeminer worlds.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Quick links (functional on github. Elsewhere possibly not):

:All features:		`<doc/features.rst>`_
:User manual:		`<doc/manual.rst>`_
:Changelog:		`<Changelog>`_
:Building:		`<doc/build-instructions.rst>`_
:Github:		`<https://github.com/Rogier-5/minetest-mapper-cpp>`_
:Bugs:			`<https://github.com/Rogier-5/minetest-mapper-cpp/issues>`_
:Windows packages:	`<https://github.com/Rogier-5/minetest-mapper-cpp/wiki/Downloads>`_
:Generating colors.txt: `<dumpnodes/README.dumpnodes>`_

Features (short list)
=====================

Map Generation Features
-----------------------

* Support for both minetest and freeminer worlds
* Support for sqlite3, postgresql, leveldb and redis map databases
* Generate a subsection of the map, or a full map
  (but note that the size of generated images is limited)
* Generate regular maps or height-maps
* All colors for regular or height maps are configurable
* Draw player positions
* Draw different geometric figures, or text on the map
* Draw the map at a reduced scale. E.g. 1:4.
* Draw a scale on the left and/or top side of the map,
  and/or a height scale (for height maps) on the bottom.
* Optionally draw some nodes transparently (e.g. water)
* Includes User Manual
* Includes scripts to aid in generating a color definition file.

Build Features
--------------
* Supports both the gcc and clang compiler suites
* Build rpm, deb and/or tar.gz installation
  packages. Or simply type 'make install'.

Quick Start
===========

Windows
-------

Check the download page to obtain a compiled version:
`<https://github.com/Rogier-5/minetest-mapper-cpp/wiki/Downloads>`_

Install Dependencies
--------------------

Make sure development versions of the following packages are installed.
For more detailed instructions, see `<doc/build-instructions.rst>`_.

**Libraries:**

* zlib
* libgd
* sqlite3 (not mandatory. See `<doc/build-instructions.rst>`_.
* postgresql (if postgresql support is desired)
* leveldb (if leveldb support is desired)
* hiredis (if redis support is desired)

**Build environment:**

* C++ compiler suite (clang or gcc)
* cmake
* make

**Other:**

* python-docutils (if documentation in html format is desired)

Compile
-------

Plain:

::

    cmake .
    make

With levelDB and Redis support:

::

    cmake -DENABLE_LEVELDB=true -DENABLE_REDIS=true .
    make

Generate a Map
----------------

After compilation, and optional installation, minetestmapper is started as follows:

('\\' is a continuation character - the command-line continues
on the next line. Type return only after the line that does
not end with the continuation character)


::

    minetestmapper \
	--input <world-directory> \
	--output <image-file-name.png>

If the world is not too large, and if minetestmapper is installed in
a  system directory, it will most likely work as expected.

Possibly, minetestmapper will not be able to find a colors.txt file. If that happens,
the colors.txt file can be specified on the command-line:

::

    minetestmapper \
	--input <world-directory> \
	--output <image-file-name.png> \
	--colors <filename>

Or the colors.txt file must be installed in a location where minetestmapper will find it.
A colors.txt file (named ``colors.txt``, in lowercase) in the world's directory will certainly
be found. Depending on the system and the configuration, other locations are available. Use the
following command to find out which:

::

    minetestmapper \
	--input <world-directory> \
	--output <image-file-name.png> \
	--verbose-search-colors=2

Manual
======

For more elaborate compilation and installation documentation, please consult
the build manual: `<doc/build-instructions.rst>`_.

For more elaborate usage instruction, please consult the user manual: `<doc/manual.rst>`_.
