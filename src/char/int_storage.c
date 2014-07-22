/**
 * This file is part of Hercules.
 * http://herc.ws - http://github.com/HerculesWS/Hercules
 *
 * Copyright (C) 2012-2015  Hercules Dev Team
 * Copyright (C)  Athena Dev Teams
 *
 * Hercules is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define HERCULES_CORE

#include "config/core.h" // GP_BOUND_ITEMS
#include "int_storage.h"

#include "char/char.h"
#include "char/inter.h"
#include "char/mapif.h"
#include "common/memmgr.h"
#include "common/mmo.h"
#include "common/nullpo.h"
#include "common/showmsg.h"
#include "common/socket.h"
#include "common/sql.h"
#include "common/strlib.h" // StringBuf

#include <stdio.h>
#include <stdlib.h>

#define STORAGE_MEMINC 16

struct inter_storage_interface inter_storage_s;
struct inter_storage_interface *inter_storage;

/// Save storage data to sql
int inter_storage_tosql(int account_id, struct storage_data* p)
{
	nullpo_ret(p);
	chr->memitemdata_to_sql(p->items, MAX_STORAGE, account_id, TABLE_STORAGE);
	return 0;
}

/// Load storage data to mem
int inter_storage_fromsql(int account_id, struct storage_data* p)
{
	StringBuf buf;
	char* data;
	int i;
	int j;

	nullpo_ret(p);
	memset(p, 0, sizeof(struct storage_data)); //clean up memory
	p->storage_amount = 0;

	// storage {`account_id`/`id`/`nameid`/`amount`/`equip`/`identify`/`refine`/`attribute`/`card0`/`card1`/`card2`/`card3`}
	StrBuf->Init(&buf);
	StrBuf->AppendStr(&buf, "SELECT `id`,`nameid`,`amount`,`equip`,`identify`,`refine`,`attribute`,`expire_time`,`bound`,`unique_id`");
	for( j = 0; j < MAX_SLOTS; ++j )
		StrBuf->Printf(&buf, ",`card%d`", j);
	StrBuf->Printf(&buf, " FROM `%s` WHERE `account_id`='%d' ORDER BY `nameid`", storage_db, account_id);

	if (SQL_ERROR == SQL->QueryStr(inter->sql_handle, StrBuf->Value(&buf)))
		Sql_ShowDebug(inter->sql_handle);

	StrBuf->Destroy(&buf);

	for (i = 0; i < MAX_STORAGE && SQL_SUCCESS == SQL->NextRow(inter->sql_handle); ++i) {
		struct item *item = &p->items[i];
		SQL->GetData(inter->sql_handle, 0, &data, NULL); item->id = atoi(data);
		SQL->GetData(inter->sql_handle, 1, &data, NULL); item->nameid = atoi(data);
		SQL->GetData(inter->sql_handle, 2, &data, NULL); item->amount = atoi(data);
		SQL->GetData(inter->sql_handle, 3, &data, NULL); item->equip = atoi(data);
		SQL->GetData(inter->sql_handle, 4, &data, NULL); item->identify = atoi(data);
		SQL->GetData(inter->sql_handle, 5, &data, NULL); item->refine = atoi(data);
		SQL->GetData(inter->sql_handle, 6, &data, NULL); item->attribute = atoi(data);
		SQL->GetData(inter->sql_handle, 7, &data, NULL); item->expire_time = (unsigned int)atoi(data);
		SQL->GetData(inter->sql_handle, 8, &data, NULL); item->bound = atoi(data);
		SQL->GetData(inter->sql_handle, 9, &data, NULL); item->unique_id = strtoull(data, NULL, 10);
		for( j = 0; j < MAX_SLOTS; ++j )
		{
			SQL->GetData(inter->sql_handle, 10+j, &data, NULL); item->card[j] = atoi(data);
		}
	}
	p->storage_amount = i;
	SQL->FreeResult(inter->sql_handle);

	ShowInfo("storage load complete from DB - id: %d (total: %d)\n", account_id, p->storage_amount);
	return 1;
}

/**
 * Saves guild_storage data to SQL
 * Uses:
 *    p->guild_id
 *    p->storage_amount
 *    p->items
 * @retval Number of errors encountered when saving
 **/
