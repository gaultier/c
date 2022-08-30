#include </usr/local/opt/lmdb/include/lmdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

static int dump_db(MDB_env* env) {
    int err = 0;
    MDB_txn* txn = NULL;
    MDB_dbi dbi = {0};
    MDB_cursor* cursor = NULL;

    if ((err = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn)) != 0) {
        fprintf(stderr, "Failed to mdb_txn_begin: err=%s\n", mdb_strerror(err));
        goto end;
    }

    if ((err = mdb_dbi_open(txn, NULL, 0, &dbi)) != 0) {
        fprintf(stderr, "Failed to mdb_dbi_open: err=%s", mdb_strerror(err));
        goto end;
    }

    if ((err = mdb_cursor_open(txn, dbi, &cursor)) != 0) {
        fprintf(stderr, "Failed to mdb_cursor_open: err=%s\n",
                mdb_strerror(err));
        goto end;
    }
    MDB_val key = {0}, data = {0};
    while ((err = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
        printf("key=%.*s data=%.*s\n", (int)key.mv_size, (char*)key.mv_data,
               (int)data.mv_size, (char*)data.mv_data);
    }
    if (err == MDB_NOTFOUND) {
        err = 0;  // Not really an error
    } else {
        fprintf(stderr, "Failed to mdb_cursor_get: err=%s\n",
                mdb_strerror(err));
        goto end;
    }

end:
    if (cursor != NULL) mdb_cursor_close(cursor);
    if (txn != NULL) mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);

    return err;
}

static int put_db(MDB_env* env) {
    int err = 0;
    MDB_txn* txn = NULL;
    MDB_dbi dbi = {0};
    if ((err = mdb_txn_begin(env, NULL, 0, &txn)) != 0) {
        fprintf(stderr, "Failed to mdb_txn_begin: err=%s", mdb_strerror(err));
        goto end;
    }

    if ((err = mdb_dbi_open(txn, NULL, 0, &dbi)) != 0) {
        fprintf(stderr, "Failed to mdb_dbi_open: err=%s", mdb_strerror(err));
        goto end;
    }

    char buf_key[128] = "";
    char buf_value[128] = "";
    MDB_val key = {.mv_data = buf_key, .mv_size = sizeof(buf_key)},
            data = {.mv_data = buf_value, .mv_size = sizeof(buf_value)};
    struct timeval now = {0};
    gettimeofday(&now, NULL);
    snprintf(buf_key, sizeof(buf_key), "now-%ld", now.tv_sec);
    snprintf(buf_value, sizeof(buf_value),
             "{\"seconds\":%ld, \"microseconds\":%d}", now.tv_sec, now.tv_usec);

    if ((err = mdb_put(txn, dbi, &key, &data, 0)) != 0) {
        fprintf(stderr, "Failed to mdb_put: err=%s", mdb_strerror(err));
        goto end;
    }
    if ((err = mdb_txn_commit(txn)) != 0) {
        fprintf(stderr, "Failed to mdb_txn_commit: err=%s", mdb_strerror(err));
        goto end;
    }

end:
    mdb_dbi_close(env, dbi);

    return err;
}

int main() {
    int err = 0;
    MDB_env* env = NULL;

    if ((err = mdb_env_create(&env)) != 0) {
        fprintf(stderr, "Failed to mdb_env_create: err=%s", mdb_strerror(err));
        goto end;
    }

    if ((err = mdb_env_open(env, "./testdb", MDB_NOSUBDIR, 0664)) != 0) {
        fprintf(stderr, "Failed to mdb_env_open: err=%s", mdb_strerror(err));
        goto end;
    }

    if ((err = dump_db(env)) != 0) goto end;

    if ((err = put_db(env)) != 0) goto end;

end:
    if (env != NULL) mdb_env_close(env);
}
