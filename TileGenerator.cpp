/*
 * =====================================================================
 *        Version:  1.0
 *        Created:  23.08.2012 12:35:53
 *         Author:  Miroslav Bendík
 *        Company:  LinuxOS.sk
 * =====================================================================
 */
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <fstream>
#include <gdfontmb.h>
#include <gdfonts.h>
#include <gdfontt.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include "config.h"
#include "Settings.h"
#include "PlayerAttributes.h"
#include "TileGenerator.h"
#include "ZlibDecompressor.h"
#if USE_SQLITE3
#include "db-sqlite3.h"
#endif
#if USE_POSTGRESQL
#include "db-postgresql.h"
#endif
#if USE_LEVELDB
#include "db-leveldb.h"
#endif
#if USE_REDIS
#include "db-redis.h"
#endif

#define MESSAGE_WIDTH 25
#define MAX_NOPREFETCH_VOLUME (1LL<<24)
#define MIN_NOPREFETCH_VOLUME (1LL<<16)
#define MAX_NOPREFETCH_VOLUME_EXAMPLE "16384x256x16384"


using namespace std;

static inline void checkDataLimit(const char *type, size_t offset, size_t length, size_t dataLength)
{
	if (offset + length > dataLength)
		throw TileGenerator::UnpackError(type, offset, length, dataLength);
}

static inline uint8_t readU8(const unsigned char *data, size_t offset, size_t dataLength)
{
	checkDataLimit("uint8", offset, 1, dataLength);
	return data[offset];
}

static inline uint16_t readU16(const unsigned char *data, size_t offset, size_t dataLength)
{
	checkDataLimit("uint16", offset, 2, dataLength);
	return data[offset] << 8 | data[offset + 1];
}

static inline void readString(string &str, const unsigned char *data, size_t offset, size_t length, size_t dataLength)
{
	checkDataLimit("string", offset, length, dataLength);
	str = string(reinterpret_cast<const char *>(data) + offset, length);
}

static inline void checkBlockNodeDataLimit(int version, size_t dataLength)
{
	int datapos = 16 * 16 * 16;
	if (version >= 24) {
		size_t index = datapos << 1;
		checkDataLimit("node:24", index, 2, dataLength);
	}
	else if (version >= 20) {
		checkDataLimit("node:20", datapos + 0x2000, 1, dataLength);
	}
	else {
		std::ostringstream oss;
		oss << "Unsupported map version " << version;
		throw std::runtime_error(oss.str());
	}
}

static inline int readBlockContent(const unsigned char *mapData, int version, int datapos)
{
	if (version >= 24) {
		size_t index = datapos << 1;
		return (mapData[index] << 8) | mapData[index + 1];
	}
	else if (version >= 20) {
		if (mapData[datapos] <= 0x80) {
			return mapData[datapos];
		}
		else {
			return (int(mapData[datapos]) << 4) | (int(mapData[datapos + 0x2000]) >> 4);
		}
	}
	else {
		std::ostringstream oss;
		oss << "Unsupported map version " << version;
		throw std::runtime_error(oss.str());
	}
}

static const ColorEntry nodeColorNotDrawnObject;
const ColorEntry *TileGenerator::NodeColorNotDrawn = &nodeColorNotDrawnObject;

const BlockPos TileGenerator::BlockPosLimitMin(MAPBLOCK_MIN, MAPBLOCK_MIN, MAPBLOCK_MIN);
const BlockPos TileGenerator::BlockPosLimitMax(MAPBLOCK_MAX, MAPBLOCK_MAX, MAPBLOCK_MAX);

struct HeightMapColor
{
	int height[2];
	Color color[2];
};

TileGenerator::TileGenerator():
	verboseCoordinates(0),
	verboseReadColors(0),
	verboseStatistics(0),
	progressIndicator(false),
	m_silenceSuggestions(0),
	m_heightMap(false),
	m_heightMapYScale(1),
	m_seaLevel(0),
	m_bgColor(255, 255, 255),
	m_blockDefaultColor(0, 0, 0, 0),
	m_scaleColor(0, 0, 0),
	m_originColor(255, 0, 0),
	m_playerColor(255, 0, 0),
	m_tileBorderColor(0, 0, 0),
	m_drawOrigin(false),
	m_drawPlayers(false),
	m_drawScale(DRAWSCALE_NONE),
	m_drawAlpha(false),
	m_drawAir(false),
	m_drawIgnore(false),
	m_shading(true),
	m_backend(DEFAULT_BACKEND),
	m_requestedBackend(DEFAULT_BACKEND),
	m_scanEntireWorld(false),
	m_shrinkGeometry(true),
	m_blockGeometry(false),
	m_scaleFactor(1),
	m_chunkSize(0),
	m_sideScaleMajor(0),
	m_sideScaleMinor(0),
	m_heightScaleMajor(0),
	m_heightScaleMinor(0),
	m_generateNoPrefetch(0),
	m_databaseFormatSet(false),
	m_databaseFormat(BlockPos::Unknown),
	m_reportDatabaseFormat(false),
	m_image(0),
	m_xMin(INT_MAX/16-1),
	m_xMax(INT_MIN/16+1),
	m_zMin(INT_MAX/16-1),
	m_zMax(INT_MIN/16+1),
	m_yMin(INT_MAX/16-1),
	m_yMax(INT_MIN/16+1),
	m_reqXMin(MAPBLOCK_MIN),
	m_reqXMax(MAPBLOCK_MAX),
	m_reqYMin(MAPBLOCK_MIN),
	m_reqYMax(MAPBLOCK_MAX),
	m_reqZMin(MAPBLOCK_MIN),
	m_reqZMax(MAPBLOCK_MAX),
	m_reqYMinNode(0),
	m_reqYMaxNode(15),
	m_YMinMapped(MAPBLOCK_MAX),
	m_YMaxMapped(MAPBLOCK_MIN),
	m_emptyMapArea(0),
	m_mapXStartNodeOffset(0),
	m_mapYStartNodeOffset(0),
	m_mapXEndNodeOffset(0),
	m_mapYEndNodeOffset(0),
	m_tileXOrigin(TILECENTER_AT_WORLDCENTER),
	m_tileZOrigin(TILECENTER_AT_WORLDCENTER),
	m_tileWidth(0),
	m_tileHeight(0),
	m_tileBorderSize(1),
	m_tileMapXOffset(0),
	m_tileMapYOffset(0),
	m_surfaceHeight(INT_MIN),
	m_surfaceDepth(INT_MAX),
	m_gdStringConv(NULL)
{
	// Libgd requires ISO8859-2 :-(
	// Internally, we use UTF-8. Assume minetest does the same... (if not, it's probably broken)
	m_gdStringConv = CharEncodingConverter::createStandardConverter("ISO8859-2", "UTF-8");

	memset(&m_databaseFormatFound, 0, sizeof(m_databaseFormatFound));
	// Load default grey colors.
	m_heightMapColors.push_back(HeightMapColor(INT_MIN, Color(0,0,0), -129, Color(0,0,0)));
	m_heightMapColors.push_back(HeightMapColor(-128, Color(0,0,0), 127, Color(255,255,255)));
	m_heightMapColors.push_back(HeightMapColor(128, Color(255,255,255), INT_MAX, Color(255,255,255)));
}

TileGenerator::~TileGenerator()
{
	closeDb();
}

void TileGenerator::setSilenceSuggestion(unsigned flags)
{
	m_silenceSuggestions |= flags;
}

void TileGenerator::setGenerateNoPrefetch(int enable)
{
	m_generateNoPrefetch = enable;
}

void TileGenerator::setDBFormat(BlockPos::StrFormat format, bool query)
{
	m_databaseFormat = format;
	m_databaseFormatSet = true;
	m_reportDatabaseFormat = query;
}

void TileGenerator::setHeightMap(bool enable)
{
	m_heightMap = enable;
}

void TileGenerator::setHeightMapYScale(float scale)
{
	m_heightMapYScale = scale;
}

void TileGenerator::setSeaLevel(int level)
{
	m_seaLevel = level;
}

void TileGenerator::setBgColor(const Color &bgColor)
{
	m_bgColor = bgColor;
}

void TileGenerator::setBlockDefaultColor(const Color &color)
{
	m_blockDefaultColor = color;
	// Any value will do, except for 0
	m_blockDefaultColor.a = 1;
}

void TileGenerator::setShrinkGeometry(bool shrink)
{
	m_shrinkGeometry = shrink;
}

void TileGenerator::setBlockGeometry(bool block)
{
	m_blockGeometry = block;
}

void TileGenerator::setScaleColor(const Color &scaleColor)
{
	m_scaleColor = scaleColor;
}

void TileGenerator::setOriginColor(const Color &originColor)
{
	m_originColor = originColor;
}

void TileGenerator::setPlayerColor(const Color &playerColor)
{
	m_playerColor = playerColor;
}

void TileGenerator::setHeightMapColor(const Color &color0, const Color &color1)
{
	m_heightMapColors.clear();
	m_heightMapColors.push_back(HeightMapColor(INT_MIN, color0, -129, color0));
	m_heightMapColors.push_back(HeightMapColor(-128, color0, 127, color1));
	m_heightMapColors.push_back(HeightMapColor(128, color1, INT_MAX, color1));
}

void TileGenerator::setTileBorderColor(const Color &tileBorderColor)
{
	m_tileBorderColor = tileBorderColor;
}

void TileGenerator::setTileBorderSize(int size)
{
	m_tileBorderSize = size;
}

void TileGenerator::setTileSize(int width, int heigth)
{
	m_tileWidth = width;
	m_tileHeight = heigth;
}

void TileGenerator::setTileOrigin(int x, int y)
{
	m_tileXOrigin = x;
	m_tileZOrigin = y;
	m_tileXCentered = false;
	m_tileYCentered = false;
}

void TileGenerator::setTileCenter(int x, int y)
{
	m_tileXOrigin = x;
	m_tileZOrigin = y;
	m_tileXCentered = true;
	m_tileYCentered = true;
}

void TileGenerator::setScaleFactor(int f)
{
	m_scaleFactor = f;
}

void TileGenerator::setDrawOrigin(bool drawOrigin)
{
	m_drawOrigin = drawOrigin;
}

void TileGenerator::setDrawPlayers(bool drawPlayers)
{
	m_drawPlayers = drawPlayers;
}

void TileGenerator::setDrawScale(int scale)
{
	m_drawScale = (scale & DRAWSCALE_MASK) | (m_drawScale & DRAWHEIGHTSCALE_MASK & ((~scale & DRAWSCALE_MASK) << 4));
}

void TileGenerator::setDrawHeightScale(int scale)
{
	unsigned s = scale;
	int bits = 0;
	for (; s; s >>= 1)
		if ((s & 0x1)) bits++;
	if (bits > 1)
		throw std::runtime_error(std::string("Multiple height scale positions requested"));
	m_drawScale = (scale & DRAWHEIGHTSCALE_MASK) | (m_drawScale & DRAWSCALE_MASK & ((~scale & DRAWHEIGHTSCALE_MASK) >> 4));
}

void TileGenerator::setSideScaleInterval(int major, int minor)
{
	m_sideScaleMajor = major;
	m_sideScaleMinor = minor;
}

void TileGenerator::setHeightScaleInterval(int major, int minor)
{
	m_heightScaleMajor = major;
	m_heightScaleMinor = minor;
}

void TileGenerator::setDrawAlpha(bool drawAlpha)
{
    m_drawAlpha = drawAlpha;
}

void TileGenerator::setDrawAir(bool drawAir)
{
	m_drawAir = drawAir;
}

void TileGenerator::setDrawIgnore(bool drawIgnore)
{
	m_drawIgnore = drawIgnore;
}

void TileGenerator::setShading(bool shading)
{
	m_shading = shading;
}

void TileGenerator::enableProgressIndicator(void)
{
	progressIndicator = true;
}

void TileGenerator::setGeometry(const NodeCoord &corner1, const NodeCoord &corner2)
{
	if (corner1.x() > 0) {
		m_reqXMin = corner1.x() / 16;
	}
	else {
		m_reqXMin = (corner1.x() - 15) / 16;
	}
	if (corner1.y() > 0) {
		m_reqZMin = corner1.y() / 16;
	}
	else {
		m_reqZMin = (corner1.y() - 15) / 16;
	}
	m_mapXStartNodeOffsetOrig = m_mapXStartNodeOffset = corner1.x() - m_reqXMin * 16;
	m_mapYEndNodeOffsetOrig = m_mapYEndNodeOffset = m_reqZMin * 16 - corner1.y();

	if (corner2.x() > 0) {
		m_reqXMax = corner2.x() / 16;
	}
	else {
		m_reqXMax = (corner2.x() - 15) / 16;
	}
	if (corner2.y() > 0) {
		m_reqZMax = corner2.y() / 16;
	}
	else {
		m_reqZMax = (corner2.y() - 15) / 16;
	}
	m_mapXEndNodeOffsetOrig = m_mapXEndNodeOffset = corner2.x() - (m_reqXMax * 16 + 15);
	m_mapYStartNodeOffsetOrig = m_mapYStartNodeOffset = (m_reqZMax * 16 + 15) - corner2.y();
}