int inter_storage_guild_storage_tosql(struct guild_storage* p)
{
	int err_count = 0;
	nullpo_ret(p);

	err_count = chr->memitemdata_to_sql(p->items, p->storage_amount, p->guild_id, TABLE_GUILD_STORAGE);
	if (err_count)
		ShowError("guild_storage_tosql: Couldn't save storage item data! (GID: %d)\n", p->guild_id);
	else
		ShowInfo("guild storage save to DB - guild: %d\n", p->guild_id);

	return err_count;
}

/**
 * Loads `guild_storage` data to memory
 *  Allocates p->item using aCalloc, nothing is allocated in case of failure
 * @param p Should be allocated using aCalloc and freed by the caller
 * @retval 0 Success
 **/
int inter_storage_guild_storage_fromsql(int guild_id, struct guild_storage* p)
{
	StringBuf buf;
	char *data;
	uint64 num_rows;
	int i, j;

	nullpo_retr(1,p);

	p->guild_id = guild_id;

	if( SQL_ERROR == SQL->Query(inter->sql_handle, "SELECT `guild_id` FROM `%s` WHERE `guild_id`='%d'", guild_db, guild_id) ) {
		Sql_ShowDebug(inter->sql_handle);
		return 1;
	}
	SQL->FreeResult(inter->sql_handle);

	// storage {`guild_id`/`id`/`nameid`/`amount`/`equip`/`identify`/`refine`/`attribute`/`card0`/`card1`/`card2`/`card3`}
	StrBuf->Init(&buf);
	StrBuf->AppendStr(&buf, "SELECT `id`,`nameid`,`amount`,`equip`,`identify`,`refine`,`attribute`,`bound`,`unique_id`");
	for( j = 0; j < MAX_SLOTS; ++j )
		StrBuf->Printf(&buf, ",`card%d`", j);
	StrBuf->Printf(&buf, " FROM `%s` WHERE `guild_id`='%d' ORDER BY `nameid`", guild_storage_db, guild_id);

	if( SQL_ERROR == SQL->QueryStr(inter->sql_handle, StrBuf->Value(&buf)))
		Sql_ShowDebug(inter->sql_handle);

	StrBuf->Destroy(&buf);
	num_rows = SQL->NumRows(inter->sql_handle);
	if( num_rows > USHRT_MAX ) { // Bigger values would overflow p->storage_amount
		ShowError("guild_storage_fromsql: num_rows is too big!\n");
		SQL->FreeResult(inter->sql_handle);
		return 1;
	}

	p->items = aCalloc((uint16)num_rows, sizeof(p->items[0]));
	for (i = 0; i < num_rows && SQL_SUCCESS == SQL->NextRow(inter->sql_handle); ++i) {
		SQL->GetData(inter->sql_handle, 0, &data, NULL); p->items[i].id = atoi(data);
		SQL->GetData(inter->sql_handle, 1, &data, NULL); p->items[i].nameid = atoi(data);
		SQL->GetData(inter->sql_handle, 2, &data, NULL); p->items[i].amount = atoi(data);
		SQL->GetData(inter->sql_handle, 3, &data, NULL); p->items[i].equip = atoi(data);
		SQL->GetData(inter->sql_handle, 4, &data, NULL); p->items[i].identify = atoi(data);
		SQL->GetData(inter->sql_handle, 5, &data, NULL); p->items[i].refine = atoi(data);
		SQL->GetData(inter->sql_handle, 6, &data, NULL); p->items[i].attribute = atoi(data);
		SQL->GetData(inter->sql_handle, 7, &data, NULL); p->items[i].bound = atoi(data);
		SQL->GetData(inter->sql_handle, 8, &data, NULL); p->items[i].unique_id = strtoull(data, NULL, 10);
 		p->items[i].expire_time = 0;

		for( j = 0; j < MAX_SLOTS; ++j ) {
			SQL->GetData(inter->sql_handle, 9+j, &data, NULL); p->items[i].card[j] = atoi(data);
		}
	}
	p->storage_amount = i;
	SQL->FreeResult(inter->sql_handle);

	if (i < num_rows) {
		struct item *temp;
		temp = aRealloc(p->items, sizeof(p->items[0])*i);
		if(temp)
			p->items = temp;
	}
	ShowInfo("guild storage load complete from DB - id: %d (total: %d)\n", guild_id, p->storage_amount);
	return 0;
}

