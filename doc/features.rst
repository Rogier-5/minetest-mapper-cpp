Minetest Mapper Features
########################

Minetestmapper generates maps of minetest and freeminer worlds.

Major Features
==============
* Support for both minetest and freeminer
* Support for sqlite3, postsgresql, leveldb and redis map databases
* Generate a subsection of the map, or a full map
  (but the size of generated images is limited - see
  'Known Problems' below)
* Generate regular maps or height-maps
* All colors for regular or height maps are configurable
* Draw player positions
* Draw different geometric figures, or text on the map
* Draw the map at a reduced scale. E.g. 1:4 (max: 1:16).
* Draw a scale on the left and/or top side of the map,
  and/or a height scale (for height maps) on the bottom.
* Optionally draw some nodes transparently (e.g. water)
* Includes a user manual

Build Features
==============
* Supports both the gcc and clang compiler suites
* Supports MSVC building on Windows
* Build rpm, deb and/or tar.gz installation
  packages. Or simply type 'make install'.

Minor Features
==============
* Specify a number colors symbolically ('red', ...)
* Draw a grid on the map
* Show a progress indicator
* Draw shades to accentuate height differences (on by default)
* Report actual world dimensions in all directions, as
  well which part of it will be in the map.
* optionally, avoid reading the block list from the database
  (may be more efficient when mapping small parts of the *existing* world)

Differences From Stock Minetestmapper
=====================================
* Support for the new freeminer database format
* Support for the unofficial postgresql backend.
* Ability to draw height-maps
* Different methods for drawing transparent blocks
  (more than transparency on and off)
* Different colors can be specified for nodes, in the
  same colors file, depending on whether transparency
  is enabled or not.
* Abiliy to draw different geometric figures, or text on the map
* Map dimensions can be specified in different ways:

  - using two corners
  - using a corner and the size
  - using the center and the size

* Pixel or block granularity for the dimensions
  (stock minetestmapper always uses block granularity: it rounds
  all dimensions to the next multiple of 16).
* Colors files are automatically searched for in the world
  directory, or in system directories
* Colors files can include others, so that just a few colors can
  be redefined, and the system colors file used for the others.
* The map can be drawn at a reduced scale (1:1 - 1:16).
  This means that a full world map can now be generated.
* A grid can be drawn on the map.
* A number of symbolic colors ('red', ...) are available on the
  command-line.
* The scale can be enabled on the left and top side individually
* Major and minor (tick) intervals are configurable for the scale
* Block numbers are shown on the scale as well
* Optionally, avoid reading the block list from the database
  (dramatically speeds up generating maps of small parts of large worlds)

In addition a number bugs have been fixed. As bugs are also getting
fixed in the stock version of minetestmapper, no accurate list
can be given.

Known Problems
==============

* It is currently not possible to generate huge maps.

  On 32-bit systems, the map size is limited by the maximum amount of memory
  (or really: the size of the address space).
  this means in practise that maps larger than about 24100x24100 (determined
  experimentally - YMMV) can't be generated. Note, that even if a larger
  /could/ be generated, most 32-bit applications would still not be able to
  display it for the same reason.

  On 64-bit systems, the libgd image library unfortunately limits the map
  size to approximately 2147483648 pixels, e.g. approximately 46300x46300.

  If a full map is required for a world that is too large, there are currently
  two options:

    - Generate the map in sections, and use another application to paste them
      together.
    - Generate a 1:2 or 1:4 scaled version of the map, if the reduced level of
      detail is acceptable.

  A third alternative, is of course to support the libgd project in removing
  the current restrictions on image size.

* On scaled maps, the colors of some pixels may be imperceptibly different on
  different systems.

  The difference would be at most 1/256 per color.
  (e.g., a pixel with color ``#4c92a1`` on one system, might have color
  ``#4x91a1`` on another system)

  The cause of this difference has not been determined yet.

* The drawing library supports ISO8859-2 fonts only. Characters in text and
  player names that cannot be converted to the ISO8859-2 character set will
  not be rendered correctly.
