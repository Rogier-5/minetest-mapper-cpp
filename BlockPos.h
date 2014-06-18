
#ifndef BLOCKPOS_H
#define BLOCKPOS_H

#include <cstdlib>
#include <limits>
#include <climits>
#include <cctype>
#include <string>
#include <sstream>
#include <iomanip>
#include <stdexcept>

struct BlockPos {
	// m_strFormat is used to record the original format that was obtained
	// from the database (and thus, the format to use when querying
	// the database again). DO NOT USE M_STRfORMAT FOR ANYTHING ELSE.
	// Use the argument to databasePosStrFmt to force a specific display format.
	enum StrFormat {
		Unknown = 0,
		I64,		// Minetest & Freeminer
		XYZ,
		AXYZ,		// Freeminer
	};
	int dimension[3];
	// Unfortunately, static member references are not supported (even though static member pointers are...)
	// I'd like to have:
	//	static int BlockPos::&x = dimension[0];
	// (which would avoid the extra object size associated with non-static members)
	int &x = dimension[0];
	int &y = dimension[1];
	int &z = dimension[2];
//#if SIZE_MAX > (1LL << 36)
//	std::size_t hash(void) const { return databasePosI64(); }
//#else
//	std::size_t hash(void) const { return databasePosI64() % SIZE_MAX; }
//#endif
	BlockPos() : dimension{0, 0, 0}, m_strFormat(Unknown) {}
	BlockPos(int _x, int _y, int _z) : dimension{_x, _y, _z}, m_strFormat(Unknown) {}
	BlockPos(const BlockPos &pos) : dimension{pos.x, pos.y, pos.z}, m_strFormat(pos.m_strFormat) {}
	BlockPos(int64_t i) { operator=(i); }
	BlockPos(const std::string &s) { operator=(s); }
	int64_t databasePosI64(void) const { return getDBPos(); }
	std::string databasePosStr(StrFormat defaultFormat = Unknown) const;
	std::string databasePosStrFmt(StrFormat format) const;

	bool operator<(const BlockPos& p) const;
	bool operator==(const BlockPos& p) const;
	void operator=(const BlockPos &p) { x = p.x; y = p.y; z = p.z; m_strFormat = p.m_strFormat; }
	void operator=(int64_t i) { setFromDBPos(i); m_strFormat = I64; }
	void operator=(const std::string &s);

	static const int Any = INT_MIN;
	static const int Invalid = INT_MAX;

protected:
// Include code copied from the minetest codebase.
// Defines (at least) the following functions:
//	setFromDBPos(int64_t i);
//	int64_t getDBPos(void) const;
#include "minetest-database.h"

private:
	// WARNING: see comment about m_strFormat above !!
	StrFormat m_strFormat;

};

struct NodeCoord : BlockPos
{
	bool isBlock[3];
	// Unfortunately, static member references are not supported (even though static member pointers are...)
	bool &xBlock = isBlock[0];
	bool &yBlock = isBlock[1];
	bool &zBlock = isBlock[2];

	NodeCoord() : BlockPos(), isBlock{false, false, false} {}
	NodeCoord(int _x, int _y, int _z) : BlockPos(_x, _y, _z), isBlock{false, false, false} {}
	NodeCoord(const BlockPos &pos, bool node = false) : BlockPos(pos), isBlock{pos.x == Any ? false : !node, pos.y == Any ? false : !node, pos.z == Any ? false : !node} {}
	NodeCoord(const NodeCoord &coord) : BlockPos(coord), isBlock{coord.xBlock, coord.yBlock, coord.zBlock} {}
	NodeCoord(int64_t i) : isBlock{true, true, true} { setFromDBPos(i); }

	size_t hash(void) const;

	bool operator==(const NodeCoord &coord) const;
	void operator=(const BlockPos &coord) { x = coord.x; y = coord.y; z = coord.z; xBlock = true; yBlock = true; zBlock = true; }
	void operator=(const NodeCoord &coord) { x = coord.x; y = coord.y; z = coord.z; xBlock = coord.xBlock; yBlock = coord.yBlock; zBlock = coord.zBlock; }
};

