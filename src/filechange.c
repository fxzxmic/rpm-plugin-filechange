#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rpm/header.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmfi.h>
#include <rpm/rpmfiles.h>
#include <rpm/rpmmacro.h>
#include <rpm/rpmplugin.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmte.h>
#include <rpm/rpmts.h>

typedef struct {
    GHashTable *files;
    rpmte te; // borrowed from the transaction
} Snapshot;

typedef struct {
    GHashTable *old;
    char **exclude;
    char *log;
} State;

static void snapshot_free(gpointer data) {
    Snapshot *snapshot = data;
    g_hash_table_destroy(snapshot->files);
    g_free(snapshot);
}

static gboolean is_excluded(const char *path, char *const *exclude) {
    for (char *const *keyword = exclude; keyword && *keyword; keyword++) {
        if (**keyword && strstr(path, *keyword)) {
            return TRUE;
        }
    }
    return FALSE;
}

static Snapshot *snapshot_from_iterator(rpmfi fi, rpmte te,
                                        char *const *exclude) {
    Snapshot *snapshot = g_new0(Snapshot, 1);
    snapshot->files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    snapshot->te = te;

    while (rpmfiNext(fi) >= 0) {
        const char *path = rpmfiFN(fi);
        if (path && !is_excluded(path, exclude)) {
            g_hash_table_add(snapshot->files, g_strdup(path));
        }
    }

    return snapshot;
}

static Snapshot *snapshot_from_element(rpmte te, char *const *exclude) {
    rpmfiles files = rpmteFiles(te);
    if (!files) {
        return NULL;
    }

    rpmfi fi = rpmfilesIter(files, RPMFI_ITER_FWD);
    if (!fi) {
        rpmfilesFree(files);
        return NULL;
    }

    Snapshot *snapshot = snapshot_from_iterator(fi, te, exclude);
    rpmfiFree(fi);
    rpmfilesFree(files);
    return snapshot;
}

static Snapshot *snapshot_from_db(rpmts ts, rpmte te, char *const *exclude) {
    unsigned int instance = rpmteDBInstance(te);
    if (!instance) {
        return NULL;
    }

    rpmdbMatchIterator mi = rpmtsInitIterator(ts, RPMDBI_PACKAGES,
                                              &instance, sizeof(instance));
    if (!mi) {
        return NULL;
    }

    Header header = rpmdbNextIterator(mi);
    Snapshot *snapshot = NULL;
    if (header) {
        rpmfi fi = rpmfiNew(ts, header, RPMTAG_BASENAMES, 0);
        if (fi) {
            snapshot = snapshot_from_iterator(fi, te, exclude);
            rpmfiFree(fi);
        }
    }
    rpmdbFreeIterator(mi);
    return snapshot;
}

static rpmRC filechange_pre(rpmPlugin plugin, rpmts ts) {
    State *state = g_new0(State, 1);
    state->old = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                       (GDestroyNotify)g_ptr_array_unref);

    char *expanded = rpmExpand("%{?_filechange_exclude}", NULL);
    state->exclude = g_strsplit_set(expanded ? expanded : "", " \t\r\n", -1);
    free(expanded);
    state->log = rpmExpand("%{?_filechange_log}", NULL);

    rpmPluginSetData(plugin, state);

    rpmtsi tsi = rpmtsiInit(ts);
    rpmte te;
    while ((te = rpmtsiNext(tsi, TR_REMOVED)) != NULL) {
        rpmte added = rpmteDependsOn(te);
        if (!added || strcmp(rpmteN(te), rpmteN(added)) != 0) {
            continue;
        }

        Snapshot *snapshot = snapshot_from_element(te, state->exclude);
        if (snapshot) {
            GPtrArray *old = g_hash_table_lookup(state->old, added);
            if (!old) {
                old = g_ptr_array_new_with_free_func(snapshot_free);
                g_hash_table_insert(state->old, added, old);
            }
            g_ptr_array_add(old, snapshot);
        }
    }
    rpmtsiFree(tsi);
    return RPMRC_OK;
}

static GPtrArray *changed_paths(GHashTable *left, GHashTable *right) {
    GPtrArray *paths = g_ptr_array_new();
    GHashTableIter iter;
    gpointer path;
    g_hash_table_iter_init(&iter, left);
    while (g_hash_table_iter_next(&iter, &path, NULL)) {
        if (!g_hash_table_contains(right, path)) {
            g_ptr_array_add(paths, path);
        }
    }
    return paths;
}

static gint compare_strings(gconstpointer a, gconstpointer b) {
    return g_strcmp0(*(char * const *)a, *(char * const *)b);
}

static void log_paths(FILE *fp, const char *label, const GPtrArray *paths) {
    if (paths->len == 0) {
        return;
    }

    fprintf(fp, "  %s %u file(s):\n", label, paths->len);
    for (guint i = 0; i < paths->len; i++) {
        fprintf(fp, "    %s\n", (char *)g_ptr_array_index(paths, i));
    }
}

static void log_changes(FILE *fp, const char *name,
                        const Snapshot *old, const Snapshot *new) {
    GPtrArray *added = changed_paths(new->files, old->files);
    GPtrArray *removed = changed_paths(old->files, new->files);
    g_ptr_array_sort(added, compare_strings);
    g_ptr_array_sort(removed, compare_strings);

    if (added->len || removed->len) {
        fprintf(fp, "Package %s: %s → %s\n", name,
                rpmteEVR(old->te), rpmteEVR(new->te));
        log_paths(fp, "Added", added);
        log_paths(fp, "Removed", removed);
        fputc('\n', fp);
    }

    g_ptr_array_free(added, TRUE);
    g_ptr_array_free(removed, TRUE);
}

static rpmRC filechange_post(rpmPlugin plugin, rpmts ts, int res) {
    (void)res;
    State *state = rpmPluginGetData(plugin);
    if (state->log && *state->log && g_hash_table_size(state->old) > 0) {
        FILE *fp = fopen(state->log, "a");
        if (fp) {
            rpmtsi tsi = rpmtsiInit(ts);
            rpmte te;
            while ((te = rpmtsiNext(tsi, TR_ADDED)) != NULL) {
                if (rpmteFailed(te)) {
                    continue;
                }

                GPtrArray *old = g_hash_table_lookup(state->old, te);
                if (!old) {
                    continue;
                }

                Snapshot *new = snapshot_from_db(ts, te, state->exclude);
                if (new) {
                    for (guint i = 0; i < old->len; i++) {
                        Snapshot *previous = g_ptr_array_index(old, i);
                        if (!rpmteFailed(previous->te)) {
                            log_changes(fp, rpmteN(te), previous, new);
                        }
                    }
                    snapshot_free(new);
                }
            }
            rpmtsiFree(tsi);
            fclose(fp);
        }
    }

    g_hash_table_destroy(state->old);
    g_strfreev(state->exclude);
    free(state->log);
    g_free(state);
    rpmPluginSetData(plugin, NULL);
    return RPMRC_OK;
}

struct rpmPluginHooks_s filechange_hooks = {
    .tsm_pre = filechange_pre,
    .tsm_post = filechange_post,
};
