/*
 * =====================================================================
 *        Version:  1.0
 *        Created:  22.08.2012 15:15:54
 *         Author:  Miroslav Bendík
 *        Company:  LinuxOS.sk
 * =====================================================================
 */

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <map>
#include <string>
#include <cctype>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <sys/types.h>
#include "porting.h"
#include "TileGenerator.h"
#include "PixelAttributes.h"
#include "util.h"

using namespace std;

#define OPT_SQLITE_CACHEWORLDROW	0x81
#define OPT_PROGRESS_INDICATOR		0x82
#define OPT_DRAW_OBJECT			0x83
#define OPT_BLOCKCOLOR			0x84
#define OPT_DRAWAIR			0x85
#define OPT_VERBOSE_SEARCH_COLORS	0x86
#define OPT_CHUNKSIZE			0x87
#define OPT_HEIGHTMAP			0x88
#define OPT_HEIGHTMAPYSCALE		0x89
#define OPT_HEIGHT_LEVEL0		0x8a
#define OPT_HEIGHTMAPNODESFILE		0x8b
#define OPT_HEIGHTMAPCOLORSFILE		0x8c
#define OPT_DRAWHEIGHTSCALE		0x8d
#define OPT_SCALEFACTOR			0x8e
#define OPT_SCALEINTERVAL		0x8f
#define OPT_NO_BLOCKLIST_PREFETCH	0x90
#define OPT_DATABASE_FORMAT		0x91
#define OPT_SILENCE_SUGGESTIONS		0x92
#define OPT_PRESCAN_WORLD		0x93
#define OPT_DRAWNODES			0x94

#define DRAW_ARROW_LENGTH		10
#define DRAW_ARROW_ANGLE		30

// Will be replaced with the actual name and location of the executable (if found)
string executableName = "minetestmapper";
string executablePath;			// ONLY for use on windows
string installPrefix = INSTALL_PREFIX;
string nodeColorsDefaultFile = "colors.txt";
string heightMapNodesDefaultFile = "heightmap-nodes.txt";
string heightMapColorsDefaultFile = "heightmap-colors.txt";

class FuzzyBool {
private:
	int m_value;
	FuzzyBool(int i) : m_value(i) {}
public:
	FuzzyBool() : m_value(0) {}
	FuzzyBool(bool b) : m_value(b ? Yes.m_value : No.m_value) {}
	static const FuzzyBool Yes;
	static const FuzzyBool Maybe;
	static const FuzzyBool No;
inline friend bool operator==(FuzzyBool f1, FuzzyBool f2) { return f1.m_value == f2.m_value; }
inline friend bool operator!=(FuzzyBool f1, FuzzyBool f2) { return f1.m_value != f2.m_value; }
inline friend bool operator>=(FuzzyBool f1, FuzzyBool f2) { return f1.m_value >= f2.m_value; }
inline friend bool operator<=(FuzzyBool f1, FuzzyBool f2) { return f1.m_value <= f2.m_value; }
inline friend bool operator<(FuzzyBool f1, FuzzyBool f2) { return f1.m_value < f2.m_value; }
inline friend bool operator>(FuzzyBool f1, FuzzyBool f2) { return f1.m_value < f2.m_value; }
};
const FuzzyBool FuzzyBool::Yes = 1;
const FuzzyBool FuzzyBool::Maybe = 0;
const FuzzyBool FuzzyBool::No = -1;

static inline bool validStreamAtWsOrEof(std::istream &is)
{
	// May need to peek before knowing it's EOF.
	// However, peeking twice results in fail()...
	return !is.fail() && (is.eof() || is.peek() == ' ' || is.eof() || is.peek() == '\t');
}

static inline bool validStreamAtEof(std::istream &is)
{
	// May need to peek before knowing it's EOF.
	// However, peeking twice at eof results in fail()...
	return !is.fail() && (is.eof() || (is.peek(), is.eof()));
}

static inline int safePeekStream(std::istream &is)
{
	// Don't peek at EOF, else stream fails...
	return is.eof() ? EOF : is.peek();
}

void usage()
{
	const char *options_text = "[options]\n"
			"  -h/--help\n"
			"  -V/--version\n"
			"  -i/--input <world_path>\n"
			"  -o/--output <output_image.png>\n"
			"  --colors <file>\n"
			"  --heightmap-nodes <file>\n"
			"  --heightmap-colors[=<file>]\n"
			"  --height-level-0 <level>\n"
			"  --heightmap[=<color>]\n"
			"  --heightmap-yscale <scale>\n"
			"  --bgcolor <color>\n"
			"  --blockcolor <color>\n"
			"  --scalecolor <color>\n"
			"  --playercolor <color>\n"
			"  --origincolor <color>\n"
			"  --tilebordercolor <color>\n"
			"  --drawscale[=left,top]\n"
			"  --sidescale-interval <major>[[,:]<minor>]\n"
			"  --drawheightscale\n"
			"  --heightscale-interval <major>[[,:]<minor>]\n"
			"  --drawplayers\n"
			"  --draworigin\n"
			"  --drawalpha[=cumulative|cumulative-darken|average|none]\n"
			"  --drawair\n"
			"  --drawnodes [no-]air,[no-]ignore\n"
			"  --ignorenodes [no-]air,[no-]ignore\n"
			"  --draw[map]point \"<x>,<y> color\"\n"
			"  --draw[map]line \"<geometry> color\"\n"
			"  --draw[map]line \"<x>,<y> <angle> <length>[np] color\"\n"
			"  --draw[map]circle \"<geometry> color\"\n"
			"  --draw[map]ellipse \"<geometry> color\"\n"
			"  --draw[map]rectangle \"<geometry> color\"\n"
			"  --draw[map]arrow \"<x>,<y> <x>,<y> color\"\n"
			"  --draw[map]arrow \"<x>,<y> <angle> <length>[np] color\"\n"
			"  --draw[map]text \"<x>,<y> color text\"\n"
			"  --noshading\n"
			"  --min-y <y>\n"
			"  --max-y <y>\n"
			"  --backend <" USAGE_DATABASES ">\n"
			"  --disable-blocklist-prefetch[=force]\n"
			"  --database-format minetest-i64|freeminer-axyz|mixed|query\n"
			"  --prescan-world=full|auto|disabled\n"
			"  --geometry <geometry>\n"
			"\t(Warning: has a compatibility mode - see README.rst)\n"
			"  --cornergeometry <geometry>\n"
			"  --centergeometry <geometry>\n"
			"  --geometrymode pixel,block,fixed,shrink\n"
			"\tpixel:   interpret geometry as pixel-accurate\n"
			"\tblock:   round geometry away from zero, to entire map blocks (16 nodes)\n"
			"\tfixed:   generate a map of exactly the requested geometry\n"
			"\tshrink:  generate a smaller map if possible\n"
			"  --tiles <tilesize>[+<border>]|block|chunk\n"
			"  --tileorigin <x>,<y>|world|map\n"
			"  --tilecenter <x>,<y>|world|map\n"
			"  --scalefactor 1:<n>\n"
			"  --chunksize <size>\n"
			"  --silence-suggestions all,prefetch\n"
			"  --verbose[=n]\n"
			"  --verbose-search-colors[=n]\n"
			"  --progress\n"
			"Color formats:\n"
			"\t'#000' or '#000000'                                  (RGB)\n"
			"\t'#0000' or '#0000000'                                (ARGB - usable if an alpha value is allowed)\n"
			"\tSome symbolic color names:\n"
			"\t\twhite, black, gray, grey, red, green, blue,\n"
			"\t\tyellow, magenta, fuchsia, cyan, aqua,\n"
			"\t\torange, chartreuse, pink, violet, springgreen, azure\n"
			"\t\tbrown (= 50% orange)\n"
			"\tAs well as <color>[+-][wkrgbcmy]<n>, where n = 0.0..1.0 (or 1.00001 .. 255)\n"
			"\t\t[+-][wkrgbcmy]<n> mixes in or out white, black, red, green, blue, cyan, magenta, yellow\n"
			"Geometry formats:\n"
			"\t<width>x<heigth>[+|-<xoffset>+|-<yoffset>]           (dimensions and corner)\n"
			"\t<xoffset>,<yoffset>+<width>+<height>                 (corner and dimensions)\n"
			"\t<xcenter>,<ycenter>:<width>x<height>                 (center and dimensions)\n"
			"\t<xcorner1>,<ycorner1>:<xcorner2>,<ycorner2>          (corners of area)\n"
			"\tOriginal/legacy format - see note under '--geometry' option:\n"
			"\t<xoffset>:<yoffset>+<width>+<height>                 (corner and dimensions)\n"
			"X and Y coordinate formats:\n"
			"\t[+-]<n>                                              (node +/- <n>)\n"
			"\t[+-]<b>#[<n>]                                        (node <n> in block +/- <b>)\n"
			"\t[+-]<b>.[<n>]                                        (node +/- (b * 16 + n))\n"
			;
	std::cout << executableName << ' ' << options_text;
}