// Use this for map or set storage only...
struct NodeCoordHashed : NodeCoord
{
private:
	size_t m_hash;
public:
	struct Hash {
		size_t operator()(const NodeCoordHashed &h) const { return h.hash(); }
	};
	NodeCoordHashed(const BlockPos &pos) : NodeCoord(pos) { rehash(); }
	NodeCoordHashed(const NodeCoord &coord) : NodeCoord(coord) { rehash(); }
	void rehash(void) { m_hash = NodeCoord::hash(); }
	unsigned hash(void) { rehash(); return m_hash; }
	unsigned hash(void) const { return m_hash; }
	bool operator==(const NodeCoordHashed &coord) const { if (m_hash != coord.m_hash) return false; return NodeCoord::operator==(coord); }
	void operator=(const NodeCoord &coord) { NodeCoord::operator=(coord); rehash(); }
	bool operator<(const NodeCoordHashed &coord) { return m_hash < coord.m_hash; }
};

namespace std {
	template<>
	struct hash<NodeCoordHashed>
	{
		size_t operator()(const NodeCoordHashed &nch) const { return nch.hash(); }
	};
}

inline void BlockPos::operator=(const std::string &s)
{
	std::istringstream is(s);
	if (isdigit(is.peek()) || is.peek() == '-' || is.peek() == '+') {
		int64_t ipos;
		is >> ipos;
		if (is.bad() || !is.eof()) {
			throw std::runtime_error(std::string("Failed to decode i64 (minetest) coordinate string from database (") + s + ")" );
		}
		operator=(ipos);
		m_strFormat = I64;
	}
	else if (is.peek() == 'a') {
		// Freeminer new format (a<x>,<y>,<z>)
		char c1, c2;
		is >> c1;
		is >> x;
		is >> c1;
		is >> y;
		is >> c2;
		is >> z;
		if (is.bad() || !is.eof() || c1 != ',' || c2 != ',') {
			throw std::runtime_error(std::string("Failed to decode axyz (freeminer) coordinate string from database (") + s + ")" );
		}
		m_strFormat = AXYZ;
	}
	else {
		throw std::runtime_error(std::string("Failed to detect format of coordinate string from database (") + s + ")" );
	}
}

inline std::string BlockPos::databasePosStr(StrFormat defaultFormat) const
{
	StrFormat format = m_strFormat;
	if (format == Unknown)
		format = defaultFormat;
	return databasePosStrFmt(format);
}


inline std::string BlockPos::databasePosStrFmt(StrFormat format) const
{
	std::ostringstream os;
	switch(format) {
	case Unknown:
		throw std::runtime_error(std::string("Internal error: Converting BlockPos to unknown string type"));
		break;
	case I64:
		os << databasePosI64();
		break;
	case XYZ:
		os << x << ',' << y << ',' << z;
		break;
	case AXYZ:
		os << 'a' << x << ',' << y << ',' << z;
		break;
	}
	return os.str();
}

// operator< should order the positions in the
// order the corresponding pixels are generated:
// First (most significant): z coordinate, descending (i.e. reversed)
// Then                    : x coordinate, ascending
// Last (least significant): y coordinate, descending (i.e. reversed)
inline bool BlockPos::operator<(const BlockPos& p) const
{
	if (z > p.z)
		return true;
	if (z < p.z)
		return false;
	if (x < p.x)
		return true;
	if (x > p.x)
		return false;
	if (y > p.y)
		return true;
	if (y < p.y)
		return false;
	return false;
}

inline bool BlockPos::operator==(const BlockPos &p) const
{
	if (z != p.z)
		return false;
	if (y != p.y)
		return false;
	if (x != p.x)
		return false;
	return true;
}

inline size_t NodeCoord::hash(void) const
{
	size_t hash = 0xd3adb33f;
	for (int i=0; i<3; i++)
		// Nothing too fancy...
		hash = ((hash << 8) | (hash >> 24)) ^ (dimension[i] ^ (isBlock[i] ? 0x50000000 : 0));
	return hash;
}

inline bool NodeCoord::operator==(const NodeCoord &coord) const
{
	if (z != coord.z)
		return false;
	if (y != coord.y)
		return false;
	if (x != coord.x)
		return false;
	if (zBlock != coord.zBlock)
		return false;
	if (yBlock != coord.yBlock)
		return false;
	if (xBlock != coord.xBlock)
		return false;
	return true;
}

#endif // BLOCKPOS_H
