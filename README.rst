Minetest Mapper C++
===================

A port of minetestmapper.py to C++ from https://github.com/minetest/minetest/tree/master/util

Requirements
------------

* libgd
* sqlite3
* xxd binary (You can get xxd from your distribution packages or use '' wget -O xxd.c http://www.opensource.apple.com/source/vim/vim-43/vim/src/xxd/xxd.c?txt && gcc xxd.c -o xxd '')

Compilation
-----------

Plain:

::

    cmake .
    make

With levelDB support:

::

    cmake -DENABLE_LEVELDB=true .
    make

Debug version:

::

    cmake -DCMAKE_BUILD_TYPE:STRING=Debug .
    make

Release version:

::

    cmake -DCMAKE_BUILD_TYPE:STRING=Release .
    make


Usage
-----

Binary `minetestmapper` has two mandatory paremeters, `-i` (input world path)
and `-o` (output image path).

::

    ./minetestmapper -i ~/.minetest/worlds/my_world/ -o ~/map.png


Parameters
^^^^^^^^^^

bgcolor:
    Background color of image, `--bgcolor #ffffff`

scalecolor:
    Color of scale, `--scalecolor #000000`

playercolor:
    Color of player indicators, `--playercolor #ff0000`

origincolor:
    Color of origin indicator, `--origincolor #ff0000`

drawscale:
    Draw tick marks, `--drawscale`

drawplayers:
    Draw player indicators, `--drawplayers`

draworigin:
    Draw origin indicator, `--draworigin`

noshading:
    Don't draw shading on nodes, `--noshading`

min-y:
    Don't draw nodes below this y value, `--min-y -25`

max-y:
    Don't draw nodes above this y value, `--max-y 75`

backend:
    Use specific map backend, supported: sqlite3, leveldb, `--backend leveldb`

geometry:
    Limit area to specific geometry, `--geometry -800:-800+1600+1600`

forcegeometry:
    Generate a map of the requested size, even if the world is smaller.

sqlite-cacheworldrow:
    When using sqlite, read an entire world row at one, instead of reading
    one block at a time.
    This may improve performance when a large percentage of the world is mapped.

verbose:
    report some useful/ interesting information:
    - maximum coordinates of the world
    - world coordinates included the map being generated
    - number of blocks: in the world, and in the map area.
    - database access statistics.

Customization of colors.txt
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Default `colors.txt` is included in binary. Color definitions can be redefined
using external `colors.txt` file.