void parseDataFile(TileGenerator &generator, const string &input, string dataFile, string defaultFile,
	void (TileGenerator::*parseFile)(const std::string &fileName))
{
	if (!dataFile.empty()) {
		(generator.*parseFile)(dataFile);
		return;
	}

	std::vector<std::string> colorPaths;
	colorPaths.push_back(input);

	// Check if input/../.. looks like a valid minetest directory
	string minetestPath = input + PATH_SEPARATOR + ".." + PATH_SEPARATOR + "..";
	string minetestConf = minetestPath + PATH_SEPARATOR + "minetest.conf";
	int fd;
	if (0 <= (fd = open(minetestConf.c_str(), O_RDONLY))) {
		close(fd);
		colorPaths.push_back(minetestPath);
	}
	char *homedir;
	if ((homedir = getenv("HOME"))) {
		colorPaths.push_back(string(homedir) + PATH_SEPARATOR + ".minetest");
	}
// TODO: test/verify this (probably another subdirectory ('application data' or so) should be preferred)
//#if MSDOS || __OS2__ || __NT__ || _WIN32
//	if ((homedir = getenv("USERPROFILE"))) {
//		colorPaths.push_back(string(homedir) + PATH_SEPARATOR + ".minetest");
//	}
//#endif

#if MSDOS || __OS2__ || __NT__ || _WIN32 || DEBUG
	// On windows, assume that argv[0] contains the full path location of minetestmapper.exe
	// (i.e. where it is installed)
	// On Unix, the path is usually absent from argv[0], and we don't want the behavior to
	// depend on how it was invoked anyway.
	// In DEBUG mode, do check the command-line path; so this code can at least be tested on
	// Linux...
	if (executablePath != "") {
		size_t binpos = executablePath.find_last_of(PATH_SEPARATOR);
		if (binpos != string::npos) {
			string lastDir = executablePath.substr(binpos + 1);
			lastDir = strlower(lastDir);
			if (lastDir == "bin") {
				colorPaths.push_back(executablePath.substr(0, binpos) + PATH_SEPARATOR + "colors");
				colorPaths.push_back(executablePath.substr(0, binpos));
			}
			else {
				colorPaths.push_back(executablePath);
				colorPaths.push_back(executablePath + PATH_SEPARATOR + "colors");
			}
		}
		else {
			colorPaths.push_back(executablePath);
		}
	}
#endif
	if (!installPrefix.empty()) {
#if PACKAGING_FLAT
		colorPaths.push_back(installPrefix + PATH_SEPARATOR + "colors");
		colorPaths.push_back(installPrefix);
#else
		colorPaths.push_back(installPrefix + "/share/games/minetestmapper");
#endif
	}
	colorPaths.push_back("");

	std::vector<std::string> fileNames;
	fileNames.push_back(defaultFile);

	for (std::vector<std::string>::iterator path = colorPaths.begin(); path != colorPaths.end(); path++) {
		for (std::vector<std::string>::iterator name = fileNames.begin(); name != fileNames.end(); name++) {
			if (path->empty())
				dataFile = *name;
			else
				dataFile = *path + PATH_SEPARATOR +  *name;
			try {
				(generator.*parseFile)(dataFile);
				if (path->empty()) {
					// I hope this is not obnoxious to windows users ?
					cerr	<< "Warning: Using " << *name << " in current directory as a last resort." << std::endl
						<< "         Preferably, store the colors file in the world directory" << std::endl;
					if (homedir)
						cerr	<< "         or in the private minetest directory ($HOME/.minetest)." << std::endl;
					cerr	<< "         It can also be specified on the command-line" << std::endl;
				}
				return;
			} catch (std::runtime_error e) {
				// Ignore failure to locate colors file in standard location
				// (we have more places to search)
				if (path->empty()) {
				}
			}

		}
	}
	ostringstream oss;
	oss << "Failed to find or failed to open a " << defaultFile << " file.";
	throw std::runtime_error(oss.str().c_str());
}

