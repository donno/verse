/*
 *
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 * Contributor(s): Jiri Hnidek <jiri.hnidek@tul.cz>.
 *
 */

#define MONGO_HAVE_STDINT 1

#include <mongo.h>

#include "vs_main.h"
#include "vs_mongo_main.h"
#include "vs_mongo_taggroup.h"
#include "vs_node.h"
#include "vs_taggroup.h"
#include "vs_tag.h"

#include "v_common.h"


/**
 * \brief This function save current version o tag group to MongoDB
 */
static void vs_mongo_taggroup_save_version(struct VSTagGroup *tg,
		bson *bson_tg,
		uint32 version)
{
	bson bson_version;
	bson bson_tag;
	struct VBucket *bucket;
	struct VSTag *tag;
	char str_num[15];
	int item_id;

	bson_init(&bson_version);

	bson_append_int(&bson_version, "crc32", tg->crc32);

	bson_append_start_object(&bson_version, "tags");

	bucket = tg->tags.lb.first;
	while(bucket != NULL) {
		tag = (struct VSTag*)bucket->data;

		bson_init(&bson_tag);
		bson_append_int(&bson_tag, "data_type", tag->data_type);
		bson_append_int(&bson_tag, "count", tag->count);
		bson_append_int(&bson_tag, "custom_type", tag->custom_type);

		bson_append_start_array(&bson_tag, "data");
		switch(tag->data_type) {
		case VRS_VALUE_TYPE_UINT8:
			for(item_id = 0; item_id < tag->count; item_id++) {
				sprintf(str_num, "%d", item_id);
				bson_append_int(&bson_tag, str_num, ((uint8*)tag->value)[item_id]);
			}
			break;
		case VRS_VALUE_TYPE_UINT16:
			for(item_id = 0; item_id < tag->count; item_id++) {
				sprintf(str_num, "%d", item_id);
				bson_append_int(&bson_tag, str_num, ((uint16*)tag->value)[item_id]);
			}
			break;
		case VRS_VALUE_TYPE_UINT32:
			for(item_id = 0; item_id < tag->count; item_id++) {
				sprintf(str_num, "%d", item_id);
				bson_append_int(&bson_tag, str_num, ((uint32*)tag->value)[item_id]);
			}
			break;
		case VRS_VALUE_TYPE_UINT64:
			for(item_id = 0; item_id < tag->count; item_id++) {
				sprintf(str_num, "%d", item_id);
				bson_append_long(&bson_tag, str_num, ((uint64*)tag->value)[item_id]);
			}
			break;
		case VRS_VALUE_TYPE_REAL16:
			/* TODO */
			break;
		case VRS_VALUE_TYPE_REAL32:
			for(item_id = 0; item_id < tag->count; item_id++) {
				sprintf(str_num, "%d", item_id);
				bson_append_double(&bson_tag, str_num, ((float*)tag->value)[item_id]);
			}
			break;
		case VRS_VALUE_TYPE_REAL64:
			for(item_id = 0; item_id < tag->count; item_id++) {
				sprintf(str_num, "%d", item_id);
				bson_append_double(&bson_tag, str_num, ((double*)tag->value)[item_id]);
			}
			break;
		case VRS_VALUE_TYPE_STRING8:
			bson_append_string(&bson_tag, "0", (char*)tag->value);
			break;
		}
		bson_append_finish_array(&bson_tag);

		bson_finish(&bson_tag);

		sprintf(str_num, "%d", tag->id);
		bson_append_bson(&bson_version, str_num, &bson_tag);

		bucket = bucket->next;
	}

	bson_append_finish_object(&bson_version);

	bson_finish(&bson_version);

	sprintf(str_num, "%u", version);
	bson_append_bson(bson_tg, str_num, &bson_version);
}

/**
 * \brief This function tries to update tag group in MongoDB. It adds new
 * version of data.
 */
int vs_mongo_taggroup_update(struct VS_CTX *vs_ctx,
		struct VSNode *node,
		struct VSTagGroup *tg)
{
	bson cond, op;
	bson bson_version;
	int ret;

	/* TODO: delete old version, when there is too much versions:
	int old_saved_version = tg->saved_version;
	*/