void TileGenerator::setMinY(int y)
{
	if (y > 0) {
		m_reqYMin = y / 16;
	}
	else {
		m_reqYMin = (y - 15) / 16;
	}
	m_reqYMinNode = y - 16 * m_reqYMin;
}

void TileGenerator::setMaxY(int y)
{
	if (y > 0) {
		m_reqYMax = y / 16;
	}
	else {
		m_reqYMax = (y - 15) / 16;
	}
	m_reqYMaxNode = y - 16 * m_reqYMax;
}

void TileGenerator::parseNodeColorsFile(const std::string &fileName)
{
	m_nodeColors.clear();
	parseDataFile(fileName, 0, "map colors", &TileGenerator::parseNodeColorsLine);
}

void TileGenerator::parseHeightMapNodesFile(const std::string &fileName)
{
	m_nodeColors.clear();
	parseDataFile(fileName, 0, "heightmap nodes", &TileGenerator::parseHeightMapNodesLine);
}

void TileGenerator::parseHeightMapColorsFile(const std::string &fileName)
{
	m_heightMapColors.clear();
	parseDataFile(fileName, 0, "heightmap colors", &TileGenerator::parseHeightMapColorsLine);
}

void TileGenerator::parseDataFile(const std::string &fileName, int depth, const char *type,
	void (TileGenerator::*parseLine)(const std::string &line, std::string name,
		istringstream &iline, int linenr, const std::string &filename))
{
	if (depth > 100)
		throw std::runtime_error(std::string("Excessive inclusion depth of ") + type + " files - suspected recursion (i.e. cycle); current file: '" + fileName + "'");
	if (depth == 0 && verboseReadColors >= 2)
		cout << "Checking for " << type << " file: " << fileName << std::endl;
	ifstream in;
	in.open(fileName.c_str(), ifstream::in);
	if (!in.is_open()) {
		throw std::runtime_error(std::string("Failed to open ") + type + " file '" + fileName + "'");
		return;
	}
	if (verboseReadColors >= 1)
		cout << "Reading " << type << " file:  " << fileName << std::endl;
	parseDataStream(in, fileName, depth, type, parseLine);
	in.close();
}

void TileGenerator::setBackend(std::string backend)
{
	m_requestedBackend = backend;
}

void TileGenerator::setScanEntireWorld(bool enable)
{
	m_scanEntireWorld = enable;
}

void TileGenerator::setChunkSize(int size)
{
	m_chunkSize = size;
}

void TileGenerator::sanitizeParameters(void)
{
	if (m_scaleFactor > 1) {
		int dx0 = m_mapXStartNodeOffset % m_scaleFactor;
		int dx1 = -m_mapXEndNodeOffset % m_scaleFactor;
		int dy0 = m_mapYStartNodeOffset % m_scaleFactor;
		int dy1 = -m_mapYEndNodeOffset % m_scaleFactor;
		if (dx0 || dx1 || dy0 || dy1) {
			m_mapXStartNodeOffsetOrig = m_mapXStartNodeOffset;
			m_mapXEndNodeOffsetOrig = m_mapXEndNodeOffset;
			m_mapYStartNodeOffsetOrig = m_mapYStartNodeOffset;
			m_mapYEndNodeOffsetOrig = m_mapYEndNodeOffset;
			m_mapXStartNodeOffset -= dx0;
			m_mapXEndNodeOffset += dx1;
			m_mapYStartNodeOffset -= dy0;
			m_mapYEndNodeOffset += dy1;
			std::cerr << "NOTE: rounding requested map boundaries to a multiple of the scale factor"
			    << "(changes -" << dx0 << ",+" << dx1 << " x -" << dy1 << ",+" << dy0 << ")" << std::endl;
		}

		dx0 = dy0 = 0;
		if (m_tileWidth != TILESIZE_CHUNK)
			dx0 = m_tileWidth % m_scaleFactor;
		if (dx0) dx0 = m_scaleFactor - dx0;
		if (m_tileHeight != TILESIZE_CHUNK)
			dy0 = m_tileHeight % m_scaleFactor;
		if (dy0) dy0 = m_scaleFactor - dy0;
		if (dx0 || dy0) {
			m_tileWidth += dx0;
			m_tileHeight += dy0;
			std::cerr << "NOTE: rounding requested tile size up to nearest multiple of the scale factor: "
			    << m_tileWidth << " x " << m_tileHeight << std::endl;
		}
	}
}

void TileGenerator::generate(const std::string &input, const std::string &output)
{
	string input_path = input;
	if (input_path[input.length() - 1] != PATH_SEPARATOR) {
		input_path += PATH_SEPARATOR;
	}

	openDb(input_path);
	sanitizeParameters();
	loadBlocks();
	if (m_xMin > m_xMax || m_yMin > m_yMax || m_zMin > m_zMax) {
		std::cout << "World is empty: no map generated" << std::endl;
		return;
	}
	computeMapParameters(input);
	createImage();
	renderMap();
	if ((m_drawScale & DRAWSCALE_MASK)) {
		renderScale();
	}
	if (m_heightMap && (m_drawScale & DRAWHEIGHTSCALE_MASK)) {
		renderHeightScale();
	}
	if (m_drawOrigin) {
		renderOrigin();
	}
	if (m_drawPlayers) {
		renderPlayers(input_path);
	}
	if (!m_drawObjects.empty()) {
		renderDrawObjects();
	}
	closeDb();
	if (progressIndicator)
	    cout << "Writing image...\r" << std::flush;
	writeImage(output);
	if (progressIndicator)
	    cout << std::setw(20) << " " <<  "\r" << std::flush;
	printUnknown();
}

void TileGenerator::parseDataStream(std::istream &in, const std::string &filename, int depth, const char *type,
	void (TileGenerator::*parseLine)(const std::string &line, std::string name,
		istringstream &iline, int linenr, const std::string &filename))
{
	string line;
	int linenr = 0;
	for (std::getline(in,line); in.good(); std::getline(in,line)) {
		linenr++;
		size_t comment = line.find_first_of('#');
		if (comment != string::npos)
			line.erase(comment);
		istringstream iline;
		iline.str(line);
		iline >> std::skipws;
		string name;
		iline >> name >> std::ws;
		if (name.length() == 0)
			continue;
		if (name == "@include") {
			string includeFile;
			getline(iline,includeFile);
			size_t lastChar = includeFile.find_last_not_of(" \t\r\n");
			if (lastChar != string::npos)
				includeFile.erase(lastChar + 1);
			if (includeFile == "") {
				std::cerr << filename << ":" << linenr << ": include filename missing in colors file (" << line << ")" << std::endl;
				continue;
			}
#if ! (MSDOS || __OS2__ || __NT__ || _WIN32)
			// This same feature seems needlessly complicated on windows - so it is not supported
			if (includeFile[0] != '/') {
				string includePath = filename;
				size_t offset = includePath.find_last_of('/');
				if (offset != string::npos) {
					includePath.erase(offset);
					includeFile = includePath + '/' + includeFile;
				}
			}
#endif
			parseDataFile(includeFile, depth + 1, type, parseLine);
		}
		else {
			(this->*parseLine)(line, name, iline, linenr, filename);
		}
	}
	if (!in.eof()) {
		std::cerr << filename << ": error reading colors file after line " << linenr << std::endl;
	}
}

void TileGenerator::parseNodeColorsLine(const std::string &line, std::string name, istringstream &iline, int linenr, const std::string &filename)
{
	iline >> std::ws >> std::skipws;
	if (iline.good() && iline.peek() == '-') {
		char c;
		iline >> c >> std::ws;
		if (iline.fail() || !iline.eof()) {
			std::cerr << filename << ":" << linenr << ": bad line in colors file (" << line << ")" << std::endl;
			return;
		}
		m_nodeColors.erase(name);
	}
	else {
		int r, g, b, a, t, f;
		std::string flags;
		ColorEntry color;
		iline >> r;
		iline >> g;
		iline >> b;
		if (iline.fail()) {
			std::cerr << filename << ":" << linenr << ": bad line in colors file (" << line << ")" << std::endl;
			return;
		}
		a = 0xff;
		iline >> std::ws;
		if (iline.good() && isdigit(iline.peek()))
			iline >> a >> std::ws;
		t = 0;
		if (iline.good() && isdigit(iline.peek()))
			iline >> t >> std::ws;
		if (iline.good() && !isdigit(iline.peek()))
			iline >> flags >> std::ws;
		f = 0;
		if (!iline.fail() && flags != "") {
			for(size_t i = 0; i < flags.length(); i++) {
				if (flags[i] == ',')
					flags[i]= ' ';
			}
			istringstream iflags(flags);
			std::string flag;
			iflags >> flag;
			while (!iflags.fail()) {
				if (flag == "ignore")
					f |= ColorEntry::FlagIgnore;
				else if (flag == "air")
					f |= ColorEntry::FlagAir;
				iflags >> flag;
			}
		}
		color = ColorEntry(r,g,b,a,t,f);
		if ((m_drawAlpha && a == 0xff) || (!m_drawAlpha && a != 0xff)) {
			// If drawing alpha, and the colors file contains both
			// an opaque entry and a non-opaque entry for a name, prefer
			// the non-opaque entry
			// If not drawing alpha, and the colors file contains both
			// an opaque entry and a non-opaque entry for a name, prefer
			// the opaque entry
			// Otherwise, any later entry overrides any previous entry
			NodeColorMap::iterator it = m_nodeColors.find(name);
			if (it != m_nodeColors.end()) {
				if (m_drawAlpha && (a == 0xff && it->second.a != 0xff)) {
					// drawing alpha: don't use opaque color to override
					// non-opaque color
					return;
				}
				if (!m_drawAlpha && (a != 0xff && it->second.a == 0xff)) {
					// not drawing alpha: don't use non-opaque color to
					// override opaque color
					return;
				}
			}
		}
		m_nodeColors[name] = color;
	}
}

void TileGenerator::parseHeightMapColorsLine(const std::string &line, std::string name, istringstream &iline, int linenr, const std::string &filename)
{
	(void) name;
	int height[2];
	Color color[2];
	iline.str(line);		// Reset
	for (int i = 0; i < 2; i++) {
		iline >> std::ws;
		char c = iline.peek();
		iline >> height[i];
		if (iline.fail()) {
			std::string value;
			iline.clear();
			iline >> std::ws;
			iline >> value >> std::ws;
			if (!iline.fail()) {
				if (value == "-oo" || (c == '-' && value=="oo"))
					height[i] = INT_MIN;
				else if (value == "oo" || value == "+oo")
					height[i] = INT_MAX;
				else {
					iline.clear(ios::failbit);	// Set to failed
					break;
				}
			}
		}
	}
	for (int i = 0; i < 2; i++) {
		int r, g, b;
		iline >> r;
		iline >> g;
		iline >> b;
		color[i] = Color(r,g,b);
	}
	if (height[0] > height[1]) {
		{
			int tmp = height[0];
			height[0] = height[1];
			height[1] = tmp;
		}
		{
			Color tmp = color[0];
			color[0] = color[1];
			color[1] = tmp;
		}
	}
	iline >> std::ws;
	if (iline.fail() || !iline.eof()) {
		std::cerr << filename << ":" << linenr << ": bad line in heightmap colors file (" << line << ")" << std::endl;
		return;
	}
	m_heightMapColors.push_back(HeightMapColor(height[0], color[0], height[1], color[1]));
}

void TileGenerator::parseHeightMapNodesLine(const std::string &line, std::string name, istringstream &iline, int linenr, const std::string &filename)
{
	if (name == "-") {
		iline >> std::ws >> name >> std::ws;
		m_nodeColors.erase(name);
	}
	else {
		m_nodeColors[name] = ColorEntry(0,0,0,255,1,0);		// Dummy entry - but must not be transparent
	}
	// Don't care if not at eof (== really eol). We might be reading a colors.txt file...
	if (iline.fail()) {
		std::cerr << filename << ":" << linenr << ": bad line in heightmap nodes file (" << line << ")" << std::endl;
		return;
	}
}

std::string TileGenerator::getWorldDatabaseBackend(const std::string &input)
{
	Settings world_mt(input + PATH_SEPARATOR + "world.mt");
	return world_mt.get("backend", "sqlite3");
}