//---------------------------------------------------------
// storage data initialize
int inter_storage_sql_init(void)
{
	return 1;
}
// storage data finalize
void inter_storage_sql_final(void)
{
	return;
}

// q?f[^?
int inter_storage_delete(int account_id)
{
	if( SQL_ERROR == SQL->Query(inter->sql_handle, "DELETE FROM `%s` WHERE `account_id`='%d'", storage_db, account_id) )
		Sql_ShowDebug(inter->sql_handle);
	return 0;
}
int inter_storage_guild_storage_delete(int guild_id)
{
	if( SQL_ERROR == SQL->Query(inter->sql_handle, "DELETE FROM `%s` WHERE `guild_id`='%d'", guild_storage_db, guild_id) )
		Sql_ShowDebug(inter->sql_handle);
	return 0;
}

//---------------------------------------------------------
// packet from map server

/**
 * Sends loaded guild storage to fd
 * 0x3818 <len>.W <account id>.L <guild id>.L <flag>.B <size>.W {<item>.P}*<size>
 * 0x3818 <len>.W <account id>.L <guild id>.W
 *
 * <flag> 0 Don't open storage
 * <flag> 1 Open storage
 * <guild id> 0 Failed
 *
 * @retval 0 success
 **/
int mapif_load_guild_storage(int fd, int account_id, int guild_id, char flag)
{
	if( SQL_ERROR == SQL->Query(inter->sql_handle, "SELECT `guild_id` FROM `%s` WHERE `guild_id`='%d'", guild_db, guild_id) )
		Sql_ShowDebug(inter->sql_handle);
	else if (SQL->NumRows(inter->sql_handle) > 0) {
		// guild exists
		struct guild_storage *gs = aCalloc(1, sizeof(struct guild_storage));

		if (!inter_storage->guild_storage_fromsql(guild_id, gs)) {
			int size = (gs->items)? (15+ (sizeof(struct item)*gs->storage_amount) ) : 15;
			WFIFOHEAD(fd, size);
			WFIFOW(fd,0) = 0x3818;
			WFIFOW(fd,2) = size;
			WFIFOL(fd,4) = account_id;
			WFIFOL(fd,8) = guild_id;
			WFIFOB(fd,12) = flag;
			WFIFOW(fd,13) = gs->storage_amount;
			if( gs->items )
				memcpy(WFIFOP(fd,15), gs->items, sizeof(struct item)*gs->storage_amount);
			WFIFOSET(fd, WFIFOW(fd,2));
			aFree(gs->items);
			return 0;
		}
		aFree(gs);
	}
	// guild does not exist or there was an error
	SQL->FreeResult(inter->sql_handle);
	WFIFOHEAD(fd, 10);
	WFIFOW(fd,0) = 0x3818;
	WFIFOW(fd,2) = 12;
	WFIFOL(fd,4) = account_id;
	WFIFOW(fd,8) = 0;
	WFIFOSET(fd, 12);
	return 0;
}

int mapif_save_guild_storage_ack(int fd, int account_id, int guild_id, int fail)
{
	WFIFOHEAD(fd,11);
	WFIFOW(fd,0)=0x3819;
	WFIFOL(fd,2)=account_id;
	WFIFOL(fd,6)=guild_id;
	WFIFOB(fd,10)=fail;
	WFIFOSET(fd,11);
	return 0;
}

//---------------------------------------------------------
// packet from map server

int mapif_parse_LoadGuildStorage(int fd)
{
	RFIFOHEAD(fd);
	mapif->load_guild_storage(fd,RFIFOL(fd,2),RFIFOL(fd,6),1);
	return 0;
}