	bson_init(&cond);
	{
		bson_append_oid(&cond, "_id", &tg->oid);
		/* To be sure that right tag group will be updated */
		bson_append_int(&cond, "node_id", node->id);
		bson_append_int(&cond, "taggroup_id", tg->id);
	}
	bson_finish(&cond);

	bson_init(&op);
	{
		/* Update item current_version in document */
		bson_append_start_object(&op, "$set");
		{
			bson_append_int(&op, "current_version", tg->version);
		}
		bson_append_finish_object(&op);
		/* Create new bson object representing current version and add it to
		 * the object versions */
		bson_append_start_object(&op, "$set");
		{
			bson_init(&bson_version);
			{
				vs_mongo_taggroup_save_version(tg, &bson_version, UINT32_MAX);
			}
			bson_finish(&bson_version);
			bson_append_bson(&op, "versions", &bson_version);
		}
		bson_append_finish_object(&op);
	}
	bson_finish(&op);

	ret = mongo_update(vs_ctx->mongo_conn, vs_ctx->mongo_tg_ns, &cond, &op,
			MONGO_UPDATE_BASIC, 0);

	bson_destroy(&bson_version);
	bson_destroy(&cond);
	bson_destroy(&op);

	if(ret != MONGO_OK) {
		v_print_log(VRS_PRINT_ERROR,
				"Unable to update tag group %d to MongoDB: %s, error: %s\n",
				tg->id, vs_ctx->mongo_tg_ns,
				mongo_get_server_err_string(vs_ctx->mongo_conn));
		return 0;
	}

	return 1;
}

/**
 * \brief This function tries to add new tag group to MongoDB
 */
int vs_mongo_taggroup_add_new(struct VS_CTX *vs_ctx,
		struct VSNode *node,
		struct VSTagGroup *tg)
{
	bson bson_tg;
	int ret;
	bson_init(&bson_tg);

	bson_oid_gen(&tg->oid);
	bson_append_oid(&bson_tg, "_id", &tg->oid);
	bson_append_int(&bson_tg, "node_id", node->id);
	bson_append_int(&bson_tg, "taggroup_id", tg->id);
	bson_append_int(&bson_tg, "custom_type", tg->custom_type);
	bson_append_int(&bson_tg, "current_version", tg->version);

	bson_append_start_object(&bson_tg, "versions");
	vs_mongo_taggroup_save_version(tg, &bson_tg, UINT32_MAX);
	bson_append_finish_object(&bson_tg);

	bson_finish(&bson_tg);

	ret = mongo_insert(vs_ctx->mongo_conn, vs_ctx->mongo_tg_ns, &bson_tg, 0);

	bson_destroy(&bson_tg);

	if(ret != MONGO_OK) {
		v_print_log(VRS_PRINT_ERROR,
				"Unable to write tag group %d of node %d to MongoDB: %s, error: %s\n",
				tg->id, node->id, vs_ctx->mongo_tg_ns,
				mongo_get_server_err_string(vs_ctx->mongo_conn));
		return 0;
	}

	return 1;
}

/**
 * \brief This function saves tag group to the MongoDB
 *
 * \param[in] *vs_ctx	The verse server context
 * \param[in] *node		The node containing tag group
 * \param[in] *tg		The tag group that will be saved
 *
 * \return	This function returns 1 on success. Otherwise it returns 0;
 */
int vs_mongo_taggroup_save(struct VS_CTX *vs_ctx,
		struct VSNode *node,
		struct VSTagGroup *tg)
{
	int ret = 1;

	if((int)tg->saved_version == -1) {
		/* Add new tag group to MongoDB */
		ret = vs_mongo_taggroup_add_new(vs_ctx, node, tg);
	} else if(tg->saved_version < tg->version) {
		/* Update document in database */
		ret = vs_mongo_taggroup_update(vs_ctx, node, tg);
	}

	if(ret == 1) {
		tg->saved_version = tg->version;
	}

	return ret;
}

/**
 * \brief This function tries to load tag from MongoDB
 */