int TileGenerator::getMapChunkSize(const std::string &input)
{
	int chunkSize = -1;

	std::string worldFile = input + PATH_SEPARATOR + "map_meta.txt";
	ifstream in;
	in.open(worldFile.c_str(), ifstream::in);
	if (!in.is_open()) {
		cerr << "Could not obtain world chunk size: failed to open map_meta.txt - using default size ("
			<< CHUNK_SIZE_DEFAULT << ")" << std::endl;
		return CHUNK_SIZE_DEFAULT;
	}

	std::string line;
	int linenr = 0;
	for (std::getline(in,line); in.good(); std::getline(in,line)) {
		linenr++;
		istringstream iline;
		iline.str(line);
		iline >> std::skipws;
		string variable;
		char eq;
		iline >> variable;
		if (variable != "chunksize")
			continue;
		iline >> std::ws >> eq;
		iline >> chunkSize;
		if (in.fail() || eq != '=') {
			cerr << "Could not obtain world chunk size: error parsing configuration line - using default size ("
				<< CHUNK_SIZE_DEFAULT << ")" << std::endl;
			return CHUNK_SIZE_DEFAULT;
		}
		if (chunkSize <= 0) {
			cerr << "Invalid chunk size found in map_meta.txt (" << chunkSize << ") - using default size ("
				<< CHUNK_SIZE_DEFAULT << ")" << std::endl;
			return CHUNK_SIZE_DEFAULT;
		}
	}
	in.close();
	if (chunkSize < 0) return CHUNK_SIZE_DEFAULT;
	else return chunkSize;
}

void TileGenerator::openDb(const std::string &input)
{
	m_backend = m_requestedBackend;
	bool unsupported = false;
	if (m_backend == "auto")
		m_backend = getWorldDatabaseBackend(input);

	if(m_backend == "sqlite3") {
#if USE_SQLITE3
		DBSQLite3 *db;
		m_db = db = new DBSQLite3(input);
		m_scanEntireWorld = true;
#else
		unsupported = true;
#endif
	}
	else if (m_backend == "postgresql") {
#if USE_POSTGRESQL
		DBPostgreSQL *db;
		m_db = db = new DBPostgreSQL(input);
#else
		unsupported = true;
#endif
	}
	else if (m_backend == "leveldb") {
#if USE_LEVELDB
		m_db = new DBLevelDB(input);
		m_scanEntireWorld = true;
#else
		unsupported = true;
#endif
	}
	else if (m_backend == "redis") {
#if USE_REDIS
		m_db = new DBRedis(input);
		m_scanEntireWorld = true;
#else
		unsupported = true;
#endif
	}
	else if (m_requestedBackend == "auto")
		throw std::runtime_error(((std::string) "World uses unrecognised database backend: ") + m_backend);
	else
		throw std::runtime_error(((std::string) "Internal error: unknown database backend: ") + m_requestedBackend);

	if (unsupported)
		throw std::runtime_error(((std::string) "World uses backend '") + m_backend + ", which was not enabled at compile-time.");
}

void TileGenerator::closeDb()
{
	if (m_db) {
		delete m_db;
		m_db = NULL;
	}
}

