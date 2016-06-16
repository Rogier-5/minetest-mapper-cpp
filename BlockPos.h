
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
#include <iterator>

class BlockPosIterator;

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
		STRFORMAT_MAX
	};
	int dimension[3];
	int &x(void) { return dimension[0]; }
	int &y(void) { return dimension[1]; }
	int &z(void) { return dimension[2]; }
	const int &x(void) const { return dimension[0]; }
	const int &y(void) const { return dimension[1]; }
	const int &z(void) const { return dimension[2]; }
//#if SIZE_MAX > (1LL << 36)
//	std::size_t hash(void) const { return databasePosI64(); }
//#else
//	std::size_t hash(void) const { return databasePosI64() % SIZE_MAX; }
//#endif
	BlockPos() : dimension{0, 0, 0}, m_strFormat(Unknown), m_id(INT64_MIN) {}
	BlockPos(int _x, int _y, int _z, StrFormat format = Unknown) : dimension{_x, _y, _z}, m_strFormat(format), m_id(INT64_MIN) {}
	BlockPos(int _x, int _y, int _z, int64_t id) : dimension{_x, _y, _z}, m_strFormat(Unknown), m_id(id) {}
	BlockPos(const BlockPos &pos) : dimension{pos.x(), pos.y(), pos.z()}, m_strFormat(pos.m_strFormat), m_id(pos.m_id) {}
	BlockPos(int64_t i) { operator=(i); }
	BlockPos(int64_t i, int64_t id) { operator=(i); m_id = id; }
	BlockPos(const std::string &s) { operator=(s); }
	StrFormat databaseFormat(void) const { return m_strFormat; }
	int64_t databasePosI64(void) const { return getDBPos(); }
	int64_t databasePosId(void) const { return m_id; }
	bool databasePosIdIsValid(void) const { return m_id != INT64_MIN; }
	std::string databasePosStr(StrFormat defaultFormat = Unknown) const;
	std::string databasePosStrFmt(StrFormat format) const;

	bool operator<(const BlockPos& p) const;
	bool operator==(const BlockPos& p) const;
	bool operator!=(const BlockPos& p) const { return !operator==(p); }
	void operator=(const BlockPos &p) { x() = p.x(); y() = p.y(); z() = p.z(); m_strFormat = p.m_strFormat; m_id = p.m_id; }
	void operator=(int64_t i) { setFromDBPos(i); m_strFormat = I64; m_id = INT64_MIN; }
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
	int64_t m_id;
	friend class BlockPosIterator;
};

// This could be a random access iterator, but it is not needed, and it
// is more complex to implement
//class BlockPosIterator : public std::iterator<std::bidirectional_iterator_tag, BlockPos, long long>
class BlockPosIterator
{
public:
	enum State { Start = -1, Middle = 0, End = 1 };
	// NOTE: if m_state is initialized to Middle, the value of the iterator will be undefined
	BlockPosIterator(void) : m_value(0,0,0), m_start(0,0,0), m_end(0,0,0), m_state(Start) {}
	BlockPosIterator(BlockPos start, BlockPos end, State state = Middle) : m_value(start), m_start(start), m_end(end), m_state(state) {}
	BlockPosIterator(const BlockPosIterator &i) : m_value(i.m_value), m_start(i.m_start), m_end(i.m_end), m_state(i.m_state) {}
	BlockPosIterator &operator=(const BlockPosIterator &i);
	bool operator==(const BlockPosIterator &i) const;
	bool operator!=(const BlockPosIterator &i) const { return !operator==(i); }

	BlockPos &operator*(void) { return m_value; }
	BlockPos *operator->(void) { return &m_value; }
	BlockPosIterator &operator++(void) { stepForward(); return *this; }
	BlockPosIterator operator++(int) { BlockPosIterator rv(*this); stepForward(); return rv; }
	BlockPosIterator &operator--(void) { stepReverse(); return *this; }
	BlockPosIterator operator--(int) { BlockPosIterator rv(*this); stepReverse(); return rv; }

	void breakDim(int i, int step = 1);
private:
	BlockPos m_value;
	BlockPos m_start;
	BlockPos m_end;
	State m_state;
	void printState(const char *message, FILE *file);
	void stepForward(void);
	void stepReverse(void);
	bool stepDim(int i, int step);
};