static void vs_mongo_tag_load_data(struct VSTag *tag,
		bson *bson_tag)
{
	bson_iterator tag_iter;

	if( bson_find(&tag_iter, bson_tag, "data") == BSON_ARRAY) {
		bson_iterator data_iter;
		uint8 val_uint8;
		uint16 val_uint16;
		uint32 val_uint32;
		uint64 val_uint64;
		real32 val_real32;
		real64 val_real64;
		const char *str_value;
		int value_index = 0;

		bson_iterator_subiterator(&tag_iter, &data_iter);

		/* Go through all values */
		switch(tag->data_type) {
		case VRS_VALUE_TYPE_UINT8:
			while( bson_iterator_next(&data_iter) == BSON_INT ) {
				val_uint8 = (uint8)bson_iterator_int(&data_iter);
				vs_tag_set_values(tag, 1, value_index, &val_uint8);
				value_index++;
			}
			break;
		case VRS_VALUE_TYPE_UINT16:
			while( bson_iterator_next(&data_iter) == BSON_INT ) {
				val_uint16 = (uint16)bson_iterator_int(&data_iter);
				vs_tag_set_values(tag, 1, value_index, &val_uint16);
				value_index++;
			}
			break;
		case VRS_VALUE_TYPE_UINT32:
			while( bson_iterator_next(&data_iter) == BSON_INT ) {
				val_uint32 = (uint32)bson_iterator_int(&data_iter);
				vs_tag_set_values(tag, 1, value_index, &val_uint32);
				value_index++;
			}
			break;
		case VRS_VALUE_TYPE_UINT64:
			while( bson_iterator_next(&data_iter) == BSON_LONG ) {
				val_uint64 = (uint64)bson_iterator_long(&data_iter);
				vs_tag_set_values(tag, 1, value_index, &val_uint64);
				value_index++;
			}
			break;
		case VRS_VALUE_TYPE_REAL16:
			/* TODO: add support for float16 */
			break;
		case VRS_VALUE_TYPE_REAL32:
			while( bson_iterator_next(&data_iter) == BSON_DOUBLE ) {
				val_real32 = (real32)bson_iterator_double(&data_iter);
				vs_tag_set_values(tag, 1, value_index, &val_real32);
				value_index++;
			}
			break;
		case VRS_VALUE_TYPE_REAL64:
			while( bson_iterator_next(&data_iter) == BSON_DOUBLE ) {
				val_real64 = (real64)bson_iterator_double(&data_iter);
				vs_tag_set_values(tag, 1, value_index, &val_real64);
				value_index++;
			}
			break;
		case VRS_VALUE_TYPE_STRING8:
			str_value = bson_iterator_string(&data_iter);
			strcpy(tag->value, str_value);
			break;
		}
	}
}

/**
 * \brief This function tries to load tag group data from MongoDB
 */
static void vs_mongo_taggroup_load_data(struct VSTagGroup *tg,
		bson *bson_version)
{
	bson_iterator version_data_iter;

	/* Try to get tags of tag group */
	if( bson_find(&version_data_iter, bson_version, "tags") == BSON_OBJECT ) {
		struct VSTag *tag;
		bson bson_tag;
		bson_iterator tags_iter, tag_iter;
		const char *key;
		int tag_id, data_type = -1, count = -1, tag_custom_type = -1;

		bson_iterator_subiterator(&version_data_iter, &tags_iter);

		while( bson_iterator_next(&tags_iter) == BSON_OBJECT ) {
			key = bson_iterator_key(&tags_iter);
			sscanf(key, "%d", &tag_id);

			bson_iterator_subobject_init(&tags_iter, &bson_tag, 0);

			if( bson_find(&tag_iter, &bson_tag, "data_type") == BSON_INT) {
				data_type = bson_iterator_int(&tag_iter);
			}

			if( bson_find(&tag_iter, &bson_tag, "count") == BSON_INT) {
				count = bson_iterator_int(&tag_iter);
			}

			if( bson_find(&tag_iter, &bson_tag, "custom_type") == BSON_INT) {
				tag_custom_type = bson_iterator_int(&tag_iter);
			}

			if(data_type != -1 && count != -1 && tag_custom_type != -1) {
				/* Create tag with specific ID */
				tag = vs_tag_create(tg, tag_id, data_type, count, tag_custom_type);

				if(tag != NULL) {
					tag->state = ENTITY_CREATED;

					vs_mongo_tag_load_data(tag, &bson_tag);

					tag->flag = TAG_INITIALIZED;
				}
			}
		}
	}
}