void TileGenerator::loadBlocks()
{
	int mapXMin, mapXMax;
	int mapYMin, mapYMax;
	int mapZMin, mapZMax;
	int geomYMin, geomYMax;
	long long map_blocks;
	if (verboseCoordinates >= 2) {
		bool partialBlocks = (m_mapXStartNodeOffset || m_mapXEndNodeOffset || m_mapYStartNodeOffset || m_mapYEndNodeOffset);
		bool adjustedGeom = (m_mapXStartNodeOffsetOrig != m_mapXStartNodeOffset || m_mapYEndNodeOffsetOrig != m_mapYEndNodeOffset
					|| m_mapXEndNodeOffsetOrig != m_mapXEndNodeOffset || m_mapYStartNodeOffsetOrig != m_mapYStartNodeOffset);
		if (partialBlocks || !m_blockGeometry || adjustedGeom) {
			cout
				<< std::setw(MESSAGE_WIDTH) << std::left
				<< (m_blockGeometry ? "Command-line Geometry:" : "Requested Geometry:")
				<< std::right
				<< std::setw(7) << m_reqXMin*16+m_mapXStartNodeOffsetOrig << ","
				<< std::setw(7) << m_reqYMin*16+m_reqYMinNode << ","
				<< std::setw(7) << m_reqZMin*16-m_mapYEndNodeOffsetOrig
				<< "  ..  "
				<< std::setw(7) << m_reqXMax*16+15+m_mapXEndNodeOffsetOrig << ","
				<< std::setw(7) << m_reqYMax*16+m_reqYMaxNode << ","
				<< std::setw(7) << m_reqZMax*16+15-m_mapYStartNodeOffsetOrig
				<< "    ("
				<< std::setw(6) << m_reqXMin << ","
				<< std::setw(6) << m_reqYMin << ","
				<< std::setw(6) << m_reqZMin
				<< "  ..  "
				<< std::setw(6) << m_reqXMax << ","
				<< std::setw(6) << m_reqYMax << ","
				<< std::setw(6) << m_reqZMax
				<< ")\n";
		}
		if (partialBlocks || m_blockGeometry) {
			cout
				<< std::setw(MESSAGE_WIDTH) << std::left
				<< (m_blockGeometry ? "Requested Geometry:" : "Block-aligned Geometry:")
				<< std::right
				<< std::setw(7) << m_reqXMin*16 << ","
				<< std::setw(7) << m_reqYMin*16+m_reqYMinNode << ","
				<< std::setw(7) << m_reqZMin*16 <<
				"  ..  "
				<< std::setw(7) << m_reqXMax*16+15 << ","
				<< std::setw(7) << m_reqYMax*16+m_reqYMaxNode << ","
				<< std::setw(7) << m_reqZMax*16+15
				<< "    ("
				<< std::setw(6) << m_reqXMin << ","
				<< std::setw(6) << m_reqYMin << ","
				<< std::setw(6) << m_reqZMin
				<< "  ..  "
				<< std::setw(6) << m_reqXMax << ","
				<< std::setw(6) << m_reqYMax << ","
				<< std::setw(6) << m_reqZMax
				<< ")\n";
		}
		if (!m_blockGeometry && adjustedGeom) {
			cout
				<< std::setw(MESSAGE_WIDTH) << std::left
				<< "Adjusted Geometry:"
				<< std::right
				<< std::setw(7) << m_reqXMin*16+m_mapXStartNodeOffset << ","
				<< std::setw(7) << m_reqYMin*16+m_reqYMinNode << ","
				<< std::setw(7) << m_reqZMin*16-m_mapYEndNodeOffset
				<< "  ..  "
				<< std::setw(7) << m_reqXMax*16+15+m_mapXEndNodeOffset << ","
				<< std::setw(7) << m_reqYMax*16+m_reqYMaxNode << ","
				<< std::setw(7) << m_reqZMax*16+15-m_mapYStartNodeOffset
				<< "    ("
				<< std::setw(6) << m_reqXMin << ","
				<< std::setw(6) << m_reqYMin << ","
				<< std::setw(6) << m_reqZMin
				<< "  ..  "
				<< std::setw(6) << m_reqXMax << ","
				<< std::setw(6) << m_reqYMax << ","
				<< std::setw(6) << m_reqZMax
				<< ")\n";
		}
	}
	if (m_blockGeometry) {
		m_mapXStartNodeOffset = 0;
		m_mapXEndNodeOffset = 0;
		m_mapYStartNodeOffset = 0;
		m_mapYEndNodeOffset = 0;
	}
	mapXMin = MAPBLOCK_MAX;
	mapXMax = MAPBLOCK_MIN;
	mapYMin = MAPBLOCK_MAX;
	mapYMax = MAPBLOCK_MIN;
	mapZMin = MAPBLOCK_MAX;
	mapZMax = MAPBLOCK_MIN;
	geomYMin = MAPBLOCK_MAX;
	geomYMax = MAPBLOCK_MIN;
	m_worldBlocks = 0;
	map_blocks = 0;
	if (m_reportDatabaseFormat && m_backend != "leveldb") {
		std::cerr << "WARNING: querying database format is only sensible when using the leveldb backend - querying disabled" << std::endl;
		m_reportDatabaseFormat = false;
	}
	if (m_reportDatabaseFormat && m_generateNoPrefetch) {
		std::cerr << "WARNING: querying database format: ignoring '--disable-blocklist-prefetch' and/or '--prescan-world=disabled'." << std::endl;
		m_generateNoPrefetch = 0;
	}
	if (m_generateNoPrefetch && !m_databaseFormatSet && m_backend == "leveldb") {
		throw(std::runtime_error("When using --disable-blocklist-prefetch with a leveldb backend, database format must be set (--database-format)"));
	}
	if (m_generateNoPrefetch) {
		if (m_generateNoPrefetch == 1) {
			long long volume = (long long)(m_reqXMax - m_reqXMin + 1) * (m_reqYMax - m_reqYMin + 1) * (m_reqZMax - m_reqZMin + 1);
			if (volume > MAX_NOPREFETCH_VOLUME) {
				std::ostringstream oss;
				// Note: the 'force' variants of the options are intentionally undocumented.
				oss << "Requested map volume is excessive for --disable-blocklist-prefetch or --prescan-world=disabled: " << std::endl
				    << " Volume is: " << volume
					    << " (" << (m_reqXMax - m_reqXMin + 1)
					    << " x " << (m_reqYMax - m_reqYMin + 1)
					    << " x " << (m_reqZMax - m_reqZMin + 1)
					    << " blocks of 16x16x16 nodes);"
					    << std::endl
				    << " Mapping will be slow. Use '--disable-blocklist-prefetch=force' or '--prescan-world=disabled-force'" << std::endl
				    << " to force this for more than " << MAX_NOPREFETCH_VOLUME << " blocks (i.e. " << MAX_NOPREFETCH_VOLUME_EXAMPLE << " nodes)";
				throw(std::runtime_error(oss.str()));
			}
		}
		if (m_shrinkGeometry) {
			std::cerr << "WARNING: geometrymode 'shrink' not supported with '--disable-blocklist-prefetch'" << std::endl;
			m_shrinkGeometry = false;
		}
		m_xMin = m_reqXMin;
		m_xMax = m_reqXMax;
		m_yMin = m_reqYMin;
		m_yMax = m_reqYMax;
		m_zMin = m_reqZMin;
		m_zMax = m_reqZMax;
	}
	else {
		if (progressIndicator)
			cout << "Scanning world (reading block list)...\r" << std::flush;
		const DB::BlockPosList *bp;
		BlockPos posMin(m_reqXMin, m_reqYMin, m_reqZMin);
		BlockPos posMax(m_reqXMax, m_reqYMax, m_reqZMax);
		if (!m_scanEntireWorld && (posMin != BlockPosLimitMin || posMax != BlockPosLimitMax))
			bp = &m_db->getBlockPosList(BlockPos(m_reqXMin, m_reqYMin, m_reqZMin), BlockPos(m_reqXMax, m_reqYMax, m_reqZMax));
		else {
			m_scanEntireWorld = true;
			bp = &m_db->getBlockPosList();
		}
		const DB::BlockPosList &blocks = *bp;
		for(DB::BlockPosList::const_iterator it = blocks.begin(); it != blocks.end(); ++it) {
			m_worldBlocks++;
			const BlockPos &pos = *it;
			m_databaseFormatFound[pos.databaseFormat()]++;
			if (pos.x() < mapXMin) {
				mapXMin = pos.x();
			}
			if (pos.x() > mapXMax) {
				mapXMax = pos.x();
			}
			if (pos.y() < mapYMin) {
				mapYMin = pos.y();
			}
			if (pos.y() > mapYMax) {
				mapYMax = pos.y();
			}
			if (pos.z() < mapZMin) {
				mapZMin = pos.z();
			}
			if (pos.z() > mapZMax) {
				mapZMax = pos.z();
			}
			if (pos.x() < m_reqXMin || pos.x() > m_reqXMax || pos.z() < m_reqZMin || pos.z() > m_reqZMax) {
				continue;
			}
			if (pos.y() < geomYMin) {
				geomYMin = pos.y();
			}
			if (pos.y() > geomYMax) {
				geomYMax = pos.y();
			}
			if (pos.y() < m_reqYMin || pos.y() > m_reqYMax) {
				continue;
			}
			map_blocks++;
			if (pos.y() < m_yMin) {
				m_yMin = pos.y();
			}
			if (pos.y() > m_yMax) {
				m_yMax = pos.y();
			}
			if (pos.x() < m_xMin) {
				m_xMin = pos.x();
			}
			if (pos.x() > m_xMax) {
				m_xMax = pos.x();
			}
			if (pos.z() < m_zMin) {
				m_zMin = pos.z();
			}
			if (pos.z() > m_zMax) {
				m_zMax = pos.z();
			}
			m_positions.push_back(pos);
		}
		if (verboseCoordinates >= 1 && m_scanEntireWorld) {
			if (mapXMin <= mapXMax || mapYMin <= mapYMax || mapZMin <= mapZMax) {
				cout
					<< std::setw(MESSAGE_WIDTH) << std::left
					<< "World Geometry:" << std::right
					<< std::setw(7) << mapXMin*16 << ","
					<< std::setw(7) << mapYMin*16 << ","
					<< std::setw(7) << mapZMin*16
					<< "  ..  "
					<< std::setw(7) << mapXMax*16+15 << ","
					<< std::setw(7) << mapYMax*16+15 << ","
					<< std::setw(7) << mapZMax*16+15
					<< "    ("
					<< std::setw(6) << mapXMin << ","
					<< std::setw(6) << mapYMin << ","
					<< std::setw(6) << mapZMin
					<< "  ..  "
					<< std::setw(6) << mapXMax << ","
					<< std::setw(6) << mapYMax << ","
					<< std::setw(6) << mapZMax
					<< ")    blocks: "
					<< std::setw(10) << m_worldBlocks << "\n";
			}
			else {
				cout
					<< std::setw(MESSAGE_WIDTH) << std::left
					<< "World Geometry:" << std::right
					<< std::setw(7) << "-" << ","
					<< std::setw(7) << "-" << ","
					<< std::setw(7) << "-"
					<< "  ..  "
					<< std::setw(7) << "-" << ","
					<< std::setw(7) << "-" << ","
					<< std::setw(7) << "-"
					<< "    ("
					<< std::setw(6) << "-" << ","
					<< std::setw(6) << "-" << ","
					<< std::setw(6) << "-"
					<< "  ..  "
					<< std::setw(6) << "-" << ","
					<< std::setw(6) << "-" << ","
					<< std::setw(6) << "-"
					<< ")    blocks: "
					<< std::setw(10) << m_worldBlocks << "\n";
			}
		}
		if (m_shrinkGeometry) {
			if (m_xMin != m_reqXMin) m_mapXStartNodeOffset = 0;
			if (m_xMax != m_reqXMax) m_mapXEndNodeOffset = 0;
			if (m_zMin != m_reqZMin) m_mapYEndNodeOffset = 0;
			if (m_zMax != m_reqZMax) m_mapYStartNodeOffset = 0;
		}
		else {
			if (verboseCoordinates >= 2) {
				if (m_xMin <= m_xMax || m_yMin <= m_yMax || m_zMin <= m_zMax) {
					cout
						<< std::setw(MESSAGE_WIDTH) << std::left
						<< "Minimal Map Geometry:" << std::right
						<< std::setw(7) << m_xMin*16 << ","
						<< std::setw(7) << m_yMin*16+m_reqYMinNode << ","
						<< std::setw(7) << m_zMin*16
						<< "  ..  "
						<< std::setw(7) << m_xMax*16+15 << ","
						<< std::setw(7) << m_yMax*16+m_reqYMaxNode << ","
						<< std::setw(7) << m_zMax*16+15
						<< "    ("
						<< std::setw(6) << m_xMin << ","
						<< std::setw(6) << m_yMin << ","
						<< std::setw(6) << m_zMin
						<< "  ..  "
						<< std::setw(6) << m_xMax << ","
						<< std::setw(6) << m_yMax << ","
						<< std::setw(6) << m_zMax
						<< ")\n";
				}
				else {
					cout
						<< std::setw(MESSAGE_WIDTH) << std::left
						<< "Minimal Map Geometry:" << std::right
						<< std::setw(7) << "-" << ","
						<< std::setw(7) << "-" << ","
						<< std::setw(7) << "-"
						<< "  ..  "
						<< std::setw(7) << "-" << ","
						<< std::setw(7) << "-" << ","
						<< std::setw(7) << "-"
						<< "    ("
						<< std::setw(6) << "-" << ","
						<< std::setw(6) << "-" << ","
						<< std::setw(6) << "-"
						<< "  ..  "
						<< std::setw(6) << "-" << ","
						<< std::setw(6) << "-" << ","
						<< std::setw(6) << "-"
						<< ")\n";
				}
			}
			m_xMin = m_reqXMin;
			m_xMax = m_reqXMax;
			m_zMin = m_reqZMin;
			m_zMax = m_reqZMax;
		}
		if (verboseCoordinates >= 2) {
			if (geomYMin <= geomYMax) {
				cout
					<< std::setw(MESSAGE_WIDTH) << std::left
					<< "Map Vertical Limits:" << std::right
					<< std::setw(7) << "x" << ","
					<< std::setw(7) << geomYMin*16 << ","
					<< std::setw(7) << "z"
					<< "  ..  "
					<< std::setw(7) << "x" << ","
					<< std::setw(7) << geomYMax*16+15 << ","
					<< std::setw(7) << "z"
					<< "    ("
					<< std::setw(6) << "x" << ","
					<< std::setw(6) << geomYMin << ","
					<< std::setw(6) << "z"
					<< "  ..  "
					<< std::setw(6) << "x" << ","
					<< std::setw(6) << geomYMax << ","
					<< std::setw(6) << "z"
					<< ")\n";
			}
			else {
				cout
					<< std::setw(MESSAGE_WIDTH) << std::left
					<< "Map Vertical Limits:" << std::right
					<< std::setw(7) << "x" << ","
					<< std::setw(7) << "-" << ","
					<< std::setw(7) << "z"
					<< "  ..  "
					<< std::setw(7) << "x" << ","
					<< std::setw(7) << "-" << ","
					<< std::setw(7) << "z"
					<< "    ("
					<< std::setw(6) << "x" << ","
					<< std::setw(6) << "-" << ","
					<< std::setw(6) << "z"
					<< "  ..  "
					<< std::setw(6) << "x" << ","
					<< std::setw(6) << "-" << ","
					<< std::setw(6) << "z"
					<< ")\n";
			}
		}
		m_positions.sort();
	}
	if ((m_xMin <= m_xMax || m_zMin <= m_zMax) && m_yMin > m_yMax) {
		m_yMin = MAPBLOCK_MIN;
		m_yMax = MAPBLOCK_MAX;
	}
	if (verboseCoordinates >= 1) {
		if (m_xMin <= m_xMax || m_zMin <= m_zMax) {
			cout
				<< std::setw(MESSAGE_WIDTH) << std::left
				<< "Map Output Geometry:" << std::right
				<< std::setw(7) << m_xMin*16+m_mapXStartNodeOffset << ","
				<< std::setw(7) << m_yMin*16+m_reqYMinNode << ","
				<< std::setw(7) << m_zMin*16-m_mapYEndNodeOffset
				<< "  ..  "
				<< std::setw(7) << m_xMax*16+15+m_mapXEndNodeOffset << ","
				<< std::setw(7) << m_yMax*16+m_reqYMaxNode << ","
				<< std::setw(7) << m_zMax*16+15-m_mapYStartNodeOffset
				<< "    ("
				<< std::setw(6) << m_xMin << ","
				<< std::setw(6) << m_yMin << ","
				<< std::setw(6) << m_zMin
				<< "  ..  "
				<< std::setw(6) << m_xMax << ","
				<< std::setw(6) << m_yMax << ","
				<< std::setw(6) << m_zMax
				<< ")    blocks: "
				<< std::setw(10) << map_blocks << "\n";
		}
		else {
			cout
				<< std::setw(MESSAGE_WIDTH) << std::left
				<< "Map Output Geometry:" << std::right
				<< std::setw(7) << "-" << ","
				<< std::setw(7) << "-" << ","
				<< std::setw(7) << "-"
				<< "  ..  "
				<< std::setw(7) << "-" << ","
				<< std::setw(7) << "-" << ","
				<< std::setw(7) << "-"
				<< "    ("
				<< std::setw(6) << "-" << ","
				<< std::setw(6) << "-" << ","
				<< std::setw(6) << "-"
				<< "  ..  "
				<< std::setw(6) << "-" << ","
				<< std::setw(6) << "-" << ","
				<< std::setw(6) << "-"
				<< ")    blocks: "
				<< std::setw(10) << map_blocks << "\n";
		}
	}
	if (m_backend == "leveldb" && !m_generateNoPrefetch) {
		if (m_databaseFormatFound[BlockPos::AXYZ] && m_databaseFormatFound[BlockPos::I64])
			m_recommendedDatabaseFormat = "mixed";
		else if (m_databaseFormatFound[BlockPos::AXYZ])
			m_recommendedDatabaseFormat = "freeminer-axyz";
		else if (m_databaseFormatFound[BlockPos::I64])
			m_recommendedDatabaseFormat = "minetest-i64";
		else
			m_recommendedDatabaseFormat = "";
		if (m_reportDatabaseFormat || verboseStatistics >= 3) {
			cout
				<< std::setw(MESSAGE_WIDTH) << std::left
				<< "Database block format(s):" << std::endl
				<< "    " << std::setw(MESSAGE_WIDTH-4) << std::left << "Total blocks:"
					  << std::setw(15) << std::right << m_worldBlocks
					  << std::endl;
			if (m_databaseFormatFound[BlockPos::Unknown]) {
				cout
					<< "    " << std::setw(MESSAGE_WIDTH-4) << std::left << "Unknown:"
						  << std::setw(15) << std::right << m_databaseFormatFound[BlockPos::Unknown]
						  << std::endl;
			}
			cout
				<< "    " << std::setw(MESSAGE_WIDTH-4) << std::left << "Minetest-I64:"
					  << std::setw(15) << std::right << m_databaseFormatFound[BlockPos::I64]
					  << std::endl
				<< "    " << std::setw(MESSAGE_WIDTH-4) << std::left << "Freeminer-AXYZ:"
					  << std::setw(15) << std::right << m_databaseFormatFound[BlockPos::AXYZ]
					  << std::endl;
				long long other_blocks = m_worldBlocks
						- m_databaseFormatFound[BlockPos::Unknown]
						- m_databaseFormatFound[BlockPos::I64]
						- m_databaseFormatFound[BlockPos::AXYZ];
				if (other_blocks) {
					cout
					<< "    " << std::setw(MESSAGE_WIDTH-8) << std::left << "Miscounted:"
						<< std::setw(15) << std::right << other_blocks
					<< std::endl;
				}
		}
	}
}

void TileGenerator::scalePixelRows(PixelAttributes &pixelAttributes, PixelAttributes &pixelAttributesScaled, int zPosLimit) {
	int y;
	for (y = pixelAttributes.getNextY(); y <= pixelAttributes.getLastY() && y < worldBlockZ2StoredY(m_zMin - 1) + m_mapYEndNodeOffset; y++) {
		for (int x = m_mapXStartNodeOffset; x < worldBlockX2StoredX(m_xMax + 1) + m_mapXEndNodeOffset; x++) {
			#define pixel pixelAttributes.attribute(y, x)
			//PixelAttribute &pixel = pixelAttributes.attribute(y, x);
			if (pixel.nextEmpty) {
				pixelAttributesScaled.attribute(y / m_scaleFactor, x/m_scaleFactor).nextEmpty = true;
				x += 15;
				continue;
			}
#ifdef DEBUG
			int mapX = x - m_mapXStartNodeOffset;
			int mapY = y - m_mapYStartNodeOffset;
			{ int ix = mapX2ImageX(mapX / m_scaleFactor); assert(ix - borderLeft() >= 0 && ix - borderLeft() - borderRight() < m_pictWidth); }
			{ int iy = mapY2ImageY(mapY / m_scaleFactor); assert(iy - borderTop() >= 0 && iy - borderTop() - borderBottom() < m_pictHeight); }
#endif
			if (pixel.is_valid() || pixel.color().to_uint())
				pixelAttributesScaled.attribute(y / m_scaleFactor, x / m_scaleFactor).add(pixel);
			#undef pixel
		}
	}
	for (y = pixelAttributesScaled.getNextY(); y <= pixelAttributesScaled.getLastY(); y++) {
		for (int x = m_mapXStartNodeOffset / m_scaleFactor; x < (worldBlockX2StoredX(m_xMax + 1) + m_mapXEndNodeOffset) / m_scaleFactor; x++) {
			#define pixel pixelAttributesScaled.attribute(y, x)
			if (pixel.nextEmpty) {
				x += 16 / m_scaleFactor - 1;
				continue;
			}
			if (pixel.is_valid() || pixel.color().to_uint())
				pixel.normalize();
			#undef pixel
		}
	}
	int yLimit = worldBlockZ2StoredY(zPosLimit);
	if (y <= yLimit) {
		pixelAttributes.scroll(yLimit);
	}
}

