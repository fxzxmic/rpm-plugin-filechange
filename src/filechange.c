#include <rpm/rpmplugin.h>
#include <rpm/rpmts.h>
#include <rpm/rpmdb.h>
#include <rpm/header.h>
#include <rpm/rpmfi.h>
#include <glib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"

typedef struct {
    GHashTable *files;  // key: file path string
    char *evr;
} FileSnapshot;

static GHashTable *replacedPkgs = NULL; // key: pkg name (char*), value: FileSnapshot*

static void free_snapshot(gpointer data) {
    FileSnapshot *snap = data;
    if (!snap) return;
    g_hash_table_destroy(snap->files);
    free(snap->evr);
    free(snap);
}

static FileSnapshot *create_snapshot(rpmts ts, const char *pkgname, const char *evr) {
    rpmdbMatchIterator mi = rpmdbInitIterator(rpmtsGetRdb(ts), RPMDBI_NAME, pkgname, 0);
    Header hdr = rpmdbNextIterator(mi);
    if (!hdr) {
        rpmdbFreeIterator(mi);
        return NULL;
    }

    rpmfi fi = rpmfiNew(NULL, hdr, RPMTAG_BASENAMES, RPMFI_ITER_FWD);
    GHashTable *files = g_hash_table_new_full(g_str_hash, g_str_equal, free, NULL);

    while (rpmfiNext(fi) != -1) {
        const char *fn = rpmfiFN(fi);
        if (fn) g_hash_table_add(files, strdup(fn));
    }

    rpmfiFree(fi);
    rpmdbFreeIterator(mi);

    FileSnapshot *snap = malloc(sizeof(FileSnapshot));
    snap->files = files;
    snap->evr = strdup(evr);
    return snap;
}

static gboolean is_replaced(rpmts ts, const char *pkgname) {
    rpmtsi tsi = rpmtsiInit(ts);
    rpmte te;
    while ((te = rpmtsiNext(tsi, TR_ADDED)) != NULL) {
        if (strcmp(pkgname, rpmteN(te)) == 0) {
            rpmtsiFree(tsi);
            return TRUE;
        }
    }
    rpmtsiFree(tsi);
    return FALSE;
}

static rpmRC filechange_pre(rpmPlugin plugin, rpmts ts) {
    replacedPkgs = g_hash_table_new_full(g_str_hash, g_str_equal, free, (GDestroyNotify)free_snapshot);

    rpmtsi tsi = rpmtsiInit(ts);
    rpmte te;
    while ((te = rpmtsiNext(tsi, TR_REMOVED)) != NULL) {
        const char *pkgname = rpmteN(te);
        if (is_replaced(ts, pkgname)) {
            FileSnapshot *snap = create_snapshot(ts, pkgname, rpmteEVR(te));
            if (snap) {
                g_hash_table_insert(replacedPkgs, strdup(pkgname), snap);
            }
        }
    }
    rpmtsiFree(tsi);
    return RPMRC_OK;
}

static void log_file_changes(FILE *fp, const char *pkgname, FileSnapshot *old_snap, FileSnapshot *new_snap) {
    GPtrArray *added = g_ptr_array_new_with_free_func(free);
    GPtrArray *removed = g_ptr_array_new_with_free_func(free);

    GHashTableIter iter;
    gpointer key;

    // Collect added files
    g_hash_table_iter_init(&iter, new_snap->files);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        if (!g_hash_table_contains(old_snap->files, key)) {
            g_ptr_array_add(added, strdup((char *)key));
        }
    }

    // Collect removed files
    g_hash_table_iter_init(&iter, old_snap->files);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        if (!g_hash_table_contains(new_snap->files, key)) {
            g_ptr_array_add(removed, strdup((char *)key));
        }
    }

    // Log changes
    if (added->len || removed->len) {
        fprintf(fp, "Package %s: %s → %s\n", pkgname, old_snap->evr, new_snap->evr);
        if (added->len) {
            fprintf(fp, "  Added %d file(s):\n", added->len);
            for (guint i = 0; i < added->len; i++) {
                fprintf(fp, "    %s\n", (char *)g_ptr_array_index(added, i));
            }
        }
        if (removed->len) {
            fprintf(fp, "  Removed %d file(s):\n", removed->len);
            for (guint i = 0; i < removed->len; i++) {
                fprintf(fp, "    %s\n", (char *)g_ptr_array_index(removed, i));
            }
        }
        fprintf(fp, "\n");
    }

    g_ptr_array_free(added, TRUE);
    g_ptr_array_free(removed, TRUE);
}

static rpmRC filechange_post(rpmPlugin plugin, rpmts ts, int res) {
    if (!replacedPkgs) return RPMRC_OK;

    FILE *fp = fopen(LOG_FILE, "a");
    if (!fp) return RPMRC_OK;

    rpmtsi tsi = rpmtsiInit(ts);
    rpmte te;
    while ((te = rpmtsiNext(tsi, TR_ADDED)) != NULL) {
        const char *pkgname = rpmteN(te);
        FileSnapshot *old_snap = g_hash_table_lookup(replacedPkgs, pkgname);
        if (!old_snap) continue;

        FileSnapshot *new_snap = create_snapshot(ts, pkgname, rpmteEVR(te));
        if (!new_snap) continue;

        log_file_changes(fp, pkgname, old_snap, new_snap);

        free_snapshot(new_snap);
    }

    rpmtsiFree(tsi);
    fclose(fp);

    g_hash_table_destroy(replacedPkgs);
    replacedPkgs = NULL;
    return RPMRC_OK;
}

struct rpmPluginHooks_s filechange_hooks = {
    .tsm_pre = filechange_pre,
    .tsm_post = filechange_post,
};