/**
 * Saves a storage that was received via 0x3019
 * 0x3019 <len>.W <account id>.L <guild id>.L <storage_amount>.W {<item>.P}*<storage_amount>
 **/
int mapif_parse_SaveGuildStorage(int fd) {
	int guild_id, len, storage_amount;
	int expected;

	RFIFOHEAD(fd);
	guild_id = RFIFOL(fd,8);
	storage_amount = RFIFOW(fd,12);
	len = RFIFOW(fd,2);

	expected = 14 + sizeof(struct item)*storage_amount;
	if (expected != len) {
		ShowError("mapif_parse_SaveGuildStorage: data size mismatch: %d != %d\n", len, expected);
	} else {
		if (SQL_ERROR == SQL->Query(inter->sql_handle, "SELECT `guild_id` FROM `%s` WHERE `guild_id`='%d'", guild_db, guild_id)) {
			Sql_ShowDebug(inter->sql_handle);
		} else if(SQL->NumRows(inter->sql_handle) > 0) {
			// guild exists
			struct guild_storage *gs;
			SQL->FreeResult(inter->sql_handle);

			gs = aCalloc(1, sizeof(struct guild_storage));
			gs->items = (struct item *)RFIFOP(fd, 14);
			gs->storage_amount = storage_amount;
			gs->guild_id = guild_id;
			inter_storage->guild_storage_tosql(gs);

			mapif->save_guild_storage_ack(fd, RFIFOL(fd,4), guild_id, 0);
			aFree(gs);
			return 0;
		}
		SQL->FreeResult(inter->sql_handle);
	}
	mapif->save_guild_storage_ack(fd, RFIFOL(fd,4), guild_id, 1);
	return 0;
}

int mapif_itembound_ack(int fd, int aid, int guild_id)
{
#ifdef GP_BOUND_ITEMS
	WFIFOHEAD(fd,8);
	WFIFOW(fd,0) = 0x3856;
	WFIFOL(fd,2) = aid;/* the value is not being used, drop? */
	WFIFOW(fd,6) = guild_id;
	WFIFOSET(fd,8);
#endif
	return 0;
}