void TileGenerator::pushPixelRows(PixelAttributes &pixelAttributes, int zPosLimit) {
	if (m_shading)
		// Make shading less pronounced when map is scaled down
		// (the formula for the emphasis parameter was determined (tuned) experimentally...)
		pixelAttributes.renderShading(m_scaleFactor < 3 ? 1 : 1 / sqrt(m_scaleFactor), m_drawAlpha);
	int y;
	for (y = pixelAttributes.getNextY(); y <= pixelAttributes.getLastY() && y < (worldBlockZ2StoredY(m_zMin - 1) + m_mapYEndNodeOffset) / m_scaleFactor; y++) {
		for (int x = m_mapXStartNodeOffset / m_scaleFactor; x < (worldBlockX2StoredX(m_xMax + 1) + m_mapXEndNodeOffset) / m_scaleFactor; x++) {
			int mapX = x - m_mapXStartNodeOffset / m_scaleFactor;
			int mapY = y - m_mapYStartNodeOffset / m_scaleFactor;
			#define pixel pixelAttributes.attribute(y, x)
			//PixelAttribute &pixel = pixelAttributes.attribute(y, x);
			if (pixel.nextEmpty) {
				x += 16 / m_scaleFactor - 1;
				continue;
			}
#ifdef DEBUG
			{ int ix = mapX2ImageX(mapX); assert(ix - borderLeft() >= 0 && ix - borderLeft() - borderRight() < m_pictWidth); }
			{ int iy = mapY2ImageY(mapY); assert(iy - borderTop() >= 0 && iy - borderTop() - borderBottom() < m_pictHeight); }
#endif
			if (pixel.is_valid() || pixel.color().to_uint())
				m_image->tpixels[mapY2ImageY(mapY)][mapX2ImageX(mapX)] = pixel.color().to_libgd();
			#undef pixel
		}
	}
	int yLimit = worldBlockZ2StoredY(zPosLimit) / m_scaleFactor;
	if (y <= yLimit) {
		pixelAttributes.scroll(yLimit);
	}
}

void TileGenerator::computeTileParameters(
		// Input parameters
		int minPos,
		int maxPos,
		int mapStartNodeOffset,
		int mapEndNodeOffset,
		int tileOrigin,
		int tileSize,
		// Output parameters
		int &tileBorderCount,
		int &tileMapStartOffset,
		int &tileMapEndOffset,
		// Behavior selection
		bool ascending)
{
	int start = minPos * 16 + mapStartNodeOffset - tileOrigin;
	int limit = (maxPos+1) * 16 + mapEndNodeOffset - tileOrigin;
	int shift;
	// shift values, so that start = 0..tileSize-1
	// (effect of tileOrigin is identical to (tileOrigin + tileSize)
	//  so any multiple of tileSize can be safely added)
	if (start<0)
		shift = - (start + 1) / tileSize + 1;
	else
		shift = - start / tileSize;
	start += shift * tileSize;
	limit += shift * tileSize;

	int tileBorderStart = 0;	// First border to draw
	int tileBorderLimit = 0;	// Last + 1 border to draw
	if (ascending) {
		// Prefer tile borders towards negative infinity
		// 0 -> 0
		// 1..tileSize -> 1
		// (tileSize+1)..(2*tileSize) -> 2
		// etc.
		tileBorderStart = (start + tileSize - 1) / tileSize;
		tileBorderLimit = (limit + tileSize - 1) / tileSize;
	} else {
		// Prefer tile borders towards positive infinity
		// 0..(tileSize-1) -> 1
		// tileSize..(2*tileSize-1) -> 2
		// etc.
		tileBorderStart = start / tileSize + 1;
		tileBorderLimit = limit / tileSize + 1;
	}
	tileMapStartOffset = (tileSize - start) % tileSize;
	tileMapEndOffset = limit - ((tileBorderLimit-tileBorderStart) * tileSize);
	tileBorderCount = tileBorderLimit - tileBorderStart;
}

void TileGenerator::computeMapParameters(const std::string &input)
{
	if (!m_chunkSize && (m_tileWidth == TILESIZE_CHUNK || m_tileHeight == TILESIZE_CHUNK))
		m_chunkSize = getMapChunkSize(input);
	if (m_tileWidth == TILESIZE_CHUNK)
		m_tileWidth = m_chunkSize * BLOCK_SIZE;
	if (m_tileHeight == TILESIZE_CHUNK)
		m_tileHeight = m_chunkSize * BLOCK_SIZE;

	m_storedWidth = (m_xMax - m_xMin + 1) * 16;
	m_storedHeight = (m_zMax - m_zMin + 1) * 16;
	int mapWidth = m_storedWidth - m_mapXStartNodeOffset + m_mapXEndNodeOffset;
	int mapHeight = m_storedHeight - m_mapYStartNodeOffset + m_mapYEndNodeOffset;
#ifdef DEBUG
	assert(mapWidth % m_scaleFactor == 0);
	assert(mapHeight % m_scaleFactor == 0);
#else
	mapWidth += mapWidth % m_scaleFactor;
	mapHeight += mapHeight % m_scaleFactor;
#endif
	m_blockPixelAttributes.setParameters(m_storedWidth, 16, m_mapYStartNodeOffset, 1, true);
	m_blockPixelAttributesScaled.setParameters(m_storedWidth / m_scaleFactor, 16 / m_scaleFactor, m_mapYStartNodeOffset / m_scaleFactor, m_scaleFactor, false);

	// Set special values for origin (which depend on other paramters)
	if (m_tileWidth) {
		switch (m_tileXOrigin) {
		case TILECENTER_AT_WORLDCENTER:
			m_tileXOrigin = -m_tileWidth / 2;
			break;
		case TILECORNER_AT_WORLDCENTER:
			m_tileXOrigin = 0;
			break;
		case TILECENTER_AT_CHUNKCENTER:
			m_tileXOrigin = ((m_chunkSize%2) ? BLOCK_SIZE / 2 : 0) - m_tileWidth / 2;
			break;
		case TILECENTER_AT_MAPCENTER:
			m_tileXOrigin = m_xMin * 16 + m_mapXStartNodeOffset + mapWidth / 2 - m_tileWidth / 2;
			break;
		case TILECORNER_AT_MAPCENTER:
			m_tileXOrigin = m_xMin * 16 + m_mapXStartNodeOffset + mapWidth / 2;
			break;
		default:
			if (m_tileXCentered)
				m_tileXOrigin -= m_tileWidth/2;
			break;
		}
		if (m_tileXOrigin >= 0)
			m_tileXOrigin -= m_tileXOrigin % m_scaleFactor;
		else
			m_tileXOrigin -= -m_tileXOrigin % m_scaleFactor;
	}
	if (m_tileHeight) {
		switch (m_tileZOrigin) {
		case TILECENTER_AT_WORLDCENTER:
			m_tileZOrigin = -m_tileHeight / 2;
			break;
		case TILECORNER_AT_WORLDCENTER:
			m_tileZOrigin = 0;
			break;
		case TILECENTER_AT_CHUNKCENTER:
			m_tileZOrigin = ((m_chunkSize%2) ? BLOCK_SIZE / 2 : 0) - m_tileHeight / 2;
			break;
		case TILECENTER_AT_MAPCENTER:
			m_tileZOrigin = (m_zMax + 1) * 16 - 1 - m_mapYStartNodeOffset - mapHeight / 2 - m_tileHeight / 2;
			break;
		case TILECORNER_AT_MAPCENTER:
			m_tileZOrigin = (m_zMax + 1) * 16 - 1 - m_mapYStartNodeOffset - mapHeight / 2;
			break;
		default:
			if (m_tileYCentered)
				m_tileZOrigin -= m_tileHeight / 2;
			break;
		}
		if (m_tileXOrigin >= 0)
			m_tileZOrigin -= m_tileZOrigin % m_scaleFactor;
		else
			m_tileZOrigin -= -m_tileZOrigin % m_scaleFactor;
	}

	// Compute adjustments for tiles.
	m_pictWidth = mapWidth;
	m_pictHeight = mapHeight;
	if (m_tileWidth && m_tileBorderSize) {
		int tileMapXEndOffset;		// Dummy
		TileGenerator::computeTileParameters(
				// Input parameters
				m_xMin,
				m_xMax,
				m_mapXStartNodeOffset,
				m_mapXEndNodeOffset,
				m_tileXOrigin,
				m_tileWidth,
				// Output parameters
				m_tileBorderXCount,
				m_tileMapXOffset,
				tileMapXEndOffset,
				// Behavior selection
				true);
	}
	if (m_tileHeight && m_tileBorderSize) {
		int tileMapYEndOffset;		// Dummy
		TileGenerator::computeTileParameters(
				// Input parameters
				m_zMin,
				m_zMax,
				-m_mapYEndNodeOffset,
				-m_mapYStartNodeOffset,
				m_tileZOrigin,
				m_tileHeight,
				// Output parameters
				m_tileBorderYCount,
				tileMapYEndOffset,
				m_tileMapYOffset,
				// Behavior selection
				false);
	}

	m_pictWidth /= m_scaleFactor;
	m_pictWidth += m_tileBorderXCount * m_tileBorderSize;
	m_pictHeight /= m_scaleFactor;
	m_pictHeight += m_tileBorderYCount * m_tileBorderSize;

	// Print some useful messages in cases where it may not be possible to generate the image...
	long long pixels = static_cast<long long>(m_pictWidth + borderLeft() + borderRight()) * (m_pictHeight + borderTop() + borderBottom());
	// Study the libgd code to known why the maximum is the following:
	long long max_pixels = INT_MAX - INT_MAX % m_pictHeight;
	if (pixels > max_pixels) {
		cerr << "WARNING: Image will have " << pixels << " pixels; the PNG graphics library will refuse to handle more than approximately " << INT_MAX << std::endl;
		cerr << "         (If map generation fails, consider using --scalefactor to reduce the image size by a factor 2)" << std::endl;
	}
	// Estimated approximate maximum was determined by trial and error...
	// (24100x24100 succeeded; 24200x24200 failed)
	#define ESTIMATED_MAX_PIXELS_32BIT (24100*24100L)
	else if (sizeof(void *) == 4 && pixels > ESTIMATED_MAX_PIXELS_32BIT) {
		cerr << "WARNING: Image will have " << pixels << " pixels; The maximum achievable on a 32-bit system is approximately " << ESTIMATED_MAX_PIXELS_32BIT << std::endl;
		cerr << "         (If map generation fails, consider using --scalefactor to reduce the image size by a factor 2 or 4)" << std::endl;
	}
	#undef ESTIMATED_MAX_PIXELS_32BIT
}


void TileGenerator::createImage()
{
	int totalPictHeight = m_pictHeight + borderTop() + borderBottom();
	int totalPictWidth = m_pictWidth + borderLeft() + borderRight();
	m_image = gdImageCreateTrueColor(totalPictWidth, totalPictHeight);
	if (!m_image) {
		ostringstream oss;
		oss << "Failed to allocate " << totalPictWidth << "x" << totalPictHeight << " image";
		throw std::runtime_error(oss.str());
	}
	// Background
	gdImageFilledRectangle(m_image, 0, 0, totalPictWidth - 1, totalPictHeight -1, m_bgColor.to_libgd());

	// Draw tile borders
	if (m_tileWidth && m_tileBorderSize) {
		int borderColor = m_tileBorderColor.to_libgd();
		for (int i = 0; i < m_tileBorderXCount; i++) {
			int xPos = m_tileMapXOffset / m_scaleFactor + i * (m_tileWidth / m_scaleFactor + m_tileBorderSize);
#ifdef DEBUG
			int xPos2 = mapX2ImageX(m_tileMapXOffset / m_scaleFactor + i * m_tileWidth / m_scaleFactor) - borderLeft() - m_tileBorderSize;
			assert(xPos == xPos2);
#endif
			gdImageFilledRectangle(m_image, xPos + borderLeft(), borderTop(), xPos + (m_tileBorderSize-1) + borderLeft(), m_pictHeight + borderTop() - 1, borderColor);
		}
	}
	if (m_tileHeight && m_tileBorderSize) {
		int borderColor = m_tileBorderColor.to_libgd();
		for (int i = 0; i < m_tileBorderYCount; i++) {
			int yPos = m_tileMapYOffset / m_scaleFactor + i * (m_tileHeight / m_scaleFactor + m_tileBorderSize);
#ifdef DEBUG
			int yPos2 = mapY2ImageY(m_tileMapYOffset / m_scaleFactor + i * m_tileHeight / m_scaleFactor) - borderTop() - m_tileBorderSize;
			assert(yPos == yPos2);
#endif
			gdImageFilledRectangle(m_image, borderLeft(), yPos + borderTop(), m_pictWidth + borderLeft() - 1, yPos + (m_tileBorderSize-1) + borderTop(), borderColor);
		}
	}
}