/**
 * \brief This function tries to load tag group from MongoDB
 *
 * \param[in] *vs_ctx		The verse server context
 * \param[in] *oid			The pointer at ObjectID of tag group in MongoDB
 * \param[in] *node			The node containing tag group
 * \param[in] taggroup_id	The tag group ID that is requested from database
 * \param[in] version		The version of tag group id that is requested
 * 							from database. When version is equal to -1, then
 * 							current version is loaded from MongoDB.
 *
 * \return This function returns pointer at tag group, when tag group is found.
 * Otherwise it returns NULL.
 */
struct VSTagGroup *vs_mongo_taggroup_load_linked(struct VS_CTX *vs_ctx,
		bson_oid_t *oid,
		struct VSNode *node,
		uint16 taggroup_id,
		uint32 req_version)
{
	struct VSTagGroup *tg = NULL;
	bson query;
	mongo_cursor cursor;
	uint32 node_id = -1, tmp_taggroup_id = -1,
			current_version = -1, custom_type = -1;
	int found = 0;
	bson_iterator tg_data_iter;
	const bson *bson_tg;

	bson_init(&query);
	bson_append_oid(&query, "_id", oid);
	bson_finish(&query);

	mongo_cursor_init(&cursor, vs_ctx->mongo_conn, vs_ctx->mongo_tg_ns);
	mongo_cursor_set_query(&cursor, &query);

	/* ObjectID should be unique */
	while( mongo_cursor_next(&cursor) == MONGO_OK ) {
		bson_tg = mongo_cursor_bson(&cursor);

		/* Try to get node id */
		if( bson_find(&tg_data_iter, bson_tg, "node_id") == BSON_INT ) {
			node_id = bson_iterator_int(&tg_data_iter);
		}

		/* Try to get tag group id */
		if( bson_find(&tg_data_iter, bson_tg, "taggroup_id") == BSON_INT ) {
			tmp_taggroup_id = bson_iterator_int(&tg_data_iter);
		}

		/* ObjectID is ALMOST unique. So it is check, if node id and
		 * tag group id matches */
		if(node_id == node->id && tmp_taggroup_id == taggroup_id) {
			found = 1;
			break;
		}
	}

	/* When tag group was found, then load required data from MongoDB */
	if(found == 1) {

		/* Try to get current version of tag group */
		if( bson_find(&tg_data_iter, bson_tg, "current_version") == BSON_INT ) {
			current_version = bson_iterator_int(&tg_data_iter);
		}

		/* Try to get custom type of tag group */
		if( bson_find(&tg_data_iter, bson_tg, "custom_type") == BSON_INT ) {
			custom_type = bson_iterator_int(&tg_data_iter);
		}

		/* Create tag group with specific ID */
		if((int)current_version != -1 && (int)custom_type != -1) {
			tg = vs_taggroup_create(node, taggroup_id, custom_type);

			if(tg != NULL) {

				tg->state = ENTITY_CREATED;

				/* Save ObjectID to tag group */
				memcpy(&tg->oid, oid, sizeof(bson_oid_t));

				/* Try to get versions of tag group */
				if( bson_find(&tg_data_iter, bson_tg, "versions") == BSON_OBJECT ) {
					bson bson_versions;
					bson_iterator version_iter;
					char str_num[15];

					/* Initialize sub-object of versions */
					bson_iterator_subobject_init(&tg_data_iter, &bson_versions, 0);

					sprintf(str_num, "%u", req_version);

					/* Try to find required version of tag group */
					if( bson_find(&version_iter, &bson_versions, str_num) == BSON_OBJECT ) {
						bson bson_version;

						/* Set version of tag group */
						tg->version = tg->saved_version = current_version;

						bson_iterator_subobject_init(&version_iter, &bson_version, 0);

						/* Try to load tags */
						vs_mongo_taggroup_load_data(tg, &bson_version);
					}
				}
			}
		}
	}

	bson_destroy(&query);
	mongo_cursor_destroy(&cursor);

	return tg;
}
