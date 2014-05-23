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
#include <climits>
#include <iosfwd>
#include <list>
#if __cplusplus >= 201103L
#include <unordered_map>
#else
#include <map>
#endif
#include <set>
#include <stdint.h>
#include <string>
#include <string>
#include "types.h"
#include "PixelAttributes.h"
#include "BlockPos.h"
#include "Color.h"
#include "db.h"

#define TILE_WORLDCENTERED	INT_MAX
#define TILE_AT_WORLDCENTER	(INT_MAX - 1)
#define TILE_MAPCENTERED	INT_MIN
#define TILE_AT_MAPCENTER	(INT_MIN + 1)

class TileGenerator
{
private:
#if __cplusplus >= 201103L
	typedef std::unordered_map<std::string, ColorEntry> ColorMap;
	typedef std::unordered_map<int, std::string> NodeID2NameMap;
#else
	typedef std::map<std::string, ColorEntry> ColorMap;
	typedef std::map<int, std::string> NodeID2NameMap;
#endif
public:
	struct DrawObject {
		void setCenter(const NodeCoord &c) { haveCenter = true; center = c; }
		void setCorner1(const NodeCoord &c) { haveCenter = false; corner1 = c; }
		void setDimensions(const NodeCoord &d) { haveDimensions = true; dimensions = d; }
		void setCorner2(const NodeCoord &c) { haveDimensions = false; corner2 = c; }
		enum Type {
			Unknown,
			Point,
			Line,
			Ellipse,
			Rectangle,
			Text
		};
		bool world;
		Type type;
		bool haveCenter;
		NodeCoord corner1;
		NodeCoord center;
		bool haveDimensions;
		NodeCoord corner2;
		NodeCoord dimensions;
		Color color;
		std::string text;
	};

	struct UnpackError
	{
		BlockPos pos;
		const char *type;
		size_t offset;
		size_t length;
		size_t dataLength;
		UnpackError(const char *t, size_t o, size_t l, size_t dl) : type(t), offset(o), length(l), dataLength(dl) {}
	};

	TileGenerator();
	~TileGenerator();
	void setBgColor(const Color &bgColor);
	void setScaleColor(const Color &scaleColor);
	void setOriginColor(const Color &originColor);
	void setPlayerColor(const Color &playerColor);
	Color parseColor(const Color &color);
	void setDrawOrigin(bool drawOrigin);
	void setDrawPlayers(bool drawPlayers);
	void setDrawScale(bool drawScale);
	void setDrawAlpha(bool drawAlpha);
	void drawObject(const DrawObject &object) { m_drawObjects.push_back(object); }
	void setShading(bool shading);
	void setGeometry(const NodeCoord &corner1, const NodeCoord &corner2);
	void setMinY(int y);
	void setMaxY(int y);
	void setShrinkGeometry(bool shrink);
	void setBlockGeometry(bool block);
	void setSqliteCacheWorldRow(bool cacheWorldRow);
	void setTileBorderColor(const Color &tileBorderColor);
	void setTileBorderSize(int size);
	void setTileSize(int width, int heigth);
	void setTileOrigin(int x, int y);
	void setTileCenter(int x, int y);
	void enableProgressIndicator(void);
	void parseColorsFile(const std::string &fileName);
	void setBackend(std::string backend);
	void generate(const std::string &input, const std::string &output);

private:
	void parseColorsStream(std::istream &in, const std::string &filename);
	std::string getWorldDatabaseBackend(const std::string &input);
	void openDb(const std::string &input);
	void loadBlocks();
	BlockPos decodeBlockPos(int64_t blockId) const;
	void createImage();
	void computeMapParameters();
	void computeTileParameters(
                // Input parameters
                int minPos,
                int maxPos,
                int mapStartNodeOffset,
                int mapEndNodeOffset,
                int tileOrigin,
                int tileSize,
                // Input / Output parameters
                int &pictSize,
                // Output parameters
                int &tileBorderCount,
                int &tileMapOffset,
                int &tileMapExcess,
                // Behavior selection
                bool ascending);
	void renderMap();
	std::list<int> getZValueList() const;
	void pushPixelRows(int zPosLimit);
	void processMapBlock(const DB::Block &block);
	void renderMapBlock(const ustring &mapBlock, const BlockPos &pos, int version);
	void renderScale();
	void renderOrigin();
	void renderPlayers(const std::string &inputPath);
	void renderDrawObjects();
	void writeImage(const std::string &output);
	void printUnknown();
	int mapX2ImageX(int val) const;
	int mapY2ImageY(int val) const;
	int worldX2ImageX(int val) const;
	int worldZ2ImageY(int val) const;
	int worldBlockX2StoredX(int xPos) const { return (xPos - m_xMin) * 16; }
	int worldBlockZ2StoredY(int zPos) const { return (m_zMax - zPos) * 16; }

public:
	int verboseCoordinates;
	bool verboseStatistics;
	bool progressIndicator;

private:
	Color m_bgColor;
	Color m_scaleColor;
	Color m_originColor;
	Color m_playerColor;
	Color m_tileBorderColor;
	bool m_drawOrigin;
	bool m_drawPlayers;
	bool m_drawScale;
	bool m_drawAlpha;
	bool m_shading;
	int m_border;
	std::string m_backend;
	bool m_shrinkGeometry;
	bool m_blockGeometry;
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
	int m_reqYMinNode;		// Node offset within a map block
	int m_reqYMaxNode;		// Node offset within a map block
	int m_storedWidth;
	int m_storedHeight;
	int m_mapXStartNodeOffset;
	int m_mapYStartNodeOffset;
	int m_mapXEndNodeOffset;
	int m_mapYEndNodeOffset;
	int m_nextStoredYCoord;
	int m_tileXOrigin;
	int m_tileZOrigin;
	int m_tileXCentered;
	int m_tileYCentered;
	int m_tileWidth;
	int m_tileHeight;
	int m_tileBorderSize;
	int m_tileMapXOffset;
	int m_tileMapYOffset;
	int m_tileBorderXCount;
	int m_tileBorderYCount;
	int m_pictWidth;
	int m_pictHeight;
	std::list<BlockPos> m_positions;
	NodeID2NameMap m_nameMap;
	ColorMap m_colors;
	uint16_t m_readedPixels[16];
	std::set<std::string> m_unknownNodes;
	std::vector<DrawObject> m_drawObjects;

	int m_blockAirId;
	int m_blockIgnoreId;
}; /* -----  end of class TileGenerator  ----- */

#endif /* end of include guard: TILEGENERATOR_H_JJNUCARH */