// is: stream to read from
// coord: set to coordinate value that was read
// isBlockCoord: set to true if the coordinate read was a block coordinate
// wildcard: if non-zero, accept '*' as a coordinate, and return this value instead.
//	(suggested values for 'wildcard': INT_MIN or INT_MAX)
//
// Accepted coordinate syntax:
// 	[+-]<n>:	node coordinate:  node +/- n
// 	[+-]<b>#:	block coordinate: block +/- b	(isBlockCoord will be set to true)
// 	[+-]<b>#<n>:	node coordinate:  node <n> in block +/- <b>
// 	[+-]<b>.<n>:	node coordinate:  node +/- (b * 16 + n)
// As a special feature, double signs are also supported. E.g.:
//	+-3
// Which allows shell command-lines like the following
//	${width}x${height}+$xoffs+$yoffs
// (which otherwise require special measures to cope with xoffs or yoffs being negative...)
// Other uses of this feature are left as an excercise to the reader.
// Hint: --3.5 is *not* the same as 3.5
static bool parseNodeCoordinate(istream &is, int &coord, bool &isBlockCoord, int wildcard)
{
	char c;
	int i;
	char s;

	s = c = safePeekStream(is);
	if (c == '*') {
		if (wildcard) {
			i = wildcard;
			is.ignore(1);
		}
		else {
			is >> coord;	// Set stream status to failed
		}
	}
	else {
		wildcard = 0;		// not processing a wildcard now
		if (s == '-' || s == '+')
			is.ignore(1);
		else
			s = '+';
		is >> i;
		if (s == '-')
			i = -i;
	}
	if (is.fail())
		return false;
	coord = i;
	isBlockCoord = false;
	if (validStreamAtEof(is))
		return true;

	// Check if this is a block number, and so: if it has a node number.
	c = safePeekStream(is);
	if (c == '#' || c == '.') {
		// coordinate read was a block number
		is.ignore(1);
		if (wildcard) {
			return false;	// wildcards are generic
		}
		else if (isdigit(safePeekStream(is))) {
			// has a node number / offset
			is >> i;
			if (!is.fail()) {
				if (c == '.' && s == '-') {
					// Using '.', the node number has same sign as block number
					// Using '#', the node number is always positive
					// i.e. -1#1 is: node #1 in block -1 (i.e. node -16 + 1 = -15)
					// i.e. -1.1 is: 1 block and 1 node in negative direction (i.e. node 16 - 1 = -17)
					i = -i;
				}
				coord = coord * 16 + i;
			}
		}
		else {
			// No node number / offset
			isBlockCoord = true;
		}
	}
	return (!is.fail());
}

static bool parseCoordinates(istream &is, NodeCoord &coord, int n, int wildcard = 0, char separator = ',')
{
	bool result;
	result = true;
	NodeCoord tempCoord;
	for (int i = 0; result && i < n; i++) {
		if (i && separator) {
			char c;
			is >> c;
			if (c != separator) {
				result = false;
				break;
			}
		}
		result = parseNodeCoordinate(is, tempCoord.dimension[i], tempCoord.isBlock[i], wildcard);
	}
	if (result)
		coord = tempCoord;
	return result;
}

static void convertBlockToNodeCoordinates(NodeCoord &coord, int offset, int n)
{
	for (int i = 0; i < n; i++) {
		if (coord.isBlock[i]) {
			coord.dimension[i] = coord.dimension[i] * 16 + offset;
			coord.isBlock[i] = false;
		}
	}
}

static void convertBlockToNodeCoordinates(NodeCoord &coord1, NodeCoord &coord2, int n)
{
	for (int i = 0; i < n; i++) {
		int c1 = coord1.isBlock[i] ? coord1.dimension[i] * 16 : coord1.dimension[i];
		int c2 = coord2.isBlock[i] ? coord2.dimension[i] * 16 + 15 : coord2.dimension[i];
		if (c1 > c2) {
		    c1 = coord1.isBlock[i] ? coord1.dimension[i] * 16 + 15 : coord1.dimension[i];
		    c2 = coord2.isBlock[i] ? coord2.dimension[i] * 16 : coord2.dimension[i];
		}
		coord1.dimension[i] = c1;
		coord2.dimension[i] = c2;
		coord1.isBlock[i] = false;
		coord2.isBlock[i] = false;
	}
}

static void convertCenterToCornerCoordinates(NodeCoord &coord, NodeCoord &dimensions, int n)
{
	// This results in a slight bias to the negative side.
	// i.e.: 0,0:2x2 will be -1,-1 .. 0,0 and not 0,0 .. 1,1
	// The advantage is that e.g. 0#,0#:16x16 selects the 16x16 area that is block 0:
	// 	0#,0#:16x16 -> 0,0:15,15
	// With a bias to the positive side, that would be:
	// 	0#,0#:16x16 -> 1,1:16,16
	// Which is counter-intuitive by itself (IMHO :-)
	for (int i = 0; i < n; i++) {
		if (dimensions.dimension[i] < 0)
			coord.dimension[i] += -dimensions.dimension[i] / 2;
		else
			coord.dimension[i] -= dimensions.dimension[i] / 2;
	}
}

static void convertDimensionToCornerCoordinates(NodeCoord &coord1, NodeCoord &coord2, NodeCoord &dimensions, int n)
{
	for (int i = 0; i < n; i++) {
		if (dimensions.dimension[i] < 0)
			coord2.dimension[i] = coord1.dimension[i] + dimensions.dimension[i] + 1;
		else
			coord2.dimension[i] = coord1.dimension[i] + dimensions.dimension[i] - 1;
	}
}

static void convertCornerToDimensionCoordinates(NodeCoord &coord1, NodeCoord &coord2, NodeCoord &dimensions, int n)
{
	for (int i = 0; i < n; i++) {
		if (coord2.dimension[i] < coord1.dimension[i])
			dimensions.dimension[i] = coord2.dimension[i] - coord1.dimension[i] - 1;
		else
			dimensions.dimension[i] = coord2.dimension[i] - coord1.dimension[i] + 1;
	}
}

static void convertPolarToCartesianCoordinates(NodeCoord &coord1, NodeCoord &coord2, double angle, double length)
{
	angle *= M_PI / 180;
	double dxf = sin(angle) * length;
	int dx = (dxf < 0 ? -1 : 1) * int(fabs(dxf + (dxf < 0 ? -0.5 : 0.5)));
	dx -= dx < 0 ? -1 : dx > 0 ? +1 : 0;
	double dyf = cos(angle) * length;
	int dy = (dyf < 0 ? -1 : 1) * int(fabs(dyf + (dyf < 0 ? -0.5 : 0.5)));
	dy -= dy < 0 ? -1 : dy > 0 ? +1 : 0;
	coord2.x() = coord1.x() + dx;
	coord2.y() = coord1.y() + dy;
}

static void convertCartesianToPolarCoordinates(NodeCoord &coord1, NodeCoord &coord2, double &angle, double &length)
{
	int lx = coord2.x() - coord1.x();
	lx += lx < 0 ? -1 : lx > 0 ? 1 : 0;
	int ly = coord2.y() - coord1.y();
	ly += ly < 0 ? -1 : ly > 0 ? 1 : 0;
	length = sqrt(lx*lx + ly*ly);
	angle = atan2(lx, ly) / M_PI * 180;
}

static void orderCoordinateDimensions(NodeCoord &coord1, NodeCoord &coord2, int n)
{
	for (int i = 0; i < n; i++)
		if (coord1.dimension[i] > coord2.dimension[i]) {
			int temp = coord1.dimension[i];
			coord1.dimension[i] = coord2.dimension[i];
			coord2.dimension[i] = temp;
		}
}


