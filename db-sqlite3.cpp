#include "db-sqlite3.h"
#include <stdexcept>
#include "porting.h"
#include "types.h"

#define BLOCKPOSLIST_STATEMENT	"SELECT pos, rowid FROM blocks"
#define BLOCK_STATEMENT_POS	"SELECT pos, data FROM blocks WHERE pos == ?"
#define BLOCK_STATEMENT_ROWID	"SELECT pos, data FROM blocks WHERE rowid == ?"


DBSQLite3::DBSQLite3(const std::string &mapdir) :
	m_blocksQueriedCount(0),
	m_blocksReadCount(0),
	m_blockPosListStatement(NULL),
	m_blockOnPosStatement(NULL),
	m_blockOnRowidStatement(NULL)
{
	
	std::string db_name = mapdir + "map.sqlite";
	if (sqlite3_open_v2(db_name.c_str(), &m_db, SQLITE_OPEN_READONLY | SQLITE_OPEN_PRIVATECACHE, 0) != SQLITE_OK) {
		throw std::runtime_error(std::string(sqlite3_errmsg(m_db)) + ", Database file: " + db_name);
	}
	if (SQLITE_OK != sqlite3_prepare_v2(m_db, BLOCKPOSLIST_STATEMENT, sizeof(BLOCKPOSLIST_STATEMENT)-1, &m_blockPosListStatement, 0)) {
		throw std::runtime_error("Failed to prepare SQL statement (blockPosListStatement)");
	}
	if (SQLITE_OK != sqlite3_prepare_v2(m_db, BLOCK_STATEMENT_POS, sizeof(BLOCK_STATEMENT_POS)-1, &m_blockOnPosStatement, 0)) {
		throw std::runtime_error("Failed to prepare SQL statement (blockOnPosStatement)");
	}
	if (SQLITE_OK != sqlite3_prepare_v2(m_db, BLOCK_STATEMENT_ROWID, sizeof(BLOCK_STATEMENT_ROWID)-1, &m_blockOnRowidStatement, 0)) {
		throw std::runtime_error("Failed to prepare SQL statement (blockOnRowidStatement)");
	}
}

DBSQLite3::~DBSQLite3() {
	if (m_blockPosListStatement) sqlite3_finalize(m_blockPosListStatement);
	if (m_blockOnPosStatement) sqlite3_finalize(m_blockOnPosStatement);
	if (m_blockOnRowidStatement) sqlite3_finalize(m_blockOnRowidStatement);
	sqlite3_close(m_db);
}

int DBSQLite3::getBlocksReadCount(void)
{
	return m_blocksReadCount;
}

int DBSQLite3::getBlocksQueriedCount(void)
{
	return m_blocksQueriedCount;
}

const DB::BlockPosList &DBSQLite3::getBlockPosList() {
	m_BlockPosList.clear();
	int result = 0;
	while (true) {
		result = sqlite3_step(m_blockPosListStatement);
		if(result == SQLITE_ROW) {
			sqlite3_int64 blocknum = sqlite3_column_int64(m_blockPosListStatement, 0);
			sqlite3_int64 rowid = sqlite3_column_int64(m_blockPosListStatement, 1);
			m_BlockPosList.push_back(BlockPos(blocknum, rowid));
		} else if (result == SQLITE_BUSY) // Wait some time and try again
			sleepMs(10);
		else
			break;
	}
	sqlite3_reset(m_blockPosListStatement);
	return m_BlockPosList;
}


DB::Block DBSQLite3::getBlockOnPos(const BlockPos &pos)
{
	Block block(pos,reinterpret_cast<const unsigned char *>(""));
	int result = 0;
	
	m_blocksQueriedCount++;

	sqlite3_stmt *statement;

	if (pos.databasePosIdIsValid()) {
		statement = m_blockOnRowidStatement;
		sqlite3_bind_int64(m_blockOnRowidStatement, 1, pos.databasePosId());
	}
	else {
		statement = m_blockOnPosStatement;
		sqlite3_bind_int64(m_blockOnPosStatement, 1, pos.databasePosI64());
	}

	while (true) {
		result = sqlite3_step(statement);
		if(result == SQLITE_ROW) {
			const unsigned char *data = reinterpret_cast<const unsigned char *>(sqlite3_column_blob(statement, 1));
			int size = sqlite3_column_bytes(statement, 1);
			block = Block(pos, ustring(data, size));
			m_blocksReadCount++;
			break;
		} else if (result == SQLITE_BUSY) { // Wait some time and try again
			sleepMs(10);
		} else {
			break;
		}
	}
	sqlite3_reset(statement);

	return block;
}

