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
#include "PlayerAttributes.h"
#include "TileGenerator.h"
#include "ZlibDecompressor.h"
#if USE_SQLITE3
#include "db-sqlite3.h"
#endif
#if USE_LEVELDB
#include "db-leveldb.h"
#endif
#if USE_REDIS
#include "db-redis.h"
#endif

using namespace std;

static inline int64_t pythonmodulo(int64_t i, int64_t mod)
{
	if (i >= 0) {
		return i % mod;
	}
	else {
		return mod - ((-i) % mod);
	}
}

static inline int unsignedToSigned(long i, long max_positive)
{
	if (i < max_positive) {
		return i;
	}
	else {
		return i - 2l * max_positive;
	}
}

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

struct HeightMapColor
{
	int height[2];
	Color color[2];
};

TileGenerator::TileGenerator():
	verboseCoordinates(0),
	verboseReadColors(0),
	verboseStatistics(false),
	progressIndicator(false),
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
	m_shading(true),
	m_backend(DEFAULT_BACKEND),
	m_shrinkGeometry(true),
	m_blockGeometry(false),
	m_sqliteCacheWorldRow(false),
	m_chunkSize(0),
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
	m_surfaceDepth(INT_MAX)
{
	// Load default grey colors.
	m_heightMapColors.push_back(HeightMapColor(INT_MIN, Color(0,0,0), -129, Color(0,0,0)));
	m_heightMapColors.push_back(HeightMapColor(-128, Color(0,0,0), 127, Color(255,255,255)));
	m_heightMapColors.push_back(HeightMapColor(128, Color(255,255,255), INT_MAX, Color(255,255,255)));
}