// Parse the following geometry formats:
// <w>x<h>[+<x>+<y>]
//	(dimensions, and position)
//	(if x and y are omitted, they default to -w/2 and -h/2)
// <x1>,<y1>:<x2>,<y2>
//	(2 corners of the area)
// <x>,<y>:<w>x<h>
//	(center of the area, and dimensions)
// <x>[,:]<y>+<w>+<h>
//	(corner of the area, and dimensions)
// <x>,<y>@<angle>+<length>
static bool parseGeometry(istream &is, NodeCoord &coord1, NodeCoord &coord2, NodeCoord &dimensions, bool &legacy, bool &centered, int n, FuzzyBool expectDimensions, int wildcard = 0)
{
	int pos;
	pos = is.tellg();
	legacy = false;

	for (int i = 0; i < n; i++) {
		coord1.dimension[i] = NodeCoord::Invalid;
		coord2.dimension[i] = NodeCoord::Invalid;
		dimensions.dimension[i] = NodeCoord::Invalid;
	}

	if (expectDimensions >= FuzzyBool::Maybe && parseCoordinates(is, dimensions, n, 0, 'x')) {
		convertBlockToNodeCoordinates(dimensions, 0, n);
		// <w>x<h>[+<x>+<y>]
		if (validStreamAtEof(is)) {
			centered = true;
			for (int i = 0; i < n; i++) {
				coord1.dimension[i] = 0;
				coord1.isBlock[i] = false;
			}
			return validStreamAtWsOrEof(is);
		}
		else {
			centered = false;
			if (parseCoordinates(is, coord1, n, 0, '\0')) {
				convertBlockToNodeCoordinates(coord1, 0, n);
				return validStreamAtWsOrEof(is);
			}
			else
				return false;
		}
	}

	is.clear();
	is.seekg(pos);
	if (wildcard) {
		coord1.x() = coord1.y() = coord1.z() = 0;
	}
	if (parseCoordinates(is, coord1, n, wildcard, ',')) {
		if (expectDimensions == FuzzyBool::No || (expectDimensions == FuzzyBool::Maybe && validStreamAtWsOrEof(is))) {
			// Just coordinates were specified
			centered = false;
			return validStreamAtWsOrEof(is);
		}
		else if (wildcard && (coord1.x() == wildcard || coord1.y() == wildcard || coord1.z() == wildcard)) {
			// wildcards are only allowed for plain coordinates (i.e. no dimensions)
			return false;
		}
		else if (safePeekStream(is) == ':') {
			is.ignore(1);
			pos = is.tellg();
			if (parseCoordinates(is, coord2, n, 0, ',')) {
				// <x1>,<y1>:<x2>,<y2>
				centered = false;
				convertBlockToNodeCoordinates(coord1, coord2, n);
				return validStreamAtWsOrEof(is);
			}
			is.clear();
			is.seekg(pos);
			if (parseCoordinates(is, dimensions, n, 0, 'x')) {
				// <x>,<y>:<w>x<h>
				// (x,y is the center of the area by default)
				centered = true;
				convertBlockToNodeCoordinates(coord1, 8, n);
				convertBlockToNodeCoordinates(dimensions, 0, n);
				return validStreamAtWsOrEof(is);
			}
			else {
				return false;
			}
		}
		else if (safePeekStream(is) == '@') {
			// <x>,<y>@<angle>+<length>[np]
			if (n != 2)
				return false;
			centered = false;
			is.ignore(1);
			double angle;
			double length;
			bool world_relative = false;

			is >> angle;
			if (safePeekStream(is) != '+' && safePeekStream(is) != '-')
				return false;
			if (safePeekStream(is) == '+')
				is.ignore(1);
			is >> length;
			if (!validStreamAtEof(is)) {
				switch (safePeekStream(is)) {
					case 'n' :
						is.ignore(1);
						world_relative = true;
						break;
					case 'p' :
						is.ignore(1);
						world_relative = false;
						break;
				}
			}
			if (!validStreamAtWsOrEof(is))
				return false;
			convertPolarToCartesianCoordinates(coord1, coord2, angle, length);
			if (!world_relative) {
				convertCornerToDimensionCoordinates(coord1, coord2, dimensions, n);
				coord2.x() = NodeCoord::Invalid;
				coord2.y() = NodeCoord::Invalid;
			}
			return true;
		}
		else {
			// <x>,<y>+<w>+<h>
			centered = false;
			if (parseCoordinates(is, dimensions, n, 0, '\0')) {
				convertBlockToNodeCoordinates(coord1, 0, n);
				convertBlockToNodeCoordinates(dimensions, 0, n);
				return validStreamAtWsOrEof(is);
			}
			else {
				return false;
			}
		}
	}

	is.clear();
	is.seekg(pos);
	if (parseCoordinates(is, coord1, n, 0, ':')) {
		// <x>:<y>+<w>+<h>
		legacy = true;
		centered = false;
		if (parseCoordinates(is, dimensions, n, 0, '\0')) {
			convertBlockToNodeCoordinates(coord1, 0, n);
			convertBlockToNodeCoordinates(dimensions, 0, n);
			return validStreamAtWsOrEof(is);
		}
		return false;
	}

	return false;
}

static bool parseMapGeometry(istream &is, NodeCoord &coord1, NodeCoord &coord2, bool &legacy, FuzzyBool interpretAsCenter)
{
	NodeCoord dimensions;
	bool centered;

	bool result = parseGeometry(is, coord1, coord2, dimensions, legacy, centered, 2, FuzzyBool::Yes);

	if (result) {
		bool haveCoord2 = coord2.dimension[0] != NodeCoord::Invalid
			&& coord2.dimension[1] != NodeCoord::Invalid;
		bool haveDimensions = dimensions.dimension[0] != NodeCoord::Invalid
			&& dimensions.dimension[1] != NodeCoord::Invalid;
		if (!haveCoord2 && haveDimensions) {
			// Convert coord1 + dimensions to coord1 + coord2.
			// First, if coord1 must be interpreted as center of the area, adjust it to be a corner
			if ((centered && interpretAsCenter == FuzzyBool::Maybe) || interpretAsCenter == FuzzyBool::Yes)
				convertCenterToCornerCoordinates(coord1, dimensions, 2);
			convertDimensionToCornerCoordinates(coord1, coord2, dimensions, 2);
		}
		else if (!haveCoord2 || haveDimensions) {
			return false;
		}
		orderCoordinateDimensions(coord1, coord2, 2);
	}

	return result;
}

