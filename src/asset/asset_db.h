#ifndef NB_ASSET_ASSET_DB_H
#define NB_ASSET_ASSET_DB_H

#include <stddef.h>
#include <stdint.h>

#define NB_ASSET_DB_PATH_MAX 512
#define NB_ASSET_DB_UUID_SIZE 37

typedef struct {
	char uuid[NB_ASSET_DB_UUID_SIZE];
	char path[NB_ASSET_DB_PATH_MAX];
	int64_t mtime;
	int64_t size;
	int imported;
	int seen;
} asset_db_entry_t;

typedef struct {
	char root[NB_ASSET_DB_PATH_MAX];
	asset_db_entry_t* entries;
	int count;
	int capacity;
	int ready;
} asset_db_t;

int asset_db_init(asset_db_t* db, const char* root);
void asset_db_shutdown(asset_db_t* db);
int asset_db_refresh(asset_db_t* db);
int asset_db_import(asset_db_t* db, const char* path);
const char* asset_db_resolve(const asset_db_t* db, const char* path, char* out, size_t size);
const char* asset_db_path_for_uuid(const asset_db_t* db, const char* uuid, char* out, size_t size);
const char* asset_db_uuid(const asset_db_t* db, const char* path);
int asset_db_ensure_meta(const asset_db_t* db, const char* path, char* uuid_out, size_t size);

#endif