TileGenerator::~TileGenerator()
{
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

void TileGenerator::setSqliteCacheWorldRow(bool cacheWorldRow)
{
	m_sqliteCacheWorldRow = cacheWorldRow;
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

void TileGenerator::setDrawAlpha(bool drawAlpha)
{
    m_drawAlpha = drawAlpha;
}

void TileGenerator::setDrawAir(bool drawAir)
{
	m_drawAir = drawAir;
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
	if (corner1.x > 0) {
		m_reqXMin = corner1.x / 16;
	}
	else {
		m_reqXMin = (corner1.x - 15) / 16;
	}
	if (corner1.y > 0) {
		m_reqZMin = corner1.y / 16;
	}
	else {
		m_reqZMin = (corner1.y - 15) / 16;
	}
	m_mapXStartNodeOffset = corner1.x - m_reqXMin * 16;
	m_mapYEndNodeOffset = m_reqZMin * 16 - corner1.y;

	if (corner2.x > 0) {
		m_reqXMax = corner2.x / 16;
	}
	else {
		m_reqXMax = (corner2.x - 15) / 16;
	}
	if (corner2.y > 0) {
		m_reqZMax = corner2.y / 16;
	}
	else {
		m_reqZMax = (corner2.y - 15) / 16;
	}
	m_mapXEndNodeOffset = corner2.x - (m_reqXMax * 16 + 15);
	m_mapYStartNodeOffset = (m_reqZMax * 16 + 15) - corner2.y;
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
	m_backend = backend;
}

void TileGenerator::setChunkSize(int size)
{
	m_chunkSize = size;
}

void TileGenerator::generate(const std::string &input, const std::string &output)
{
	string input_path = input;
	if (input_path[input.length() - 1] != PATH_SEPARATOR) {
		input_path += PATH_SEPARATOR;
	}

	openDb(input_path);
	loadBlocks();
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
	if (iline.good() && iline.peek() == '-') {
		char c;
		iline >> c >> std::ws;
		if (iline.bad() || !iline.eof()) {
			std::cerr << filename << ":" << linenr << ": bad line in colors file (" << line << ")" << std::endl;
			return;
		}
		m_nodeColors.erase(name);
	}
	else {
		int r, g, b, a, t;
		ColorEntry color;
		iline >> r;
		iline >> g;
		iline >> b;
		if (iline.fail()) {
			std::cerr << filename << ":" << linenr << ": bad line in colors file (" << line << ")" << std::endl;
			return;
		}
		a = 0xff;
		iline >> a;
		t = 0;
		iline >> t;
		color = ColorEntry(r,g,b,a,t);
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
			if (iline.good()) {
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
		m_nodeColors[name] = ColorEntry(0,0,0,255,1);		// Dummy entry - but must not be transparent
	}
	// Don't care if not at eof (== really eol). We might be reading a colors.txt file...
	if (iline.bad()) {
		std::cerr << filename << ":" << linenr << ": bad line in heightmap nodes file (" << line << ")" << std::endl;
		return;
	}
}

std::string TileGenerator::getWorldDatabaseBackend(const std::string &input)
{
	string backend;

	std::string worldFile = input + PATH_SEPARATOR + "world.mt";
	ifstream in;
	in.open(worldFile.c_str(), ifstream::in);
	if (!in.is_open())
		throw std::runtime_error(std::string("Failed to open world.mt file '") + worldFile + "'");

	std::string line;
	int linenr = 0;
	for (std::getline(in,line); in.good(); std::getline(in,line)) {
		linenr++;
		istringstream iline;
		iline.str(line);
		iline >> std::skipws;
		string variable;
		string eq;
		iline >> variable;
		if (variable != "backend")
			continue;
		iline >> eq;
		iline >> backend;
		if (in.fail() || eq != "=") {
			ostringstream oss;
			oss << "Error parsing 'backend' in world.mt file at line " << linenr;
			throw std::runtime_error(oss.str());
		}
	}
	in.close();
	if (backend == "")
		backend = "sqlite3";
	return backend;
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
	string backend = m_backend;
	bool unsupported = false;
	if (m_backend == "auto")
		backend = getWorldDatabaseBackend(input);

	if(backend == "sqlite3") {
#if USE_SQLITE3
		DBSQLite3 *db;
		m_db = db = new DBSQLite3(input);
		db->cacheWorldRow = m_sqliteCacheWorldRow;
#else
		unsupported = true;
#endif
	}
	else if (backend == "leveldb") {
#if USE_LEVELDB
		m_db = new DBLevelDB(input);
#else
		unsupported = true;
#endif
	}
	else if (backend == "redis") {
#if USE_REDIS
		m_db = new DBRedis(input);
#else
		unsupported = true;
#endif
	}
	else if (m_backend == "auto")
		throw std::runtime_error(((std::string) "World uses unrecognised database backend: ") + backend);
	else
		throw std::runtime_error(((std::string) "Internal error: unknown database backend: ") + m_backend);

	if (unsupported)
		throw std::runtime_error(((std::string) "World uses backend '") + backend + ", which was not enabled at compile-time.");
}

void TileGenerator::loadBlocks()
{
	#define MESSAGE_WIDTH 25
	int mapXMin, mapXMax;
	int mapYMin, mapYMax;
	int mapZMin, mapZMax;
	int geomYMin, geomYMax;
	long long world_blocks;
	long long map_blocks;
	if (verboseCoordinates >= 2) {
		bool partialBlocks = (m_mapXStartNodeOffset || m_mapXEndNodeOffset || m_mapYStartNodeOffset || m_mapYEndNodeOffset);
		if (partialBlocks || !m_blockGeometry) {
			cout
				<< std::setw(MESSAGE_WIDTH) << std::left
				<< (m_blockGeometry ? "Command-line Geometry:" : "Requested Geometry:")
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
	}
	if (m_blockGeometry) {
		m_mapXStartNodeOffset = 0;
		m_mapXEndNodeOffset = 0;
		m_mapYStartNodeOffset = 0;
		m_mapYEndNodeOffset = 0;
	}
	if (progressIndicator)
	    cout << "Scanning world...\r" << std::flush;
	mapXMin = INT_MAX/16-1;
	mapXMax = -INT_MIN/16+1;
	mapYMin = INT_MAX/16-1;
	mapYMax = -INT_MIN/16+1;
	mapZMin = INT_MAX/16-1;
	mapZMax = -INT_MIN/16+1;
	geomYMin = INT_MAX/16-1;
	geomYMax = -INT_MIN/16+1;
	const DB::BlockPosList &blocks = m_db->getBlockPos();
	world_blocks = 0;
	map_blocks = 0;
	for(DB::BlockPosList::const_iterator it = blocks.begin(); it != blocks.end(); ++it) {
		world_blocks ++;
		const BlockPos &pos = *it;
		if (pos.x < mapXMin) {
			mapXMin = pos.x;
		}
		if (pos.x > mapXMax) {
			mapXMax = pos.x;
		}
		if (pos.y < mapYMin) {
			mapYMin = pos.y;
		}
		if (pos.y > mapYMax) {
			mapYMax = pos.y;
		}
		if (pos.z < mapZMin) {
			mapZMin = pos.z;
		}
		if (pos.z > mapZMax) {
			mapZMax = pos.z;
		}
		if (pos.x < m_reqXMin || pos.x > m_reqXMax || pos.z < m_reqZMin || pos.z > m_reqZMax) {
			continue;
		}
		if (pos.y < geomYMin) {
			geomYMin = pos.y;
		}
		if (pos.y > geomYMax) {
			geomYMax = pos.y;
		}
		if (pos.y < m_reqYMin || pos.y > m_reqYMax) {
			continue;
		}
		map_blocks++;
		if (pos.y < m_yMin) {
			m_yMin = pos.y;
		}
		if (pos.y > m_yMax) {
			m_yMax = pos.y;
		}
		if (pos.x < m_xMin) {
			m_xMin = pos.x;
		}
		if (pos.x > m_xMax) {
			m_xMax = pos.x;
		}
		if (pos.z < m_zMin) {
			m_zMin = pos.z;
		}
		if (pos.z > m_zMax) {
			m_zMax = pos.z;
		}
		m_positions.push_back(pos);
	}
	if (verboseCoordinates >= 1) {
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
			<< std::setw(10) << world_blocks << "\n";
	}
	if (m_shrinkGeometry) {
		if (m_xMin != m_reqXMin) m_mapXStartNodeOffset = 0;
		if (m_xMax != m_reqXMax) m_mapXEndNodeOffset = 0;
		if (m_zMin != m_reqZMin) m_mapYEndNodeOffset = 0;
		if (m_zMax != m_reqZMax) m_mapYStartNodeOffset = 0;
	}
	else {
		if (verboseCoordinates >= 2) {
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
		m_xMin = m_reqXMin;
		m_xMax = m_reqXMax;
		m_zMin = m_reqZMin;
		m_zMax = m_reqZMax;
	}
	if (verboseCoordinates >= 2) {
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
	if (verboseCoordinates >= 1) {
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
	m_positions.sort();
	#undef MESSAGE_WIDTH
}

void TileGenerator::pushPixelRows(int zPosLimit) {
	if (m_shading)
		m_blockPixelAttributes.renderShading(m_drawAlpha);
	int y;
	for (y = m_blockPixelAttributes.getNextY(); y <= m_blockPixelAttributes.getLastY() && y < worldBlockZ2StoredY(m_zMin - 1) + m_mapYEndNodeOffset; y++) {
		for (int x = m_mapXStartNodeOffset; x < worldBlockX2StoredX(m_xMax + 1) + m_mapXEndNodeOffset; x++) {
			int mapX = x - m_mapXStartNodeOffset;
			int mapY = y - m_mapYStartNodeOffset;
			#define pixel m_blockPixelAttributes.attribute(y, x)
			//PixelAttribute &pixel = m_blockPixelAttributes.attribute(y, x);
			if (pixel.next16Empty) {
				x += 15;
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
	int yLimit = worldBlockZ2StoredY(zPosLimit);
	if (y <= yLimit) {
		m_blockPixelAttributes.scroll(yLimit);
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
		// Input / Output parameters
		int &pictSize,
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
	pictSize += (tileBorderLimit - tileBorderStart) * m_tileBorderSize;
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
	m_blockPixelAttributes.setParameters(m_storedWidth, 16, m_mapYStartNodeOffset);

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
				// Input / Output parameters
				m_pictWidth,
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
				// Input / Output parameters
				m_pictHeight,
				// Output parameters
				m_tileBorderYCount,
				tileMapYEndOffset,
				m_tileMapYOffset,
				// Behavior selection
				false);
	}

	// Print some useful messages in cases where it may not be possible to generate the image...
	long long pixels = static_cast<long long>(m_pictWidth + borderLeft() + borderRight()) * (m_pictHeight + borderTop() + borderBottom());
	// Study the libgd code to known why the maximum is the following:
	long long max_pixels = INT_MAX - INT_MAX % m_pictHeight;
	if (pixels > max_pixels) {
		cerr << "WARNING: Image will have " << pixels << " pixels; the PNG graphics library will refuse to handle more than approximately " << INT_MAX << std::endl;
	}
	// Estimated approximate maximum was determined by trial and error...
	// (24100x24100 succeeded; 24200x24200 failed)
	#define ESTIMATED_MAX_PIXELS_32BIT (24100*24100L)
	else if (sizeof(void *) == 4 && pixels > ESTIMATED_MAX_PIXELS_32BIT) {
		cerr << "WARNING: Image will have " << pixels << " pixels; The maximum achievable on a 32-bit system is approximately " << ESTIMATED_MAX_PIXELS_32BIT << std::endl;
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
			int xPos = m_tileMapXOffset + i * (m_tileWidth + m_tileBorderSize);
#ifdef DEBUG
			int xPos2 = mapX2ImageX(m_tileMapXOffset + i * m_tileWidth) - borderLeft() - 1;
			assert(xPos == xPos2);
#endif
			gdImageFilledRectangle(m_image, xPos + borderLeft(), borderTop(), xPos + (m_tileBorderSize-1) + borderLeft(), m_pictHeight + borderTop() - 1, borderColor);
		}
	}
	if (m_tileHeight && m_tileBorderSize) {
		int borderColor = m_tileBorderColor.to_libgd();
		for (int i = 0; i < m_tileBorderYCount; i++) {
			int yPos = m_tileMapYOffset + i * (m_tileHeight + m_tileBorderSize);
#ifdef DEBUG
			int yPos2 = mapY2ImageY(m_tileMapYOffset + i * m_tileHeight) - borderTop() - 1;
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
	if (version >= 22) {
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
			else if (name == "ignore") {
				m_nodeIDColor[nodeId] = NodeColorNotDrawn;
			}
			else {
				if (color != m_nodeColors.end()) {
					m_nodeIDColor[nodeId] = &color->second;
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

void TileGenerator::renderMap()
{
	int unpackErrors = 0;
	int blocks_rendered = 0;
	int area_rendered = 0;
	BlockPos currentPos;
	currentPos.x = INT_MIN;
	currentPos.y = 0;
	currentPos.z = INT_MIN;
	bool allReaded = false;
	for (std::list<BlockPos>::const_iterator position = m_positions.begin(); position != m_positions.end(); ++position) {
		const BlockPos &pos = *position;
		if (currentPos.x != pos.x || currentPos.z != pos.z) {
			area_rendered++;
			if (currentPos.z != pos.z) {
				pushPixelRows(pos.z);
				m_blockPixelAttributes.setLastY((m_zMax - pos.z) * 16 + 15);
				if (progressIndicator)
				    cout << "Processing Z-coordinate: " << std::setw(6) << pos.z*16
					<< "  (" << std::fixed << std::setprecision(0) << 100.0 * (m_zMax - pos.z) / (m_zMax - m_zMin)
					<< "%)          \r" << std::flush;
			}
			for (int i = 0; i < 16; ++i) {
				m_readedPixels[i] = 0;
			}
			allReaded = false;
			currentPos = pos;
		}
		else if (allReaded) {
			continue;
		}
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
				std::cerr << "Failed to unpack map block " << pos.x << "," << pos.y << "," << pos.z
					<< " (id: " << pos.databasePosStr(BlockPos::I64) << "). Block corrupt ?"
					<< std::endl
					<< "\tCoordinates: " << pos.x*16 << "," << pos.y*16 << "," << pos.z*16 << "+16+16+16"
					<< ";  Data: " << e.type << " at: " << e.offset << "(+" << e.length <<  ")/" << e.dataLength
					<< std::endl;
				unpackErrors++;
			}
			catch (ZlibDecompressor::DecompressError &e) {
				std::cerr << "Failed to decompress data in map block " << pos.x << "," << pos.y << "," << pos.z
					<< " (id: " << pos.databasePosStr(BlockPos::I64) << "). Block corrupt ?"
					<< std::endl
					<< "\tCoordinates: " << pos.x*16 << "," << pos.y*16 << "," << pos.z*16 << "+16+16+16"
					<< ";  Cause: " << e.message
					<< std::endl;
				unpackErrors++;
			}
		}
		if (unpackErrors >= 100) {
			throw(std::runtime_error("Too many block unpacking errors - bailing out"));
		}
	}
	if (currentPos.z != INT_MIN)
		pushPixelRows(currentPos.z - 1);
	if (verboseStatistics) {
		cout << "Statistics"
		     << ":  blocks read: " << m_db->getBlocksReadCount()
		     << "  (" << m_db->getBlocksCachedCount() << " cached + "
			       << m_db->getBlocksUnCachedCount() << " uncached)"
		     << ";  blocks rendered: " << blocks_rendered
		     << ";  area rendered: " << area_rendered
		     << "/" << (m_xMax-m_xMin+1) * (m_zMax-m_zMin+1)
		     << "  (" << (long long)area_rendered*16*16 << " nodes)";
		if (unpackErrors)
			cout << "  (" << unpackErrors << " errors)";
		 cout << std::endl;
	}
	else if (progressIndicator)
		cout << std::setw(50) << "" << "\r";
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
	int xBegin = worldBlockX2StoredX(pos.x);
	int zBegin = worldBlockZ2StoredY(pos.z);
	const unsigned char *mapData = mapBlock.c_str();
	int minY = (pos.y < m_reqYMin) ? 16 : (pos.y > m_reqYMin) ?  0 : m_reqYMinNode;
	int maxY = (pos.y > m_reqYMax) ? -1 : (pos.y < m_reqYMax) ? 15 : m_reqYMaxNode;
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
				int height = pos.y * 16 + y;
				if (m_heightMap) {
					if (m_nodeIDColor[content] && nodeColor.a != 0) {
						if (!(m_readedPixels[z] & (1 << x))) {
							if (height > m_surfaceHeight) m_surfaceHeight = height;
							if (height < m_surfaceDepth) m_surfaceDepth = height;
						}
						rowIsEmpty = false;
						pixel = PixelAttribute(computeMapHeightColor(height), height);
						m_readedPixels[z] |= (1 << x);
						break;
					}
				}
				else if (m_nodeIDColor[content]) {
					rowIsEmpty = false;
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
			m_blockPixelAttributes.attribute(zBegin + 15 - z,xBegin).next16Empty = false;
	}
}

void TileGenerator::renderScale()
{
	int color = m_scaleColor.to_libgd();
	if ((m_drawScale & DRAWSCALE_LEFT) && (m_drawScale & DRAWSCALE_TOP)) {
		gdImageString(m_image, gdFontGetMediumBold(), 24, 0, reinterpret_cast<unsigned char *>(const_cast<char *>("X")), color);
		gdImageString(m_image, gdFontGetMediumBold(), 2, 24, reinterpret_cast<unsigned char *>(const_cast<char *>("Z")), color);
	}

	string scaleText;

	if ((m_drawScale & DRAWSCALE_TOP)) {
		for (int i = (m_xMin / 4) * 4; i <= m_xMax; i += 4) {
			stringstream buf1, buf2;
			buf1 << i * 16;
			buf2 << "(" << i << ")";
			int xPos = worldX2ImageX(i * 16);

			scaleText = buf1.str();
			gdImageString(m_image, gdFontGetMediumBold(), xPos + 2, 0, reinterpret_cast<unsigned char *>(const_cast<char *>(scaleText.c_str())), color);
			scaleText = buf2.str();
			gdImageString(m_image, gdFontGetTiny(), xPos + 2, 16, reinterpret_cast<unsigned char *>(const_cast<char *>(scaleText.c_str())), color);
			gdImageLine(m_image, xPos, 0, xPos, borderTop() - 1, color);
		}
	}

	if ((m_drawScale & DRAWSCALE_LEFT)) {
		for (int i = (m_zMax / 4) * 4; i >= m_zMin; i -= 4) {
			stringstream buf1, buf2;
			buf1 << i * 16;
			buf2 << "(" << i << ")";
			int yPos = worldZ2ImageY(i * 16);

			scaleText = buf1.str();
			gdImageString(m_image, gdFontGetMediumBold(), 2, yPos, reinterpret_cast<unsigned char *>(const_cast<char *>(scaleText.c_str())), color);
			scaleText = buf2.str();
			gdImageString(m_image, gdFontGetTiny(), 2, yPos-10, reinterpret_cast<unsigned char *>(const_cast<char *>(scaleText.c_str())), color);
			gdImageLine(m_image, 0, yPos, borderLeft() - 1, yPos, color);
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
	double number_step = 64;
	while (number_step / height_step < 48)
		number_step *= 2;
	while (number_step / height_step > 96)
		number_step /= 2;

	double height = height_min;
	for (int x = 0; height < height_limit; x++, height += height_step) {
		Color color = computeMapHeightColor(int(height + 0.5));
		gdImageLine(m_image, xBorderOffset + x, yBorderOffset + 8, xBorderOffset + x, yBorderOffset + borderBottom() - 20, color.to_libgd());

		int iheight = int(height + (height > 0 ? 0.5 : -0.5));
		int iheight64 = int(iheight / number_step + (height > 0 ? 0.5 : -0.5)) * number_step;
		if (fabs(height - iheight64) <= height_step / 2 && (height - iheight64) > -height_step / 2) {
			if (iheight64 / int(number_step) % 2 == 1 && fabs(height) > 9999 && number_step / height_step < 56) {
				// Maybe not enough room for the number. Draw a tick mark instead
				gdImageLine(m_image, xBorderOffset + x, yBorderOffset + borderBottom() - 19, xBorderOffset + x, yBorderOffset + borderBottom() - 16, scaleColor);
			}
			else {
				stringstream buf;
				buf << iheight64;
				string scaleText = buf.str();
				gdImageString(m_image, gdFontGetMediumBold(), xBorderOffset + x + 2, yBorderOffset + borderBottom() - 16,
					reinterpret_cast<unsigned char *>(const_cast<char *>(scaleText.c_str())), scaleColor);
				gdImageLine(m_image, xBorderOffset + x, yBorderOffset + borderBottom() - 19, xBorderOffset + x, yBorderOffset + borderBottom() - 1, scaleColor);
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
		int imageX = worldX2ImageX(player->x / 10);
		int imageY = worldZ2ImageY(player->z / 10);

		gdImageArc(m_image, imageX, imageY, 5, 5, 0, 360, color);
		gdImageString(m_image, gdFontGetMediumBold(), imageX + 2, imageY + 2, reinterpret_cast<unsigned char *>(const_cast<char *>(player->name.c_str())), color);
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
			gdImageSetPixel(m_image, o->center.x, o->center.y, o->color.to_libgd());
			break;
		case DrawObject::Line:
			gdImageLine(m_image, o->corner1.x, o->corner1.y, o->corner2.x, o->corner2.y, o->color.to_libgd());
			break;
		case DrawObject::Ellipse:
			gdImageArc(m_image, o->center.x, o->center.y, o->dimensions.x, o->dimensions.y, 0, 360, o->color.to_libgd());
			break;
		case DrawObject::Rectangle:
			gdImageRectangle(m_image, o->corner1.x, o->corner1.y, o->corner2.x, o->corner2.y, o->color.to_libgd());
			break;
		case DrawObject::Text:
			gdImageString(m_image, gdFontGetMediumBold(), o->center.x, o->center.y, reinterpret_cast<unsigned char *>(const_cast<char *>(o->text.c_str())), o->color.to_libgd());
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
		zlist.push_back(position->z);
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
		val += ((val - m_tileMapXOffset + m_tileWidth) / m_tileWidth) * m_tileBorderSize + borderLeft();
	else
		val += borderLeft();
	return val;
}

// Adjust map coordinate for tiles and border
inline int TileGenerator::mapY2ImageY(int val) const
{
	if (m_tileHeight && m_tileBorderSize)
		val += ((val - m_tileMapYOffset + m_tileHeight) / m_tileHeight) * m_tileBorderSize + borderTop();
	else
		val += borderTop();
	return val;
}

// Convert world coordinate to image coordinate
inline int TileGenerator::worldX2ImageX(int val) const
{
	val = (val - m_xMin * 16) - m_mapXStartNodeOffset;
	return mapX2ImageX(val);
}

// Convert world coordinate to image coordinate
inline int TileGenerator::worldZ2ImageY(int val) const
{
	val = (m_zMax * 16 + 15 - val) - m_mapYStartNodeOffset;
	return mapY2ImageY(val);
}

