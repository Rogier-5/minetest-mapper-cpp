/*
 * =====================================================================
 *        Version:  1.0
 *        Created:  23.08.2012 12:35:59
 *         Author:  Miroslav Bendík
 *        Company:  LinuxOS.sk
 * =====================================================================
 */

#ifndef TILEGENERATOR_H_JJNUCARH
#define TILEGENERATOR_H_JJNUCARH

#include <gd.h>
#include <iosfwd>
#include <list>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include "PixelAttributes.h"
#include "db.h"

struct Color {
	Color(): r(255), g(255), b(255) {};
	Color(uint8_t r, uint8_t g, uint8_t b): r(r), g(g), b(b) {};
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

struct BlockPos {
	int x;
	int y;
	int z;
	// operator< should order the positions in the
	// order the corresponding pixels are generated:
	// First (most significant): z coordinate, descending (i.e. reversed)
	// Then                    : x coordinate, ascending
	// Last (least significant): y coordinate, descending (i.e. reversed)
	bool operator<(const BlockPos& p) const
	{
		if (z > p.z) {
			return true;
		}
		if (z < p.z) {
			return false;
		}
		if (x < p.x) {
			return true;
		}
		if (x > p.x) {
			return false;
		}
		if (y > p.y) {
			return true;
		}
		if (y < p.y) {
			return false;
		}
		return false;
	}
	bool operator==(const BlockPos& p) const
	{
		if (z != p.z) {
			return false;
		}
		if (y != p.y) {
			return false;
		}
		if (x != p.x) {
			return false;
		}
		return true;
	}
};


class TileGenerator
{
private:
	typedef std::basic_string<unsigned char> unsigned_string;
	typedef std::map<std::string, Color> ColorMap;
	typedef std::pair<BlockPos, unsigned_string> Block;
	typedef std::list<Block> BlockList;

public:
	TileGenerator();
	~TileGenerator();
	void setBgColor(const std::string &bgColor);
	void setScaleColor(const std::string &scaleColor);
	void setOriginColor(const std::string &originColor);
	void setPlayerColor(const std::string &playerColor); Color parseColor(const std::string &color);
	void setDrawOrigin(bool drawOrigin);
	void setDrawPlayers(bool drawPlayers);
	void setDrawScale(bool drawScale);
	void setShading(bool shading);
	void setGeometry(int x, int y, int w, int h);
	void setMinY(int y);
	void setMaxY(int y);
	void setForceGeom(bool forceGeom);
	void setSqliteCacheWorldRow(bool cacheWorldRow);
	void parseColorsFile(const std::string &fileName);
	void setBackend(std::string backend);
	void generate(const std::string &input, const std::string &output);

private:
	void parseColorsStream(std::istream &in);
	void openDb(const std::string &input);
	void loadBlocks();
	BlockPos decodeBlockPos(int64_t blockId) const;
	void createImage();
	void renderMap();
	std::list<int> getZValueList() const;
	Block getBlockOnPos(BlockPos pos);
	void renderMapBlock(const unsigned_string &mapBlock, const BlockPos &pos, int version);
	void renderShading(int zPos);
	void renderScale();
	void renderOrigin();
	void renderPlayers(const std::string &inputPath);
	void writeImage(const std::string &output);
	void printUnknown();
	int getImageX(int val) const;
	int getImageY(int val) const;

public:
	bool verboseCoordinates;
	bool verboseStatistics;

private:
	Color m_bgColor;
	Color m_scaleColor;
	Color m_originColor;
	Color m_playerColor;
	bool m_drawOrigin;
	bool m_drawPlayers;
	bool m_drawScale;
	bool m_shading;
	int m_border;
	std::string m_backend;
	bool m_forceGeom;
	bool m_sqliteCacheWorldRow;

	DB *m_db;
	gdImagePtr m_image;
	PixelAttributes m_blockPixelAttributes;
	int m_xMin;
	int m_xMax;
	int m_zMin;
	int m_zMax;
	int m_yMin;
	int m_yMax;
	int m_reqXMin;
	int m_reqXMax;
	int m_reqYMin;
	int m_reqYMax;
	int m_reqZMin;
	int m_reqZMax;
	int m_reqYMinNode;	// Node offset within a map block
	int m_reqYMaxNode;	// Node offset within a map block
	int m_mapWidth;
	int m_mapHeight;
	std::list<BlockPos> m_positions;
	std::map<int, std::string> m_nameMap;
	ColorMap m_colors;
	uint16_t m_readedPixels[16];
	std::set<std::string> m_unknownNodes;

	int m_blockAirId;
	int m_blockIgnoreId;
}; /* -----  end of class TileGenerator  ----- */

#endif /* end of include guard: TILEGENERATOR_H_JJNUCARH */

