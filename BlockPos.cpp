
#include <BlockPos.h>

void BlockPos::operator=(const std::string &s)
{
	m_id = INT64_MIN;
	std::istringstream is(s);
	if (isdigit(is.peek()) || is.peek() == '-' || is.peek() == '+') {
		int64_t ipos;
		is >> ipos;
		if (is.fail() || !is.eof()) {
			throw std::runtime_error(std::string("Failed to decode i64 (minetest) coordinate string from database (") + s + ")" );
		}
		operator=(ipos);
		m_strFormat = I64;
	}
	else if (is.peek() == 'a') {
		// Freeminer new format (a<x>,<y>,<z>)
		char c1, c2;
		is >> c1;
		is >> x();
		is >> c1;
		is >> y();
		is >> c2;
		is >> z();
		if (is.fail() || !is.eof() || c1 != ',' || c2 != ',') {
			throw std::runtime_error(std::string("Failed to decode axyz (freeminer) coordinate string from database (") + s + ")" );
		}
		m_strFormat = AXYZ;
	}
	else {
		throw std::runtime_error(std::string("Failed to detect format of coordinate string from database (") + s + ")" );
	}
}

std::string BlockPos::databasePosStrFmt(StrFormat format) const
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
		os << x() << ',' << y() << ',' << z();
		break;
	case AXYZ:
		os << 'a' << x() << ',' << y() << ',' << z();
		break;
	case STRFORMAT_MAX:
		break;
	}
	return os.str();
}

void BlockPosIterator::printState(const char *message, FILE *file)
{
	fprintf(file, "[%s]  Value: %d,%d,%d;  Range: %d,%d,%d .. %d,%d,%d;  State: %d\n",
		message,
		m_value.x(), m_value.y(), m_value.z(),
		m_start.x(), m_start.y(), m_start.z(),
		m_end.x(), m_end.y(), m_end.z(),
		m_state);
}

void BlockPosIterator::breakDim(int i, int step)
{
	if (!step) return;
	if (i == 0) {
		m_value.dimension[1] = step < 0 ? m_start.dimension[1] : m_end.dimension[1];
		m_value.dimension[0] = step < 0 ? m_start.dimension[0] : m_end.dimension[0];
	}
	else if (i == 1) {
		m_value.dimension[1] = step < 0 ? m_start.dimension[1] : m_end.dimension[1];
	}
	else if (i == 2) {
		m_value.dimension[1] = step < 0 ? m_start.dimension[1] : m_end.dimension[1];
		m_value.dimension[0] = step < 0 ? m_start.dimension[0] : m_end.dimension[0];
		m_value.dimension[2] = step < 2 ? m_start.dimension[2] : m_end.dimension[2];
	}
}

void BlockPosIterator::stepForward(void)
{
	switch (m_state) {
	case Start:
		m_state = Middle;
		m_value = m_start;
		break;
	case End:
		break;
	case Middle:
		stepDim(1, 1)
		|| stepDim(0, 1)
		|| stepDim(2, 1)
		|| (m_state = End);
		break;
	default:
		throw std::runtime_error("Internal error: BlockPosIterator: unexpected state");
	}
}


void BlockPosIterator::stepReverse(void)
{
	switch (m_state) {
	case Start:
		break;
	case End:
		m_state = Middle;
		m_value = m_end;
		break;
	case Middle:
		stepDim(1, -1)
		|| stepDim(0, -1)
		|| stepDim(2, -1)
		|| (m_state = Start);
		break;
	default:
		throw std::runtime_error("Internal error: BlockPosIterator: unexpected state");
	}
}

size_t NodeCoord::hash(void) const
{
	size_t hash = 0xd3adb33f;
	for (int i=0; i<3; i++)
		// Nothing too fancy...
		hash = ((hash << 8) | (hash >> 24)) ^ (dimension[i] ^ (isBlock[i] ? 0x50000000 : 0));
	return hash;
}