void TileGenerator::processMapBlock(const DB::Block &block)
{
	const BlockPos &pos = block.first;
	const unsigned char *data = block.second.c_str();
	size_t length = block.second.length();

	uint8_t version = readU8(data, 0, length);
	//uint8_t flags = readU8(data, 1, length);

	size_t dataOffset = 0;
	if (version >= 27) {
		dataOffset = 6;
	}
	else if (version >= 22) {
		dataOffset = 4;
	}
	else {
		dataOffset = 2;
	}

	// Zlib header: 2; Deflate header: >=1
	checkDataLimit("zlib", dataOffset, 3, length);
	ZlibDecompressor decompressor(data, length);
	decompressor.setSeekPos(dataOffset);
	ustring mapData = decompressor.decompress();
	ustring mapMetadata = decompressor.decompress();
	dataOffset = decompressor.seekPos();

	// Skip unused data
	if (version <= 21) {
		dataOffset += 2;
	}
	if (version == 23) {
		dataOffset += 1;
	}
	if (version == 24) {
		uint8_t ver = readU8(data, dataOffset++, length);
		if (ver == 1) {
			uint16_t num = readU16(data, dataOffset, length);
			dataOffset += 2;
			dataOffset += 10 * num;
		}
	}

	// Skip unused static objects
	dataOffset++; // Skip static object version
	int staticObjectCount = readU16(data, dataOffset, length);
	dataOffset += 2;
	for (int i = 0; i < staticObjectCount; ++i) {
		dataOffset += 13;
		uint16_t dataSize = readU16(data, dataOffset, length);
		dataOffset += dataSize + 2;
	}
	dataOffset += 4; // Skip timestamp

	// Read mapping
	if (version >= 22) {
		dataOffset++; // mapping version
		uint16_t numMappings = readU16(data, dataOffset, length);
		dataOffset += 2;
		for (int i = 0; i < numMappings; ++i) {
			uint16_t nodeId = readU16(data, dataOffset, length);
			dataOffset += 2;
			uint16_t nameLen = readU16(data, dataOffset, length);
			dataOffset += 2;
			string name;
			readString(name, data, dataOffset, nameLen, length);
			size_t end = name.find_first_of('\0');
			if (end != std::string::npos)
				name.erase(end);
			// In case of a height map, it stores just dummy colors...
			NodeColorMap::const_iterator color = m_nodeColors.find(name);
			if (name == "air" && !(m_drawAir && color != m_nodeColors.end())) {
				m_nodeIDColor[nodeId] = NodeColorNotDrawn;
			}
			else if (name == "ignore" && !(m_drawIgnore && color != m_nodeColors.end())) {
				m_nodeIDColor[nodeId] = NodeColorNotDrawn;
			}
			else {
				if (color != m_nodeColors.end()) {
					// If the color is marked 'ignore', then treat it accordingly.
					// Colors marked 'ignore' take precedence over 'air'
					if ((color->second.f & ColorEntry::FlagIgnore)) {
						if (m_drawIgnore)
							m_nodeIDColor[nodeId] = &color->second;
						else
							m_nodeIDColor[nodeId] = NodeColorNotDrawn;
					}
					// If the color is marked 'air', then treat it accordingly.
					else if ((color->second.f & ColorEntry::FlagAir)) {
						if (m_drawAir)
							m_nodeIDColor[nodeId] = &color->second;
						else
							m_nodeIDColor[nodeId] = NodeColorNotDrawn;
					}
					// Regular node.
					else {
						m_nodeIDColor[nodeId] = &color->second;
					}
				}
				else {
					m_nameMap[nodeId] = name;
					m_nodeIDColor[nodeId] = NULL;
				}
			}
			dataOffset += nameLen;
		}
	}

	// Node timers
	if (version >= 25) {
		dataOffset++;
		uint16_t numTimers = readU16(data, dataOffset, length);
		dataOffset += 2;
		dataOffset += numTimers * 10;
	}

	renderMapBlock(mapData, pos, version);
}

class MapBlockIterator
{
public:
	virtual ~MapBlockIterator(void) {}
	virtual MapBlockIterator &operator++(void) = 0;
	virtual BlockPos &operator*(void) = 0;
	virtual MapBlockIterator &operator=(const MapBlockIterator &i) = 0;
	virtual bool operator==(const MapBlockIterator &i) const = 0;
	bool operator!=(const MapBlockIterator &i) const { return !operator==(i); }
	virtual void breakDim(int i) { (void) i; }
};

class MapBlockIteratorBlockList : public MapBlockIterator
{
public:
	MapBlockIteratorBlockList(void) {}
	MapBlockIteratorBlockList(const std::list<BlockPos>::iterator &i) : m_iter(i) {}
	MapBlockIterator &operator++(void) override { m_iter++; return *this; }
	BlockPos &operator*(void) override { return *m_iter; }
	MapBlockIterator &operator=(const MapBlockIterator &i) override
		{ const MapBlockIteratorBlockList &i2 = dynamic_cast<const MapBlockIteratorBlockList &>(i); m_iter = i2.m_iter; return *this; }
	bool operator==(const MapBlockIterator &i) const override
		{ const MapBlockIteratorBlockList &i2 = dynamic_cast<const MapBlockIteratorBlockList &>(i); return m_iter == i2.m_iter; }
	// breakDim() might be implemented, but is not strictly necessary
private:
	std::list<BlockPos>::iterator m_iter;
};

class MapBlockIteratorBlockPos : public MapBlockIterator
{
public:
	MapBlockIteratorBlockPos(void) {}
	MapBlockIteratorBlockPos(const BlockPosIterator &i) : m_iter(i) {}
	MapBlockIterator &operator++(void) override { m_iter++; return *this; }
	BlockPos &operator*(void) override { return *m_iter; }
	MapBlockIterator &operator=(const MapBlockIterator &i) override
		{ const MapBlockIteratorBlockPos &i2 = dynamic_cast<const MapBlockIteratorBlockPos &>(i); m_iter = i2.m_iter; return *this; }
	bool operator==(const MapBlockIterator &i) const override
		{ const MapBlockIteratorBlockPos &i2 = dynamic_cast<const MapBlockIteratorBlockPos &>(i); return m_iter == i2.m_iter; }
	void breakDim(int i) override { m_iter.breakDim(i); }
private:
	BlockPosIterator m_iter;
};


void TileGenerator::renderMap()
{
	int unpackErrors = 0;
	long long blocks_rendered = 0;
	int area_rendered = 0;
	BlockPos currentPos;
	currentPos.x() = INT_MIN;
	currentPos.y() = INT_MAX;
	currentPos.z() = INT_MIN;
	bool allReaded = false;
	MapBlockIterator *position;
	MapBlockIterator *begin;
	MapBlockIterator *end;
	if (m_generateNoPrefetch) {
		position = new MapBlockIteratorBlockPos();
		begin = new MapBlockIteratorBlockPos(BlockPosIterator(
				BlockPos(m_xMin, m_yMax, m_zMax, m_databaseFormat),
				BlockPos(m_xMax, m_yMin, m_zMin, m_databaseFormat)));
		end = new MapBlockIteratorBlockPos(BlockPosIterator(
				BlockPos(m_xMin, m_yMax, m_zMax, m_databaseFormat),
				BlockPos(m_xMax, m_yMin, m_zMin, m_databaseFormat),
				BlockPosIterator::End));
	}
	else {
		position = new MapBlockIteratorBlockList(std::list<BlockPos>::iterator());
		begin = new MapBlockIteratorBlockList(m_positions.begin());
		end = new MapBlockIteratorBlockList(m_positions.end());
	}
	std::cout << std::flush;
	std::cerr << std::flush;
	for (*position = *begin; *position != *end; ++*position) {
		const BlockPos &pos = **position;
		if (currentPos.x() != pos.x() || currentPos.z() != pos.z()) {
			area_rendered++;
			if (currentPos.y() == m_yMin)
				m_emptyMapArea++;
			if (currentPos.z() != pos.z()) {
				if (m_scaleFactor > 1) {
					scalePixelRows(m_blockPixelAttributes, m_blockPixelAttributesScaled, pos.z());
					pushPixelRows(m_blockPixelAttributesScaled, pos.z());
					m_blockPixelAttributesScaled.setLastY(((m_zMax - pos.z()) * 16 + 15) / m_scaleFactor);
				}
				else {
					pushPixelRows(m_blockPixelAttributes, pos.z());
				}
				m_blockPixelAttributes.setLastY((m_zMax - pos.z()) * 16 + 15);
				if (progressIndicator)
				    cout << "Processing Z-coordinate: " << std::setw(6) << pos.z()*16
					<< "  (" << std::fixed << std::setprecision(0) << 100.0 * (m_zMax - pos.z()) / (m_zMax - m_zMin)
					<< "%)          \r" << std::flush;
			}
			for (int i = 0; i < 16; ++i) {
				m_readedPixels[i] = 0;
			}
			allReaded = false;
			currentPos = pos;
		}
		else if (allReaded) {
			position->breakDim(1);
			continue;
		}
		currentPos.y() = pos.y();
		DB::Block block = m_db->getBlockOnPos(pos);
		if (!block.second.empty()) {
			try {
				processMapBlock(block);

				blocks_rendered++;

				allReaded = true;
				for (int i = 0; i < 16; ++i) {
					if (m_readedPixels[i] != 0xffff) {
						allReaded = false;
					}
				}
			}
			catch (UnpackError &e) {
				std::cerr << "Failed to unpack map block " << pos.x() << "," << pos.y() << "," << pos.z()
					<< " (id: " << pos.databasePosStr(BlockPos::I64) << "). Block corrupt ?"
					<< std::endl
					<< "\tCoordinates: " << pos.x()*16 << "," << pos.y()*16 << "," << pos.z()*16 << "+16+16+16"
					<< ";  Data: " << e.type << " at: " << e.offset << "(+" << e.length <<  ")/" << e.dataLength
					<< std::endl;
				unpackErrors++;
			}
			catch (ZlibDecompressor::DecompressError &e) {
				std::cerr << "Failed to decompress data in map block " << pos.x() << "," << pos.y() << "," << pos.z()
					<< " (id: " << pos.databasePosStr(BlockPos::I64) << "). Block corrupt ?"
					<< std::endl
					<< "\tCoordinates: " << pos.x()*16 << "," << pos.y()*16 << "," << pos.z()*16 << "+16+16+16"
					<< ";  Cause: " << e.message
					<< std::endl;
				unpackErrors++;
			}
		}
		if (unpackErrors >= 100) {
			throw(std::runtime_error("Too many block unpacking errors - bailing out"));
		}
	}
	delete position;
	delete begin;
	delete end;
	if (currentPos.z() != INT_MIN) {
		if (currentPos.y() == m_yMin)
			m_emptyMapArea++;
		if (m_scaleFactor > 1) {
			scalePixelRows(m_blockPixelAttributes, m_blockPixelAttributesScaled, currentPos.z() - 1);
			pushPixelRows(m_blockPixelAttributesScaled, currentPos.z() - 1);
		}
		else {
			pushPixelRows(m_blockPixelAttributes, currentPos.z() - 1);
		}
	}
	bool eraseProgress = true;
	if (verboseCoordinates >= 1) {
		eraseProgress = false;
		if (m_YMinMapped <= m_YMaxMapped) {
			cout
				<< std::setw(MESSAGE_WIDTH) << std::left
				<< "Mapped Vertical Range:" << std::right
				<< std::setw(7) << "x" << ","
				<< std::setw(7) << m_YMinMapped*16 << ","
				<< std::setw(7) << "z"
				<< "  ..  "
				<< std::setw(7) << "x" << ","
				<< std::setw(7) << m_YMaxMapped*16+15 << ","
				<< std::setw(7) << "z"
				<< "    ("
				<< std::setw(6) << "x" << ","
				<< std::setw(6) << m_YMinMapped << ","
				<< std::setw(6) << "z"
				<< "  ..  "
				<< std::setw(6) << "x" << ","
				<< std::setw(6) << m_YMaxMapped << ","
				<< std::setw(6) << "z"
				<< ")\n";
		}
		else {
			cout
				<< std::setw(MESSAGE_WIDTH) << std::left
				<< "Mapped Vertical Range:" << std::right
				<< std::setw(7) << "x" << ","
				<< std::setw(7) << "-" << ","
				<< std::setw(7) << "z"
				<< "  ..  "
				<< std::setw(7) << "x" << ","
				<< std::setw(7) << "-" << ","
				<< std::setw(7) << "z"
				<< "    ("
				<< std::setw(6) << "x" << ","
				<< std::setw(6) << "-" << ","
				<< std::setw(6) << "z"
				<< "  ..  "
				<< std::setw(6) << "x" << ","
				<< std::setw(6) << "-" << ","
				<< std::setw(6) << "z"
				<< ")\n";
		}
	}
	if (!m_generateNoPrefetch && m_backend == "leveldb" && (m_reportDatabaseFormat || verboseStatistics >= 1)) {
		cout
			<< "Database format setting when using --disable-blocklist-prefetch: ";
		if (m_recommendedDatabaseFormat != "")
			cout << m_recommendedDatabaseFormat;
		else
			cout << "unknown - use 'mixed' to be safe";
		cout << std::endl;
	}
	if (verboseStatistics >= 1) {
		eraseProgress = false;
		cout << "Statistics"
		     << ":  blocks read/queried: " << m_db->getBlocksReadCount()
		     << " / " << m_db->getBlocksQueriedCount()
		     << ";  blocks rendered: " << blocks_rendered
		     << ";  area rendered: " << area_rendered
		     << "/" << (m_xMax-m_xMin+1) * (m_zMax-m_zMin+1)
		     << "  (" << (long long)area_rendered*16*16 << " nodes)";
		if (unpackErrors)
			cout << "  (" << unpackErrors << " errors)";
		 cout << std::endl;
	}
	if (progressIndicator && eraseProgress)
		cout << std::setw(50) << "" << "\r";

	if (m_generateNoPrefetch) {
		double queryFactor = 1.0 * m_db->getBlocksQueriedCount() / m_db->getBlocksReadCount();
		if (verboseStatistics >= 4) {
			std::cout << std::fixed << std::setprecision(2);
			std::cout << "disable-blocklist-prefetch statistics:" << std::endl
				<< "    Query factor:   " << queryFactor << std::endl;
		}
		if (!(m_silenceSuggestions & SUGGESTION_PREFETCH) && queryFactor >= 10) {
			std::cout << std::fixed << std::setprecision(2);
			std::cout << "NOTE: amount of database blocks queried exceeds amount read by a factor " << queryFactor << "." << std::endl
				<< "      This makes --disable-blocklist-prefetch rather inefficient. Consider disabling it, or" << std::endl
				<< "      adjusting the vertical limits (e.g. --min-y=" << m_YMinMapped * 16 << " --max-y=" << m_YMaxMapped*16+15 << ")" << std::endl;
		}
	}
	else {
		double blocksFractionMapped = 1.0 * m_db->getBlocksReadCount() / m_worldBlocks;
		long long worldVolumeMapped = (m_xMax-m_xMin+1) * (m_zMax-m_zMin+1) * (m_YMaxMapped-m_YMinMapped+1);

		if (verboseStatistics >= 4) {
			std::cout << std::fixed << std::setprecision(2);
			std::cout << "disable-blocklist-prefetch statistics:" << std::endl
				<< "    World size (1M Blocks):          " << m_worldBlocks / 1000.0 / 1000
					<< "  (" << MIN_NOPREFETCH_VOLUME / 1000.0 / 1000 << " .. "
					<< MAX_NOPREFETCH_VOLUME / 1000.0 / 1000 << ")" << std::endl
				<< "    Fraction of world blocks mapped: "
					<< 100 * blocksFractionMapped << "%  (limit: 10%)" << std::endl
				<< "    Volume mapped:                   "
					<< worldVolumeMapped / 10000.0 << "%  (" << worldVolumeMapped << ")" << std::endl;
		}

		if (!(m_silenceSuggestions & SUGGESTION_PREFETCH)
			&& m_worldBlocks >= MIN_NOPREFETCH_VOLUME
			&& blocksFractionMapped < 0.1
			&& worldVolumeMapped < MAX_NOPREFETCH_VOLUME) {
			std::cout << "NOTE: Mapping speed may improve using the option --disable-blocklist-prefetch,"
				<< " combined with vertical limits (" << m_YMinMapped * 16 << " .. " << m_YMaxMapped*16+15 << ")" << std::endl;
			if (m_backend == "leveldb") {
				std::cout << "      The option --database-format=" << (m_recommendedDatabaseFormat != "" ? m_recommendedDatabaseFormat : "mixed")
					<< " is also required for the (current) leveldb backend." << std::endl
					<< "      Use --verbose=2 or --database-format=query for details" << std::endl;
			}
		}
	}
}

