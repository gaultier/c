#include </usr/local/opt/lmdb/include/lmdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static int db_dump(MDB_env* env) {
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

static int db_put(MDB_env* env, char* key, char* value) {
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

    MDB_val mdb_key = {.mv_data = key, .mv_size = strlen(key)},
            mdb_value = {.mv_data = value, .mv_size = strlen(value)};

    if ((err = mdb_put(txn, dbi, &mdb_key, &mdb_value, 0)) != 0) {
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

static void print_usage(int argc, char* argv[]) {
    (void)argc;

    printf("%s get <key>\n%s put <key> <value>\n%s dump\n", argv[0], argv[0],
           argv[0]);
}

static int db_get(MDB_env* env, char* key) {
    int err = 0;
    MDB_txn* txn = NULL;
    MDB_dbi dbi = {0};

    if ((err = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn)) != 0) {
        fprintf(stderr, "Failed to mdb_txn_begin: err=%s\n", mdb_strerror(err));
        goto end;
    }

    if ((err = mdb_dbi_open(txn, NULL, 0, &dbi)) != 0) {
        fprintf(stderr, "Failed to mdb_dbi_open: err=%s", mdb_strerror(err));
        goto end;
    }

    MDB_val mdb_key = {.mv_data = key, .mv_size = strlen(key)}, mdb_value = {0};
    if ((err = mdb_get(txn, dbi, &mdb_key, &mdb_value)) != 0) {
        fprintf(stderr, "Failed to mdb_get: err=%s\n", mdb_strerror(err));
        goto end;
    }
    printf("key=%.*s value=%.*s\n", (int)mdb_key.mv_size,
           (char*)mdb_key.mv_data, (int)mdb_value.mv_size,
           (char*)mdb_value.mv_data);

end:
    if (txn != NULL) mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);

    return err;
}

typedef enum { OP_DUMP, OP_GET, OP_PUT } op_t;

int main(int argc, char* argv[]) {
    if (argc <= 1 || argc > 4) {
        print_usage(argc, argv);
        return 0;
    }

    op_t op = 0;
    if (argc == 2) {
        if (strcmp(argv[1], "dump") != 0) {
            print_usage(argc, argv);
            return 0;
        }
        op = OP_DUMP;
    } else if (argc == 3) {
        if (strcmp(argv[1], "get") != 0) {
            print_usage(argc, argv);
            return 0;
        }
        op = OP_GET;
    } else if (argc == 4) {
        if (strcmp(argv[1], "put") != 0) {
            print_usage(argc, argv);
            return 0;
        }
        op = OP_PUT;
    }
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

    switch (op) {
        case OP_DUMP:
            if ((err = db_dump(env)) != 0) goto end;
            break;

        case OP_GET:
            if ((err = db_get(env, argv[2])) != 0) goto end;
            break;

        case OP_PUT:
            if ((err = db_put(env, argv[2], argv[3])) != 0) goto end;
            break;

        default:
            __builtin_unreachable();
    }

end:
    if (env != NULL) mdb_env_close(env);
}
