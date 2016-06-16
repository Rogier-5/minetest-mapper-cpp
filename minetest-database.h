/*
Minetest
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

// This file was copied from the minetest code & minimally modified to suit
// this codebase
// Original filename: src/database.cpp
//
// For minetestmapper, this file should be included by BlockPos.h *only*


/****************
 * Black magic! *
 ****************
 * The position hashing is very messed up.
 * It's a lot more complicated than it looks.
 */

static inline int unsigned_to_signed(unsigned i, unsigned max_positive)
{
	if (i < max_positive) {
		return static_cast<int>(i);
	} else {
		return static_cast<int>(i - (max_positive * 2));
	}
}


// Modulo of a negative number does not work consistently in C
static inline unsigned pythonmodulo(int64_t i, unsigned mod)
{
	if (i >= 0) {
		return i % mod;
	}
	return mod - ((-i) % mod);
}

inline int64_t getDBPos(void) const
{
	return (uint64_t) z() * 0x1000000 +
		(uint64_t) y() * 0x1000 +
		(uint64_t) x();
}


inline void setFromDBPos(int64_t i)
{
	x() = unsigned_to_signed(pythonmodulo(i, 4096), 2048);
	i = (i - x()) / 4096;
	y() = unsigned_to_signed(pythonmodulo(i, 4096), 2048);
	i = (i - y()) / 4096;
	z() = unsigned_to_signed(pythonmodulo(i, 4096), 2048);
}