//------------------------------------------------
//Guild bound items pull for offline characters [Akinari]
//Revised by [Mhalicot]
//------------------------------------------------
int mapif_parse_ItemBoundRetrieve_sub(int fd)
{
#ifdef GP_BOUND_ITEMS
	StringBuf buf;
	SqlStmt* stmt;
	struct item item;
	int j, i=0, s=0, bound_qt=0;
	struct item items[MAX_INVENTORY];
	unsigned int bound_item[MAX_INVENTORY] = {0};
	int char_id = RFIFOL(fd,2);
	int aid = RFIFOL(fd,6);
	int guild_id = RFIFOW(fd,10);

	StrBuf->Init(&buf);
	StrBuf->AppendStr(&buf, "SELECT `id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `bound`, `unique_id`");
	for( j = 0; j < MAX_SLOTS; ++j )
		StrBuf->Printf(&buf, ", `card%d`", j);
	StrBuf->Printf(&buf, " FROM `%s` WHERE `char_id`='%d' AND `bound` = '%d'",inventory_db,char_id,IBT_GUILD);

	stmt = SQL->StmtMalloc(inter->sql_handle);
	if( SQL_ERROR == SQL->StmtPrepareStr(stmt, StrBuf->Value(&buf))
	||  SQL_ERROR == SQL->StmtExecute(stmt) )
	{
		Sql_ShowDebug(inter->sql_handle);
		SQL->StmtFree(stmt);
		StrBuf->Destroy(&buf);
		return 1;
	}

	memset(&item, 0, sizeof(item));
	SQL->StmtBindColumn(stmt, 0, SQLDT_INT,       &item.id,          0, NULL, NULL);
	SQL->StmtBindColumn(stmt, 1, SQLDT_SHORT,     &item.nameid,      0, NULL, NULL);
	SQL->StmtBindColumn(stmt, 2, SQLDT_SHORT,     &item.amount,      0, NULL, NULL);
	SQL->StmtBindColumn(stmt, 3, SQLDT_USHORT,    &item.equip,       0, NULL, NULL);
	SQL->StmtBindColumn(stmt, 4, SQLDT_CHAR,      &item.identify,    0, NULL, NULL);
	SQL->StmtBindColumn(stmt, 5, SQLDT_CHAR,      &item.refine,      0, NULL, NULL);
	SQL->StmtBindColumn(stmt, 6, SQLDT_CHAR,      &item.attribute,   0, NULL, NULL);
	SQL->StmtBindColumn(stmt, 7, SQLDT_UINT,      &item.expire_time, 0, NULL, NULL);
	SQL->StmtBindColumn(stmt, 8, SQLDT_UCHAR,     &item.bound,       0, NULL, NULL);
	SQL->StmtBindColumn(stmt, 9, SQLDT_UINT64,    &item.unique_id,   0, NULL, NULL);
	for( j = 0; j < MAX_SLOTS; ++j )
		SQL->StmtBindColumn(stmt, 10+j, SQLDT_SHORT, &item.card[j], 0, NULL, NULL);

	while( SQL_SUCCESS == SQL->StmtNextRow(stmt)) {
		Assert_retb(i < MAX_INVENTORY);
		memcpy(&items[i],&item,sizeof(struct item));
		i++;
	}
	SQL->FreeResult(inter->sql_handle);

	if(!i) { //No items found - No need to continue
		StrBuf->Destroy(&buf);
		SQL->StmtFree(stmt);
		return 0;
	}

	//First we delete the character's items
	StrBuf->Clear(&buf);
	StrBuf->Printf(&buf, "DELETE FROM `%s` WHERE",inventory_db);
	for(j=0; j<i; j++) {
		if( j )
			StrBuf->AppendStr(&buf, " OR");

		StrBuf->Printf(&buf, " `id`=%d",items[j].id);

		if( items[j].bound && items[j].equip ) {
			// Only the items that are also stored in `char` `equip`
			if( items[j].equip&EQP_HAND_R
			||  items[j].equip&EQP_HAND_L
			||  items[j].equip&EQP_HEAD_TOP
			||  items[j].equip&EQP_HEAD_MID
			||  items[j].equip&EQP_HEAD_LOW
			||  items[j].equip&EQP_GARMENT
			) {
				bound_item[bound_qt] = items[j].equip;
				bound_qt++;
			}
		}
	}

	if( SQL_ERROR == SQL->StmtPrepareStr(stmt, StrBuf->Value(&buf))
	||  SQL_ERROR == SQL->StmtExecute(stmt) )
	{
		Sql_ShowDebug(inter->sql_handle);
		SQL->StmtFree(stmt);
		StrBuf->Destroy(&buf);
		return 1;
	}

	// Removes any view id that was set by an item that was removed
	if( bound_qt ) {

#define CHECK_REMOVE(var,mask,token) do { /* Verifies equip bitmasks (see item.equip) and handles the sql statement */ \
	if ((var)&(mask)) { \
		if ((var) != (mask) && s) StrBuf->AppendStr(&buf, ","); \
		StrBuf->AppendStr(&buf,"`"#token"`='0'"); \
		(var) &= ~(mask); \
		s++; \
	} \
} while(0)

		StrBuf->Clear(&buf);
		StrBuf->Printf(&buf, "UPDATE `%s` SET ", char_db);
		for( j = 0; j < bound_qt; j++ ) {
			// Equips can be at more than one slot at the same time
			CHECK_REMOVE(bound_item[j],EQP_HAND_R,weapon);
			CHECK_REMOVE(bound_item[j],EQP_HAND_L,shield);
			CHECK_REMOVE(bound_item[j],EQP_HEAD_TOP,head_top);
			CHECK_REMOVE(bound_item[j],EQP_HEAD_MID,head_mid);
			CHECK_REMOVE(bound_item[j],EQP_HEAD_LOW,head_bottom);
			CHECK_REMOVE(bound_item[j],EQP_GARMENT,robe);
		}
		StrBuf->Printf(&buf, " WHERE `char_id`='%d'", char_id);

		if( SQL_ERROR == SQL->StmtPrepareStr(stmt, StrBuf->Value(&buf))
		||  SQL_ERROR == SQL->StmtExecute(stmt) )
		{
			Sql_ShowDebug(inter->sql_handle);
			SQL->StmtFree(stmt);
			StrBuf->Destroy(&buf);
			return 1;
		}
#undef CHECK_REMOVE
	}

	//Now let's update the guild storage with those deleted items
	/// TODO/FIXME:
	/// This approach is basically the same as the one from chr->memitemdata_to_sql, but
	/// the latter compares current database values and this is not needed in this case
	/// maybe sometime separate chr->memitemdata_to_sql into different methods in order to use
	/// call that function here as well [Panikon]
	StrBuf->Clear(&buf);
	StrBuf->Printf(&buf,"INSERT INTO `%s` (`guild_id`,`nameid`,`amount`,`equip`,`identify`,`refine`,"
						"`attribute`,`expire_time`,`bound`,`unique_id`",
					guild_storage_db);
	for( s = 0; s < MAX_SLOTS; ++s )
		StrBuf->Printf(&buf, ", `card%d`", s);
	StrBuf->AppendStr(&buf," ) VALUES ");

	for( j = 0; j < i; ++j ) {
		if( j )
			StrBuf->AppendStr(&buf, ",");

		StrBuf->Printf(&buf, "('%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%"PRIu64"'",
			guild_id, items[j].nameid, items[j].amount, items[j].equip, items[j].identify, items[j].refine,
			items[j].attribute, items[j].expire_time, items[j].bound, items[j].unique_id);
		for( s = 0; s < MAX_SLOTS; ++s )
			StrBuf->Printf(&buf, ", '%d'", items[j].card[s]);
		StrBuf->AppendStr(&buf, ")");
	}

	if( SQL_ERROR == SQL->StmtPrepareStr(stmt, StrBuf->Value(&buf))
	||  SQL_ERROR == SQL->StmtExecute(stmt) )
	{
		Sql_ShowDebug(inter->sql_handle);
		SQL->StmtFree(stmt);
		StrBuf->Destroy(&buf);
		return 1;
	}

	StrBuf->Destroy(&buf);
	SQL->StmtFree(stmt);

	//Finally reload storage and tell map we're done
	mapif->load_guild_storage(fd,aid,guild_id,0);

	// If character is logged in char, disconnect
	chr->disconnect_player(aid);
#endif
	return 0;
}