int main(int argc, char *argv[])
{
	if (argc) {
		string argv0 = argv[0];
		size_t pos = argv0.find_last_of(PATH_SEPARATOR);
		if (pos == string::npos) {
			if (!argv0.empty())
				executableName = argv0;
				executablePath = "";
		}
		else {
			executableName = argv0.substr(pos + 1);
			executablePath = argv0.substr(0, pos);
		}
	}

	static struct option long_options[] =
	{
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"input", required_argument, 0, 'i'},
		{"output", required_argument, 0, 'o'},
		{"colors", required_argument, 0, 'C'},
		{"heightmap-nodes", required_argument, 0, OPT_HEIGHTMAPNODESFILE},
		{"heightmap-colors", required_argument, 0, OPT_HEIGHTMAPCOLORSFILE},
		{"heightmap", optional_argument, 0, OPT_HEIGHTMAP},
		{"heightmap-yscale", required_argument, 0, OPT_HEIGHTMAPYSCALE},
		{"height-level-0", required_argument, 0, OPT_HEIGHT_LEVEL0},
		{"bgcolor", required_argument, 0, 'b'},
		{"blockcolor", required_argument, 0, OPT_BLOCKCOLOR},
		{"scalecolor", required_argument, 0, 's'},
		{"origincolor", required_argument, 0, 'r'},
		{"playercolor", required_argument, 0, 'p'},
		{"draworigin", no_argument, 0, 'R'},
		{"drawplayers", no_argument, 0, 'P'},
		{"drawscale", optional_argument, 0, 'S'},
		{"sidescale-interval", required_argument, 0, OPT_SCALEINTERVAL},
		{"drawheightscale", no_argument, 0, OPT_DRAWHEIGHTSCALE},
		{"heightscale-interval", required_argument, 0, OPT_SCALEINTERVAL},
		{"drawalpha", optional_argument, 0, 'e'},
		{"drawair", no_argument, 0, OPT_DRAWAIR},
		{"drawnodes", required_argument, 0, OPT_DRAWNODES},
		{"ignorenodes", required_argument, 0, OPT_DRAWNODES},
		{"drawpoint", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawline", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawcircle", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawellipse", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawrectangle", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawarrow", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawtext", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawmappoint", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawmapline", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawmapcircle", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawmapellipse", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawmaprectangle", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawmaparrow", required_argument, 0, OPT_DRAW_OBJECT},
		{"drawmaptext", required_argument, 0, OPT_DRAW_OBJECT},
		{"noshading", no_argument, 0, 'H'},
		{"geometry", required_argument, 0, 'g'},
		{"cornergeometry", required_argument, 0, 'g'},
		{"centergeometry", required_argument, 0, 'g'},
		{"geometrymode", required_argument, 0, 'G'},
		{"forcegeometry", no_argument, 0, 'G'},
		{"min-y", required_argument, 0, 'a'},
		{"max-y", required_argument, 0, 'c'},
		{"backend", required_argument, 0, 'd'},
		{"disable-blocklist-prefetch", optional_argument, 0, OPT_NO_BLOCKLIST_PREFETCH},
		{"database-format", required_argument, 0, OPT_DATABASE_FORMAT},
		{"prescan-world", required_argument, 0, OPT_PRESCAN_WORLD},
		{"sqlite-cacheworldrow", no_argument, 0, OPT_SQLITE_CACHEWORLDROW},
		{"tiles", required_argument, 0, 't'},
		{"tileorigin", required_argument, 0, 'T'},
		{"tilecenter", required_argument, 0, 'T'},
		{"tilebordercolor", required_argument, 0, 'B'},
		{"scalefactor", required_argument, 0, OPT_SCALEFACTOR},
		{"chunksize", required_argument, 0, OPT_CHUNKSIZE},
		{"silence-suggestions", required_argument, 0, OPT_SILENCE_SUGGESTIONS},
		{"verbose", optional_argument, 0, 'v'},
		{"verbose-search-colors", optional_argument, 0, OPT_VERBOSE_SEARCH_COLORS},
		{"progress", no_argument, 0, OPT_PROGRESS_INDICATOR},
		{NULL, 0, 0, 0}
	};

	string input;
	string output;
	bool heightMap = false;
	bool loadHeightMapColorsFile = false;
	string nodeColorsFile;
	string heightMapColorsFile;
	string heightMapNodesFile;
	bool foundGeometrySpec = false;
	bool setFixedOrShrinkGeometry = false;
	CharEncodingConverter *charConvUTF8;
	charConvUTF8 = CharEncodingConverter::createStandardConverter("UTF-8");

	TileGenerator generator;
	try {
		int option_index = 0;
		int c = 0;
		while (1) {
			c = getopt_long(argc, argv, "hi:o:", long_options, &option_index);
			if (c == -1) {
				if (input.empty() || output.empty()) {
					std::cerr << "Input (world directory) or output (PNG filename) missing" << std::endl;
					usage();
					return 0;
				}
				break;
			}
			switch (c) {
				case 'h':
					usage();
					return 0;
					break;
				case 'V':
					cout << "Minetestmapper - Version-ID: " << VERSION_MAJOR << "." << VERSION_MINOR << std::endl;
					return 0;
					break;
				case 'i':
					input = optarg;
					break;
				case 'o':
					output = optarg;
					break;
				case 'C':
					nodeColorsFile = optarg;
					break;
				case OPT_HEIGHTMAPNODESFILE:
					heightMapNodesFile = optarg;
					break;
				case OPT_HEIGHTMAPCOLORSFILE:
					heightMapColorsFile = optarg;
					break;
				case 'b':
					generator.setBgColor(Color(optarg, 0));
					break;
				case OPT_NO_BLOCKLIST_PREFETCH:
					if (optarg && *optarg) {
						if (strlower(optarg) == "force")
							generator.setGenerateNoPrefetch(2);
						else {
							std::cerr << "Invalid parameter to '" << long_options[option_index].name << "'; expected 'force' or nothing." << std::endl;
							usage();
							exit(1);
						}
					}
					else {
						generator.setGenerateNoPrefetch(1);
					}
					break;
				case OPT_DATABASE_FORMAT: {
						std::string opt = strlower(optarg);
						if (opt == "minetest-i64")
							generator.setDBFormat(BlockPos::I64, false);
						else if (opt == "freeminer-axyz")
							generator.setDBFormat(BlockPos::AXYZ, false);
						else if (opt == "mixed")
							generator.setDBFormat(BlockPos::Unknown, false);
						else if (opt == "query")
							generator.setDBFormat(BlockPos::Unknown, true);
						else {
							std::cerr << "Invalid parameter to '" << long_options[option_index].name << "': '" << optarg << "'" << std::endl;
							usage();
							exit(1);
						}
					}
					break;
				case OPT_PRESCAN_WORLD: {
						std::string opt = strlower(optarg);
						generator.setGenerateNoPrefetch(0);
						if (opt == "disabled-force")
							generator.setGenerateNoPrefetch(2);
						else if (opt == "disabled")
							generator.setGenerateNoPrefetch(1);
						else if (opt == "auto")
							generator.setScanEntireWorld(false);
						else if (opt == "full")
							generator.setScanEntireWorld(true);
						else {
							std::cerr << "Invalid parameter to '" << long_options[option_index].name << "': '" << optarg << "'" << std::endl;
							usage();
							exit(1);
						}
					}
					break;
				case OPT_HEIGHTMAP:
					generator.setHeightMap(true);
					heightMap = true;
					if (optarg && *optarg) {
						loadHeightMapColorsFile = false;
						std::string color = strlower(optarg);
						if (color == "grey" || color == "gray")
							generator.setHeightMapColor(Color(0, 0, 0), Color(255, 255, 255));
						else if (color == "black")
							generator.setHeightMapColor(Color(0, 0, 0), Color(0, 0, 0));
						else if (color == "white")
							generator.setHeightMapColor(Color(255, 255, 255), Color(255, 255, 255));
						else
							generator.setHeightMapColor(Color(0, 0, 0), Color(color, 0));
						break;
					}
					else {
						loadHeightMapColorsFile = true;
					}
					break;
				case OPT_HEIGHTMAPYSCALE:
					if (isdigit(optarg[0]) || ((optarg[0]=='-' || optarg[0]=='+') && isdigit(optarg[1]))) {
						float scale = atof(optarg);
						generator.setHeightMapYScale(scale);
					}
					else {
						std::cerr << "Invalid parameter to '" << long_options[option_index].name << "': '" << optarg << "'" << std::endl;
						usage();
						exit(1);
					}
					break;
				case OPT_HEIGHT_LEVEL0:
					if (isdigit(optarg[0]) || ((optarg[0]=='-' || optarg[0]=='+') && isdigit(optarg[1]))) {
						int level = atoi(optarg);
						generator.setSeaLevel(level);
					}
					else {
						std::cerr << "Invalid parameter to '" << long_options[option_index].name << "': '" << optarg << "'" << std::endl;
						usage();
						exit(1);
					}
					break;
				case OPT_BLOCKCOLOR:
					generator.setBlockDefaultColor(Color(optarg, 0));
					break;
				case 's':
					generator.setScaleColor(Color(optarg,0));
					break;
				case 'r':
					generator.setOriginColor(Color(optarg,1));
					break;
				case 'p':
					generator.setPlayerColor(Color(optarg,1));
					break;
				case 'B':
					generator.setTileBorderColor(Color(optarg,0));
					break;
				case 'R':
					generator.setDrawOrigin(true);
					break;
				case 'P':
					generator.setDrawPlayers(true);
					break;
				case 'S':
					if (optarg && *optarg) {
						std::string opt = strlower(optarg);
						if (opt == "left")
							generator.setDrawScale(DRAWSCALE_LEFT);
						else if (opt == "top")
							generator.setDrawScale(DRAWSCALE_TOP);
						else if (opt == "left,top")
							generator.setDrawScale(DRAWSCALE_LEFT | DRAWSCALE_TOP);
						else if (opt == "top,left")
							generator.setDrawScale(DRAWSCALE_LEFT | DRAWSCALE_TOP);
						else {
							std::cerr << "Invalid parameter to '" << long_options[option_index].name
								<< "': '" << optarg << "' (expected: left,top)" << std::endl;
							usage();
							exit(1);
						}
					}
					else {
						generator.setDrawScale(DRAWSCALE_LEFT | DRAWSCALE_TOP);
					}
					break;
				case OPT_DRAWHEIGHTSCALE :
					generator.setDrawHeightScale(DRAWHEIGHTSCALE_BOTTOM);
					break;
				case OPT_SCALEINTERVAL: {
						istringstream arg;
						arg.str(optarg);
						int major;
						int minor;
						char sep;
						arg >> major;
						if (major < 0 || !isdigit(*optarg) || arg.fail()) {
							std::cerr << "Invalid parameter to '" << long_options[option_index].name
								<< "': '" << optarg << "' (expected: <major>[,<minor>]" << std::endl;
							usage();
							exit(1);
						}
						arg >> std::ws >> sep >> std::ws;
						if (!arg.fail()) {
							if ((sep != ',' && sep != ':') || !isdigit(arg.peek())) {
								std::cerr << "Invalid parameter to '" << long_options[option_index].name
									<< "': '" << optarg << "' (expected: <major>[,<minor>]" << std::endl;
								usage();
								exit(1);
							}
							arg >> minor;
							if (minor < 0) {
								std::cerr << "Invalid parameter to '" << long_options[option_index].name
									<< "': '" << optarg << "' (expected: <major>[,<minor>]" << std::endl;
								usage();
								exit(1);
							}
						}
						else {
							minor = 0;
						}
						if (minor && sep == ':') {
							if (major % minor) {
								std::cerr << long_options[option_index].name << ": Cannot divide major interval in "
									<< minor << " subintervals (not divisible)" << std::endl;
								exit(1);
							}
							minor = major / minor;
						}
						if ((minor % major) == 0)
							minor = 0;
						if (long_options[option_index].name[0] == 's') {
							generator.setSideScaleInterval(major, minor);
						}
						else if (long_options[option_index].name[0] == 'h') {
							generator.setHeightScaleInterval(major, minor);
						}
						else {
							std::cerr << "Internal error: option " << long_options[option_index].name << " not handled" << std::endl;
							exit(1);
						}
					}
					break;
				case OPT_SILENCE_SUGGESTIONS: {
						for (size_t i = 0; i < strlen(optarg); i++) {
							optarg[i] = tolower(optarg[i]);
							if (optarg[i] == ',')
								optarg[i] = ' ';
						}
						std::istringstream iss(optarg);
						std::string flag;
						iss >> std::skipws >> flag;
						while (!iss.fail()) {
							if (flag == "all")
								generator.setSilenceSuggestion(SUGGESTION_ALL);
							else if (flag == "prefetch")
								generator.setSilenceSuggestion(SUGGESTION_PREFETCH);
							else {
								std::cerr << "Invalid flag to '" << long_options[option_index].name << "': '" << flag << "'" << std::endl;
								usage();
								exit(1);
							}
							iss >> flag;
						}
					}
					break;
				case 'v':
					if (optarg && isdigit(optarg[0]) && optarg[1] == '\0') {
						generator.verboseStatistics = optarg[0] - '0';
						generator.verboseCoordinates = optarg[0] - '0';
					}
					else {
						generator.verboseStatistics = 1;
						generator.verboseCoordinates = 1;
					}
					break;
				case OPT_VERBOSE_SEARCH_COLORS:
					if (optarg && isdigit(optarg[0]) && optarg[1] == '\0') {
						generator.verboseReadColors = optarg[0] - '0';
					}
					else {
						generator.verboseReadColors++;
					}
					break;
				case 'e':
					generator.setDrawAlpha(true);
					if (!optarg || !*optarg)
						PixelAttribute::setMixMode(PixelAttribute::AlphaMixAverage);
					else if (string(optarg) == "cumulative" || strlower(optarg) == "nodarken")
						// "nodarken" is supported for backwards compatibility
						PixelAttribute::setMixMode(PixelAttribute::AlphaMixCumulative);
					else if (string(optarg) == "darken" || strlower(optarg) == "cumulative-darken")
						// "darken" is supported for backwards compatibility
						PixelAttribute::setMixMode(PixelAttribute::AlphaMixCumulativeDarken);
					else if (strlower(optarg) == "average")
						PixelAttribute::setMixMode(PixelAttribute::AlphaMixAverage);
					else if (strlower(optarg) == "none")
						generator.setDrawAlpha(false);
					else {
						std::cerr << "Invalid parameter to '" << long_options[option_index].name << "': '" << optarg << "'" << std::endl;
						usage();
						exit(1);
					}
					break;
				case OPT_DRAWAIR:
					generator.setDrawAir(true);
					break;
				case OPT_DRAWNODES: {
						bool draw = long_options[option_index].name[0] == 'd';
						for (char *c = optarg; *c; c++) {
							*c = tolower(*c);
							if (*c == ',') *c = ' ';
						}
						istringstream iss(optarg);
						string flag;
						iss >> std::skipws >> flag;
						while (!iss.fail()) {
							bool enable = draw;
							if (flag.substr(0,3) == "no-") {
								flag = flag.substr(3);
								enable = !enable;
							}
							if (flag == "")
								(void) true;	// Empty flag - ignore
							else if (flag == "ignore")
								generator.setDrawIgnore(enable);
							else if (flag == "air")
								generator.setDrawAir(enable);
							else {
								std::cerr << "Invalid " << long_options[option_index].name << " flag '" << flag << "'" << std::endl;
								usage();
								exit(1);
							}
							iss >> flag;
						}
					}
					break;
				case 'H':
					generator.setShading(false);
					break;
				case OPT_SQLITE_CACHEWORLDROW:
					// This option is recognised for backward compatibility.
					// Tests with a (large) world on SSD and on HDD showed a performance decrease
					// on all map sizes with this option enabled.
					// (Next: print a message when this option is used.
					//  Later: remove it completely)
					break;
				case OPT_PROGRESS_INDICATOR:
					generator.enableProgressIndicator();
					break;
				case 'a': {
						istringstream iss;
						iss.str(optarg);
						int miny;
						iss >> miny;
						generator.setMinY(miny);
					}
					break;
				case 'c': {
						istringstream iss;
						iss.str(optarg);
						int maxy;
						iss >> maxy;
						generator.setMaxY(maxy);
					}
					break;
				case OPT_CHUNKSIZE : {
						istringstream iss;
						iss.str(optarg);
						int size;
						iss >> size;
						if (iss.fail() || size < 0) {
							std::cerr << "Invalid chunk size (" << optarg << ")" << std::endl;
							usage();
							exit(1);
						}
						generator.setChunkSize(size);
					}
					break;
				case OPT_SCALEFACTOR: {
						istringstream arg;
						arg.str(optarg);
						int one;
						char colon;
						int factor = 1;
						arg >> one >> std::ws;
						if (arg.fail() || one != 1) {
							std::cerr << "Invalid scale factor specification (" << optarg << ") - expected: 1:<n>" << std::endl;
							exit(1);
						}
						if (!arg.eof()) {
							arg >> colon >> factor >> std::ws;
							if (arg.fail() || colon != ':' || factor<0 || !arg.eof()) {
								std::cerr << "Invalid scale factor specification (" << optarg << ") - expected: 1:<n>" << std::endl;
								usage();
								exit(1);
							}
							if (factor != 1 && factor != 2 && factor != 4 && factor != 8 && factor != 16) {
								std::cerr << "Scale factor must be 1:1, 1:2, 1:4, 1:8 or 1:16" << std::endl;
								exit(1);
							}
						}
						generator.setScaleFactor(factor);
					}
					break;
				case 't': {
						istringstream tilesize;
						tilesize.str(strlower(optarg));
						if (tilesize.str() == "block") {
							generator.setTileSize(BLOCK_SIZE, BLOCK_SIZE);
							generator.setTileOrigin(TILECORNER_AT_WORLDCENTER, TILECORNER_AT_WORLDCENTER);
							}
						else if (tilesize.str() == "chunk") {
							generator.setTileSize(TILESIZE_CHUNK, TILESIZE_CHUNK);
							generator.setTileOrigin(TILECENTER_AT_CHUNKCENTER, TILECENTER_AT_CHUNKCENTER);
							}
						else {
							int size, border;
							char c;
							tilesize >> size;
							if (tilesize.fail() || size<0) {
								std::cerr << "Invalid tile size specification (" << optarg << ")" << std::endl;
								usage();
								exit(1);
							}
							generator.setTileSize(size, size);
							tilesize >> c >> border;
							if (!tilesize.fail()) {
								if (c != '+' || border < 1) {
									std::cerr << "Invalid tile border size specification (" << optarg << ")" << std::endl;
									usage();
									exit(1);
								}
								generator.setTileBorderSize(border);
							}
						}
					}
					break;
				case 'T': {
						bool origin = long_options[option_index].name[4] == 'o';
						istringstream iss;
						iss.str(strlower(optarg));
						NodeCoord coord;
						if (iss.str() == "world") {
							if (origin)
								generator.setTileOrigin(TILECORNER_AT_WORLDCENTER, TILECORNER_AT_WORLDCENTER);
							else
								generator.setTileCenter(TILECENTER_AT_WORLDCENTER, TILECENTER_AT_WORLDCENTER);
						}
						else if (iss.str() == "map") {
							if (origin)
								generator.setTileOrigin(TILECORNER_AT_MAPCENTER, TILECORNER_AT_MAPCENTER);
							else
								generator.setTileCenter(TILECENTER_AT_MAPCENTER, TILECENTER_AT_MAPCENTER);
						}
						else {
							bool result = true;
							if (!parseCoordinates(iss, coord, 2, 0, ',')) {
								iss.str(optarg);
								result = parseCoordinates(iss, coord, 2, 0, ':');
							}
							if (result) {
								if (origin) {
									convertBlockToNodeCoordinates(coord, 0, 2);
									generator.setTileOrigin(coord.x(), coord.y());
								}
								else {
									convertBlockToNodeCoordinates(coord, 8, 2);
									generator.setTileCenter(coord.x(), coord.y());
								}
							}
							else {
								std::cerr << "Invalid " << long_options[option_index].name << " parameter (" << optarg << ")" << std::endl;
								usage();
								exit(1);
							}
						}
					}
					break;
				case 'G':
					if (long_options[option_index].name[0] == 'f') {
						// '--forcegeometry'
						// Old behavior - for compatibility.
						generator.setShrinkGeometry(false);
						setFixedOrShrinkGeometry = true;
						if (!foundGeometrySpec)
							generator.setBlockGeometry(true);
					}
					else if (optarg && *optarg) {
						for (char *c = optarg; *c; c++) {
							*c = tolower(*c);
							if (*c == ',') *c = ' ';
						}
						istringstream iss;
						iss.str(optarg);
						string flag;
						iss >> std::skipws >> flag;
						while (!iss.fail()) {
							if (flag == "")
								(void) true;	// Empty flag - ignore
							else if (flag == "pixel")
								generator.setBlockGeometry(false);
							else if (flag == "block")
								generator.setBlockGeometry(true);
							else if (flag == "fixed")
								generator.setShrinkGeometry(false);
							else if (flag == "shrink")
								generator.setShrinkGeometry(true);
							else {
								std::cerr << "Invalid geometry mode flag '" << flag << "'" << std::endl;
								usage();
								exit(1);
							}
							if (flag == "fixed" || flag == "shrink")
								setFixedOrShrinkGeometry = true;
							iss >> flag;
						}
					}
					foundGeometrySpec = true;
					break;
				case 'g': {
						istringstream iss;
						iss.str(optarg);
						NodeCoord coord1;
						NodeCoord coord2;
						bool legacy;
						FuzzyBool center = FuzzyBool::Maybe;
						if (long_options[option_index].name[0] == 'c' && long_options[option_index].name[1] == 'e')
							center = FuzzyBool::Yes;
						if (long_options[option_index].name[0] == 'c' && long_options[option_index].name[1] == 'o')
							center = FuzzyBool::No;
						if (!parseMapGeometry(iss, coord1, coord2, legacy, center)) {
							std::cerr << "Invalid geometry specification '" << optarg << "'" << std::endl;
							usage();
							exit(1);
						}
						// Set defaults
						if (!foundGeometrySpec) {
							if (long_options[option_index].name[0] == 'g' && legacy) {
								// Compatibility when using the option 'geometry'
								generator.setBlockGeometry(true);
								generator.setShrinkGeometry(true);
							}
							else {
								generator.setBlockGeometry(false);
								generator.setShrinkGeometry(false);
							}
							setFixedOrShrinkGeometry = true;
						}
						if (!setFixedOrShrinkGeometry) {
							// Special treatement is needed, because:
							// - without any -[...]geometry option, default is shrink
							// - with    any -[...]geometry option, default is fixed
							generator.setShrinkGeometry(false);
							setFixedOrShrinkGeometry = true;
						}
						generator.setGeometry(coord1, coord2);
						foundGeometrySpec = true;
					}
					break;
				case OPT_DRAW_OBJECT: {
						TileGenerator::DrawObject drawObject;
						drawObject.world = long_options[option_index].name[4] != 'm';
						char object = long_options[option_index].name[4 + (drawObject.world ? 0 : 3)];
						switch (object) {
						case 'p' :
							drawObject.type = TileGenerator::DrawObject::Point;
							break;
						case 'l' :
							drawObject.type = TileGenerator::DrawObject::Line;
							break;
						case 'r' :
							drawObject.type = TileGenerator::DrawObject::Rectangle;
							break;
						case 'e' :
						case 'c' :
							drawObject.type = TileGenerator::DrawObject::Ellipse;
							break;
						case 'a' :
							drawObject.type = TileGenerator::DrawObject::Line;
							break;
						case 't' :
							drawObject.type = TileGenerator::DrawObject::Text;
							break;
						default :
							std::cerr << "Internal error: unrecognised object ("
								<< long_options[option_index].name
								<< ")" << std::endl;
							exit(1);
							break;
						}

						istringstream iss;
						iss.str(optarg);
						NodeCoord coord1;
						NodeCoord coord2;
						NodeCoord dimensions;
						FuzzyBool needDimensions;
						bool legacy;
						bool centered;

						if (object == 'p' || object == 't')
							needDimensions = FuzzyBool::No;
						else
							needDimensions = FuzzyBool::Yes;
						if (!parseGeometry(iss, coord1, coord2, dimensions, legacy, centered, 2, needDimensions)) {
							std::cerr << "Invalid drawing geometry specification for "
								<< long_options[option_index].name
								<< " '" << optarg << "'" << std::endl;
							usage();
							exit(1);
						}
						bool haveCoord2 = coord2.dimension[0] != NodeCoord::Invalid
							&& coord2.dimension[1] != NodeCoord::Invalid;
						bool haveDimensions = dimensions.dimension[0] != NodeCoord::Invalid
							&& dimensions.dimension[1] != NodeCoord::Invalid;

						if (object == 'p' || object == 't') {
							for (int i = 0; i < 2; i++)
								if (coord1.isBlock[i]) {
									coord1.dimension[i] *= 16;
									coord1.isBlock[i] = false;
								}
							drawObject.setCenter(coord1);
							drawObject.setDimensions(NodeCoord(1,1,1));
						}
						else {
							if (haveDimensions) {
								if (centered)
									drawObject.setCenter(coord1);
								else
									drawObject.setCorner1(coord1);
								drawObject.setDimensions(dimensions);
							}
							else if (haveCoord2) {
								drawObject.setCorner1(coord1);
								drawObject.setCorner2(coord2);
							}
							else {
#ifdef DEBUG
								assert(!haveDimensions && !haveCoord2);
#else
								break;
#endif
							}
						}

						string colorStr;
						iss >> std::ws >> colorStr;
						if (iss.fail()) {
							std::cerr << "Missing color for "
								<< long_options[option_index].name
								<< " '" << optarg << "'" << std::endl;
							usage();
							exit(1);
						}
						drawObject.color = colorStr;

						if (object == 't') {
							iss >> std::ws;
							std::string localizedText;
							std::getline(iss, localizedText);
							if (localizedText.empty() || iss.fail()) {
								std::cerr << "Invalid or missing text for "
									<< long_options[option_index].name
									<< " '" << optarg << "'" << std::endl;
								usage();
								exit(1);
							}
							drawObject.text = charConvUTF8->convert(localizedText);
						}

						generator.drawObject(drawObject);
						if (object == 'a') {
							if (drawObject.haveCenter) {
								std::cerr << "Arrow cannot use a centered dimension."
								    << " Specify at least one corner." << std::endl;
								exit(1);
							}
							bool useDimensions = drawObject.haveDimensions;

							if (drawObject.haveDimensions)
								convertDimensionToCornerCoordinates(drawObject.corner1, drawObject.corner2, drawObject.dimensions, 2);
							double angle, length;
							convertCartesianToPolarCoordinates(drawObject.corner1, drawObject.corner2, angle, length);
							convertPolarToCartesianCoordinates(drawObject.corner1, drawObject.corner2, angle + DRAW_ARROW_ANGLE, DRAW_ARROW_LENGTH);
							if (useDimensions) {
								convertCornerToDimensionCoordinates(drawObject.corner1, drawObject.corner2, drawObject.dimensions, 2);
								drawObject.haveDimensions = useDimensions;
							}
							generator.drawObject(drawObject);
							convertPolarToCartesianCoordinates(drawObject.corner1, drawObject.corner2, angle - DRAW_ARROW_ANGLE, DRAW_ARROW_LENGTH);
							if (useDimensions) {
								convertCornerToDimensionCoordinates(drawObject.corner1, drawObject.corner2, drawObject.dimensions, 2);
								drawObject.haveDimensions = useDimensions;
							}
							generator.drawObject(drawObject);
						}
					}
					break;
				case 'd':
					generator.setBackend(strlower(optarg));
					break;
				default:
					exit(1);
			}
		}
	} catch(std::runtime_error e) {
		std::cout<<"Command-line error: "<<e.what()<<std::endl;
		return 1;
	}

	try {
		if (heightMap) {
			parseDataFile(generator, input, heightMapNodesFile, heightMapNodesDefaultFile, &TileGenerator::parseHeightMapNodesFile);
			if (loadHeightMapColorsFile)
				parseDataFile(generator, input, heightMapColorsFile, heightMapColorsDefaultFile, &TileGenerator::parseHeightMapColorsFile);
		}
		else {
			parseDataFile(generator, input, nodeColorsFile, nodeColorsDefaultFile, &TileGenerator::parseNodeColorsFile);
		}
		generator.generate(input, output);
	} catch(std::runtime_error e) {
		std::cout<<"Exception: "<<e.what()<<std::endl;
		return 1;
	}
	return 0;
}