Color TileGenerator::computeMapHeightColor(int height)
{
	int adjustedHeight = int((height - m_seaLevel) * m_heightMapYScale + 0.5);
	float r = 0;
	float g = 0;
	float b = 0;
	int n = 0;
	for (HeightMapColorList::iterator i = m_heightMapColors.begin(); i != m_heightMapColors.end(); i++) {
		HeightMapColor &colorSpec = *i;
		if (adjustedHeight >= colorSpec.height[0] && adjustedHeight <= colorSpec.height[1]) {
			float weight = (float) (colorSpec.height[1] - adjustedHeight + 1) / (colorSpec.height[1] - colorSpec.height[0] + 1);
			for (int j = 0; j < 2; j++) {
				r += colorSpec.color[j].r * weight;
				g += colorSpec.color[j].g * weight;
				b += colorSpec.color[j].b * weight;
				weight = 1 - weight;
			}
			n++;
		}
	}
	return Color(int(r / n + 0.5), int(g / n + 0.5), int(b / n + 0.5));
}

inline void TileGenerator::renderMapBlock(const ustring &mapBlock, const BlockPos &pos, int version)
{
	checkBlockNodeDataLimit(version, mapBlock.length());
	int xBegin = worldBlockX2StoredX(pos.x());
	int zBegin = worldBlockZ2StoredY(pos.z());
	const unsigned char *mapData = mapBlock.c_str();
	int minY = (pos.y() < m_reqYMin) ? 16 : (pos.y() > m_reqYMin) ?  0 : m_reqYMinNode;
	int maxY = (pos.y() > m_reqYMax) ? -1 : (pos.y() < m_reqYMax) ? 15 : m_reqYMaxNode;
	bool renderedAnything = false;
	for (int z = 0; z < 16; ++z) {
		bool rowIsEmpty = true;
		for (int x = 0; x < 16; ++x) {
			if (m_readedPixels[z] & (1 << x)) {
				continue;
			}
			// The #define of pixel performs *significantly* *better* than the definition of PixelAttribute &pixel ...
			#define pixel m_blockPixelAttributes.attribute(zBegin + 15 - z,xBegin + x)
			//PixelAttribute &pixel = m_blockPixelAttributes.attribute(zBegin + 15 - z,xBegin + x);
			if (m_blockDefaultColor.to_uint() && !pixel.color().to_uint()) {
				rowIsEmpty = false;
				pixel = PixelAttribute(m_blockDefaultColor, NAN);
			}
			for (int y = maxY; y >= minY; --y) {
				int position = x + (y << 4) + (z << 8);
				int content = readBlockContent(mapData, version, position);
				#define nodeColor (*m_nodeIDColor[content])
				//const ColorEntry &nodeColor = *m_nodeIDColor[content];
				if (m_nodeIDColor[content] == NodeColorNotDrawn) {
					continue;
				}
				int height = pos.y() * 16 + y;
				if (m_heightMap) {
					if (m_nodeIDColor[content] && nodeColor.a != 0) {
						if (!(m_readedPixels[z] & (1 << x))) {
							if (height > m_surfaceHeight) m_surfaceHeight = height;
							if (height < m_surfaceDepth) m_surfaceDepth = height;
						}
						rowIsEmpty = false;
						renderedAnything = true;
						pixel = PixelAttribute(computeMapHeightColor(height), height);
						m_readedPixels[z] |= (1 << x);
						break;
					}
				}
				else if (m_nodeIDColor[content]) {
					rowIsEmpty = false;
					renderedAnything = true;
					pixel.mixUnder(PixelAttribute(nodeColor, height));
					if ((m_drawAlpha && nodeColor.a == 0xff) || (!m_drawAlpha && nodeColor.a != 0)) {
						m_readedPixels[z] |= (1 << x);
						break;
					}
				} else {
					NodeID2NameMap::iterator blockName = m_nameMap.find(content);
					if (blockName != m_nameMap.end())
						m_unknownNodes.insert(blockName->second);
				}
				#undef nodeColor
			}
			#undef pixel
		}
		if (!rowIsEmpty)
			m_blockPixelAttributes.attribute(zBegin + 15 - z,xBegin).nextEmpty = false;
	}
	if (renderedAnything) {
		if (pos.y() < m_YMinMapped)
			m_YMinMapped = pos.y();
		if (pos.y() > m_YMaxMapped)
			m_YMaxMapped = pos.y();
	}
}

void TileGenerator::renderScale()
{
	int color = m_scaleColor.to_libgd();
	if ((m_drawScale & DRAWSCALE_LEFT) && (m_drawScale & DRAWSCALE_TOP)) {
		gdImageString(m_image, gdFontGetMediumBold(), borderLeft() - 26, 0, reinterpret_cast<unsigned char *>(const_cast<char *>("X")), color);
		gdImageString(m_image, gdFontGetMediumBold(), 2, borderTop() - 26, reinterpret_cast<unsigned char *>(const_cast<char *>("Z")), color);
	}
	int major = m_sideScaleMajor ? m_sideScaleMajor : 4 * 16 * m_scaleFactor;
	int minor = m_sideScaleMinor;

	string scaleText;

	if ((m_drawScale & DRAWSCALE_TOP)) {
		int start;
		int extra_left = borderLeft() ? 0 : major;
		int extra_right = borderRight() ? 0 : major;
		if (m_xMin >= 0)
			start = (m_xMin * 16 + m_mapXStartNodeOffset - 1 + major - 1 - extra_left) / major * major;
		else
			start = (m_xMin * 16 + m_mapXStartNodeOffset - 1 - extra_left) / major * major;
		for (int i = start; i <= (m_xMax + 1) * 16 + m_mapXEndNodeOffset + extra_right; i += major) {
			stringstream buf;
			buf << i;
			int xPos = worldX2ImageX(i);

			scaleText = buf.str();
			gdImageString(m_image, gdFontGetMediumBold(), xPos + 2, 0, reinterpret_cast<unsigned char *>(const_cast<char *>(scaleText.c_str())), color);
			if ((major % 16) == 0) {
				buf.str("");
				buf << "(" << i / 16 << ")";
				scaleText = buf.str();
				gdImageString(m_image, gdFontGetTiny(), xPos + 2, 16, reinterpret_cast<unsigned char *>(const_cast<char *>(scaleText.c_str())), color);
			}
			gdImageLine(m_image, xPos, 0, xPos, borderTop() - 1, color);
		}
		if (minor) {
			if (m_xMin >= 0)
				start = (m_xMin * 16 + m_mapXStartNodeOffset + minor - 2) / minor * minor;
			else
				start = (m_xMin * 16 + m_mapXStartNodeOffset - 1) / minor * minor;
			for (int i = start; i <= (m_xMax + 1) * 16 + m_mapXEndNodeOffset; i += minor) {
				int xPos = worldX2ImageX(i);
				gdImageLine(m_image, xPos, borderTop() - 5, xPos, borderTop() - 1, color);
			}
		}
	}

	if ((m_drawScale & DRAWSCALE_LEFT)) {
		int start;
		int extra_top = borderTop() ? 0 : major;
		int extra_bottom = borderBottom() ? 0 : major;
		if (m_zMax >= 0)
			start = ((m_zMax + 1) * 16 - m_mapYStartNodeOffset + extra_top) / major * major;
		else
			start = ((m_zMax + 1) * 16 - m_mapYStartNodeOffset - major + 1 + extra_top) / major * major;
		for (int i = start; i >= m_zMin * 16 - m_mapYEndNodeOffset - 1 - extra_bottom; i -= major) {
			stringstream buf;
			buf << i;
			int yPos = worldZ2ImageY(i);

			scaleText = buf.str();
			gdImageString(m_image, gdFontGetMediumBold(), 2, yPos, reinterpret_cast<unsigned char *>(const_cast<char *>(scaleText.c_str())), color);
			if ((major % 16) == 0) {
				buf.str("");
				buf << "(" << i / 16 << ")";
				scaleText = buf.str();
				gdImageString(m_image, gdFontGetTiny(), 2, yPos-10, reinterpret_cast<unsigned char *>(const_cast<char *>(scaleText.c_str())), color);
			}
			gdImageLine(m_image, 0, yPos, borderLeft() - 1, yPos, color);
		}
		if (minor) {
			if (m_zMax >= 0)
				start = ((m_zMax + 1) * 16 - m_mapYStartNodeOffset) / minor * minor;
			else
				start = ((m_zMax + 1) * 16 - m_mapYStartNodeOffset - minor + 1) / minor * minor;
			for (int i = start; i >= m_zMin * 16 - m_mapYEndNodeOffset - 1; i -= minor) {
				int yPos = worldZ2ImageY(i);
				gdImageLine(m_image, borderLeft() - 5, yPos, borderLeft() - 1, yPos, color);
			}
		}
	}

	// DRAWSCALE_RIGHT and DRAWSCALE_BOTTOM not implemented - getting the text positioned right seems not trivial (??)
}