struct NodeCoord : BlockPos
{
	bool isBlock[3];
	bool &xBlock(void) { return isBlock[0]; }
	bool &yBlock(void) { return isBlock[1]; }
	bool &zBlock(void) { return isBlock[2]; }
	const bool &xBlock(void) const { return isBlock[0]; }
	const bool &yBlock(void) const { return isBlock[1]; }
	const bool &zBlock(void) const { return isBlock[2]; }

	NodeCoord() : BlockPos(), isBlock{false, false, false} {}
	NodeCoord(int _x, int _y, int _z) : BlockPos(_x, _y, _z), isBlock{false, false, false} {}
	NodeCoord(const BlockPos &pos, bool node = false) : BlockPos(pos), isBlock{pos.x() == Any ? false : !node, pos.y() == Any ? false : !node, pos.z() == Any ? false : !node} {}
	NodeCoord(const NodeCoord &coord) : BlockPos(coord), isBlock{coord.xBlock(), coord.yBlock(), coord.zBlock()} {}
	NodeCoord(int64_t i) : isBlock{true, true, true} { setFromDBPos(i); }

	size_t hash(void) const;

	bool operator==(const NodeCoord &coord) const;
	void operator=(const BlockPos &coord) { x() = coord.x(); y() = coord.y(); z() = coord.z(); xBlock() = true; yBlock() = true; zBlock() = true; }
	void operator=(const NodeCoord &coord) { x() = coord.x(); y() = coord.y(); z() = coord.z(); xBlock() = coord.xBlock(); yBlock() = coord.yBlock(); zBlock() = coord.zBlock(); }
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
	size_t hash(void) { rehash(); return m_hash; }
	size_t hash(void) const { return m_hash; }
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

inline std::string BlockPos::databasePosStr(StrFormat defaultFormat) const
{
	StrFormat format = m_strFormat;
	if (format == Unknown)
		format = defaultFormat;
	return databasePosStrFmt(format);
}

// operator< should order the positions in the
// order the corresponding pixels are generated:
// First (most significant): z coordinate, descending (i.e. reversed)
// Then                    : x coordinate, ascending
// Last (least significant): y coordinate, descending (i.e. reversed)
inline bool BlockPos::operator<(const BlockPos& p) const
{
	if (z() > p.z())
		return true;
	if (z() < p.z())
		return false;
	if (x() < p.x())
		return true;
	if (x() > p.x())
		return false;
	if (y() > p.y())
		return true;
	if (y() < p.y())
		return false;
	return false;
}

inline bool BlockPos::operator==(const BlockPos &p) const
{
	if (z() != p.z())
		return false;
	if (y() != p.y())
		return false;
	if (x() != p.x())
		return false;
	return true;
}

inline BlockPosIterator &BlockPosIterator::operator=(const BlockPosIterator &i)
{
	m_value = i.m_value;
	m_start = i.m_start;
	m_end = i.m_end;
	m_state = i.m_state;
	return *this;
}

inline bool BlockPosIterator::operator==(const BlockPosIterator &i) const
{
	if (m_state != i.m_state)
		return false;
	else
		return m_value == i.m_value;
}

inline bool BlockPosIterator::stepDim(int i, int step)
{
	bool forward = true;
	if (step < 0)
		forward = false;
	if (m_start.dimension[i] <= m_end.dimension[i] || !forward) {
		if (m_value.dimension[i] < m_end.dimension[i]) {
			m_value.dimension[i]++;
			return true;
		}
		else {
			m_value.dimension[i] = m_start.dimension[i];
			return false;
		}
	}
	else {
		if (m_value.dimension[i] > m_end.dimension[i]) {
			m_value.dimension[i]--;
			return true;
		}
		else {
			m_value.dimension[i] = m_start.dimension[i];
			return false;
		}
	}
}

inline bool NodeCoord::operator==(const NodeCoord &coord) const
{
	if (z() != coord.z())
		return false;
	if (y() != coord.y())
		return false;
	if (x() != coord.x())
		return false;
	if (zBlock() != coord.zBlock())
		return false;
	if (yBlock() != coord.yBlock())
		return false;
	if (xBlock() != coord.xBlock())
		return false;
	return true;
}

#endif // BLOCKPOS_H