void mapif_parse_ItemBoundRetrieve(int fd)
{
	mapif->parse_ItemBoundRetrieve_sub(fd);
	/* tell map server the operation is over and it can unlock the storage */
	mapif->itembound_ack(fd,RFIFOL(fd,6),RFIFOW(fd,10));
}

int inter_storage_parse_frommap(int fd)
{
	RFIFOHEAD(fd);
	switch(RFIFOW(fd,0)){
		case 0x3018: mapif->parse_LoadGuildStorage(fd); break;
		case 0x3019: mapif->parse_SaveGuildStorage(fd); break;
#ifdef GP_BOUND_ITEMS
		case 0x3056: mapif->parse_ItemBoundRetrieve(fd); break;
#endif
		default:
			return 0;
	}
	return 1;
}

void inter_storage_defaults(void)
{
	inter_storage = &inter_storage_s;

	inter_storage->tosql = inter_storage_tosql;
	inter_storage->fromsql = inter_storage_fromsql;
	inter_storage->guild_storage_tosql = inter_storage_guild_storage_tosql;
	inter_storage->guild_storage_fromsql = inter_storage_guild_storage_fromsql;
	inter_storage->sql_init = inter_storage_sql_init;
	inter_storage->sql_final = inter_storage_sql_final;
	inter_storage->delete_ = inter_storage_delete;
	inter_storage->guild_storage_delete = inter_storage_guild_storage_delete;
	inter_storage->parse_frommap = inter_storage_parse_frommap;
}