void TileGenerator::renderHeightScale()
{

	int scaleColor = m_scaleColor.to_libgd();
	int height_min = m_surfaceDepth - 16;
	int height_limit = m_surfaceHeight + 16;
	int xBorderOffset = borderLeft();
	int yBorderOffset = borderTop() + m_pictHeight;
	double height_step = (double)(height_limit - height_min) / m_pictWidth;
	if (height_step < 1.0 / 16) {
		height_step = 1.0 / 16;
	}
	double major;
	int minor = m_heightScaleMinor;
	if (m_heightScaleMajor) {
		major = m_heightScaleMajor;
	}
	else {
		major = 64;
		while (major / height_step / 64 < 0.75)
			major *= 2;
		while (major / height_step / 64 > 1.5)
			major /= 2;
	}

	double height = height_min;
	for (int x = 0; height < height_limit; x++, height += height_step) {
		Color color = computeMapHeightColor(int(height + 0.5));
		gdImageLine(m_image, xBorderOffset + x, yBorderOffset + 8, xBorderOffset + x, yBorderOffset + borderBottom() - 20, color.to_libgd());

		int iheight = static_cast<int>(height + (height > 0 ? 0.5 : -0.5));
		int iheightMaj = static_cast<int>(trunc(iheight / major + (height > 0 ? 0.5 : -0.5)) * major);
		if (fabs(height - iheightMaj) <= height_step / 2 && (height - iheightMaj) > -height_step / 2) {
			if (iheightMaj / int(major) % 2 == 1 && fabs(height) > 9999 && major / height_step < 56) {
				// Maybe not enough room for the number. Draw a tick mark instead
				gdImageLine(m_image, xBorderOffset + x, yBorderOffset + borderBottom() - 19, xBorderOffset + x, yBorderOffset + borderBottom() - 16, scaleColor);
			}
			else {
				stringstream buf;
				buf << iheightMaj;
				string scaleText = buf.str();
				gdImageString(m_image, gdFontGetMediumBold(), xBorderOffset + x + 2, yBorderOffset + borderBottom() - 16,
					reinterpret_cast<unsigned char *>(const_cast<char *>(scaleText.c_str())), scaleColor);
				gdImageLine(m_image, xBorderOffset + x, yBorderOffset + borderBottom() - 19, xBorderOffset + x, yBorderOffset + borderBottom() - 1, scaleColor);
			}
		}
		if (minor) {
			int iheightMin = int(iheight / minor + (height > 0 ? 0.5 : -0.5)) * minor;
			if (fabs(height - iheightMin) <= height_step / 2 && (height - iheightMin) > -height_step / 2) {
				gdImageLine(m_image, xBorderOffset + x, yBorderOffset + borderBottom() - 19, xBorderOffset + x, yBorderOffset + borderBottom() - 16, scaleColor);
			}
		}
	}
}

void TileGenerator::renderOrigin()
{
	int imageX = worldX2ImageX(0);
	int imageY = worldZ2ImageY(0);
	gdImageArc(m_image, imageX, imageY, 12, 12, 0, 360, m_originColor.to_libgd());
}

void TileGenerator::renderPlayers(const std::string &inputPath)
{
	int color = m_playerColor.to_libgd();

	PlayerAttributes players(inputPath);
	for (PlayerAttributes::Players::iterator player = players.begin(); player != players.end(); ++player) {
		int imageX = worldX2ImageX(static_cast<int>(player->x / 10));
		int imageY = worldZ2ImageY(static_cast<int>(player->z / 10));
		std::string displayName = m_gdStringConv->convert(player->name);

		gdImageArc(m_image, imageX, imageY, 5, 5, 0, 360, color);
		gdImageString(m_image, gdFontGetMediumBold(), imageX + 2, imageY + 2, reinterpret_cast<unsigned char *>(const_cast<char *>(displayName.c_str())), color);
	}
}

void TileGenerator::renderDrawObjects(void)
{
	for (std::vector<DrawObject>::iterator o = m_drawObjects.begin(); o != m_drawObjects.end(); o++) {
		// Hack to adjust the center of an ellipse with even dimensions to align it correctly
		bool ellipseAdjustCenter[2] = { 0, 1 };
#ifdef DEBUG
		assert(o->type != DrawObject::Unknown);
		assert(o->haveDimensions || !o->haveCenter);
		// Look for problems...
		if (o->haveCenter)
			o->corner1 = NodeCoord(NodeCoord::Invalid, NodeCoord::Invalid, NodeCoord::Invalid);
		else
			o->center = NodeCoord(NodeCoord::Invalid, NodeCoord::Invalid, NodeCoord::Invalid);
		if (o->haveDimensions)
			o->corner2 = NodeCoord(NodeCoord::Invalid, NodeCoord::Invalid, NodeCoord::Invalid);
		else
			o->dimensions = NodeCoord(NodeCoord::Invalid, NodeCoord::Invalid, NodeCoord::Invalid);
#else
		// Avoid problems - the resulting image may still be incorrect though...
		if (o->type == DrawObject::Unknown)
			continue;
		if (o->haveCenter)
			o->corner1 = o->center;
		else
			o->center = o->corner1;
		if (o->haveDimensions)
			o->corner2 = o->dimensions;
		else
			o->dimensions = o->corner2;
		if (!o->haveDimensions && o->haveCenter)
			o->haveDimensions = true;
#endif
		for (int i = 0; i < 2; i++) {
			if (o->world) {
				int (TileGenerator::*world2Image)(int val) const;
				if (i==0)
					world2Image = &TileGenerator::worldX2ImageX;
				else
					world2Image = &TileGenerator::worldZ2ImageY;
				if (o->haveCenter)
					o->center.dimension[i] = (this->*world2Image)(o->center.dimension[i]);
				else
					o->corner1.dimension[i] = (this->*world2Image)(o->corner1.dimension[i]);
				if (!o->haveDimensions) {
					o->corner2.dimension[i] = (this->*world2Image)(o->corner2.dimension[i]);
					if (i == 1)
						ellipseAdjustCenter[i] = !ellipseAdjustCenter[i];
				}
				else if (i==1) {
					o->dimensions.dimension[i] = -o->dimensions.dimension[i];
					ellipseAdjustCenter[i] = !ellipseAdjustCenter[i];
				}
			}
			else {
				if (o->haveCenter)
					o->center.dimension[i] += i ? borderTop() : borderLeft();
				else
					o->corner1.dimension[i] += i ? borderTop() : borderLeft();
				if (!o->haveDimensions)
					o->corner2.dimension[i] += i ? borderTop() : borderLeft();
			}
		}

		for (int i = 0; i < 2; i++) {
			// Make sure all individual coordinates are ordered and dimensions are positive
			// EXCEPT for lines: lines do not have reflection symmetry.
			if (o->type != DrawObject::Line) {
				if (!o->haveDimensions) {
					if (o->corner1.dimension[i] > o->corner2.dimension[i]) {
						int temp = o->corner1.dimension[i];
						o->corner1.dimension[i] = o->corner2.dimension[i];
						o->corner2.dimension[i] = temp;
						ellipseAdjustCenter[i] = !ellipseAdjustCenter[i];
					}
				}
				else if (o->dimensions.dimension[i] < 0) {
					if (!o->haveCenter)
						o->corner1.dimension[i] += o->dimensions.dimension[i] + 1;
					else
						// Even dimensions cause asymetry
						o->center.dimension[i] += ((1 - o->dimensions.dimension[i]) % 2);
					o->dimensions.dimension[i] = -o->dimensions.dimension[i];
					ellipseAdjustCenter[i] = !ellipseAdjustCenter[i];
				}
			}

			// Convert to the apropriate type of coordinates.
			if (o->type == DrawObject::Ellipse) {
				if (!o->haveDimensions) {
					o->dimensions.dimension[i] = o->corner2.dimension[i] - o->corner1.dimension[i] + 1;
					o->center.dimension[i] = o->corner1.dimension[i] + o->dimensions.dimension[i] / 2;
				}
				else if (!o->haveCenter) {
					o->center.dimension[i] = o->corner1.dimension[i] + o->dimensions.dimension[i] / 2;
				}
				if (o->world && ellipseAdjustCenter[i] && o->dimensions.dimension[i] % 2 == 0)
					o->center.dimension[i] -= 1;
			}
			else if (o->type == DrawObject::Line || o->type == DrawObject::Rectangle) {
				if (o->haveCenter) {
					o->corner1.dimension[i] = o->center.dimension[i] - o->dimensions.dimension[i] / 2;
					if (o->dimensions.dimension[i] < 0)
						o->corner2.dimension[i] = o->corner1.dimension[i] + o->dimensions.dimension[i] + 1;
					else
						o->corner2.dimension[i] = o->corner1.dimension[i] + o->dimensions.dimension[i] - 1;
				}
				else if (o->haveDimensions) {
					if (o->dimensions.dimension[i] < 0)
						o->corner2.dimension[i] = o->corner1.dimension[i] + o->dimensions.dimension[i] + 1;
					else
						o->corner2.dimension[i] = o->corner1.dimension[i] + o->dimensions.dimension[i] - 1;
				}
			}
#ifdef DEBUG
			else
				assert(o->type == DrawObject::Point || o->type == DrawObject::Text);
#endif
		}
		switch(o->type) {
		case DrawObject::Point:
			gdImageSetPixel(m_image, o->center.x(), o->center.y(), o->color.to_libgd());
			break;
		case DrawObject::Line:
			gdImageLine(m_image, o->corner1.x(), o->corner1.y(), o->corner2.x(), o->corner2.y(), o->color.to_libgd());
			break;
		case DrawObject::Ellipse:
			gdImageArc(m_image, o->center.x(), o->center.y(), o->dimensions.x(), o->dimensions.y(), 0, 360, o->color.to_libgd());
			break;
		case DrawObject::Rectangle:
			gdImageRectangle(m_image, o->corner1.x(), o->corner1.y(), o->corner2.x(), o->corner2.y(), o->color.to_libgd());
			break;
		case DrawObject::Text: {
				std::string displayText = m_gdStringConv->convert(o->text.c_str());
				gdImageString(m_image, gdFontGetMediumBold(), o->center.x(), o->center.y(),
					reinterpret_cast<unsigned char *>(const_cast<char *>(displayText.c_str())), o->color.to_libgd());
			}
			break;
		default:
#ifdef DEBUG
			assert(o->type != o->type);
#endif
			break;
		}
	}
}

inline std::list<int> TileGenerator::getZValueList() const
{
	std::list<int> zlist;
	for (std::list<BlockPos>::const_iterator position = m_positions.begin(); position != m_positions.end(); ++position) {
		zlist.push_back(position->z());
	}
	zlist.sort();
	zlist.unique();
	zlist.reverse();
	return zlist;
}

void TileGenerator::writeImage(const std::string &output)
{
	FILE *out;
	out = fopen(output.c_str(), "wb");
	if (!out) {
		std::ostringstream oss;
		oss << "Error opening '" << output.c_str() << "': " << std::strerror(errno);
		throw std::runtime_error(oss.str());
	}
	gdImagePng(m_image, out);
	fclose(out);
	gdImageDestroy(m_image);
}

void TileGenerator::printUnknown()
{
	if (m_unknownNodes.size() > 0) {
		std::cerr << "Unknown nodes:" << std::endl;
		for (std::set<std::string>::iterator node = m_unknownNodes.begin(); node != m_unknownNodes.end(); ++node) {
			std::cerr << *node << std::endl;
		}
	}
}

// Adjust map coordinate for tiles and border
inline int TileGenerator::mapX2ImageX(int val) const
{
	if (m_tileWidth && m_tileBorderSize)
		val += ((val - m_tileMapXOffset / m_scaleFactor + m_tileWidth / m_scaleFactor) / (m_tileWidth / m_scaleFactor)) * m_tileBorderSize;
	return val + borderLeft();
}

// Adjust map coordinate for tiles and border
inline int TileGenerator::mapY2ImageY(int val) const
{
	if (m_tileHeight / m_scaleFactor && m_tileBorderSize)
		val += ((val - m_tileMapYOffset / m_scaleFactor + m_tileHeight / m_scaleFactor) / (m_tileHeight / m_scaleFactor)) * m_tileBorderSize;
	return val + borderTop();
}

// Convert world coordinate to image coordinate
inline int TileGenerator::worldX2ImageX(int val) const
{
	val = (val - m_xMin * 16) - m_mapXStartNodeOffset;
	return mapX2ImageX(val / m_scaleFactor);
}

// Convert world coordinate to image coordinate
inline int TileGenerator::worldZ2ImageY(int val) const
{
	val = (m_zMax * 16 + 15 - val) - m_mapYStartNodeOffset;
	return mapY2ImageY(val / m_scaleFactor);
}

