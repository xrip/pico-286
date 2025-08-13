/*
 * Copyright (C) 2011 Mathieu Turcotte (mathieuturcotte.ca)
 *
 * This program is free software: you can redistribute it and/or modify
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
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */

#ifdef __linux__
#define _XOPEN_SOURCE 700   /* SUSv4 */
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <libgen.h>
#include <limits.h>
#include <dirent.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#ifdef __linux__
#include <alloca.h>
#endif

#include "findfirst.h"
#include "spec.h"

#define DOTDOT_HANDLE    0L
#define INVALID_HANDLE  -1L

typedef struct fhandle_t {
    DIR* dstream;
    short dironly;
    char* spec;
} fhandle_t;

static void fill_finddata(struct stat* st, const char* name,
        struct _finddata_t* fileinfo);

static intptr_t findfirst_dotdot(const char* filespec,
        struct _finddata_t* fileinfo);

static intptr_t findfirst_in_directory(const char* dirpath,
        const char* spec, struct _finddata_t* fileinfo);

static void findfirst_set_errno();

intptr_t _findfirst(const char* filespec, struct _finddata_t* fileinfo) {
    char* rmslash;      /* Rightmost forward slash in filespec. */
    const char* spec;   /* Specification string. */

    if (!fileinfo || !filespec) {
        errno = EINVAL;
        return INVALID_HANDLE;
    }

    if (filespec[0] == '\0') {
        errno = ENOENT;
        return INVALID_HANDLE;
    }

    rmslash = strrchr(filespec, '/');

    if (rmslash != NULL) {
        /*
         * At least one forward slash was found in the filespec
         * string, and rmslash points to the rightmost one. The
         * specification part, if any, begins right after it.
         */
        spec = rmslash + 1;
    } else {
        /*
         * Since no slash was found in the filespec string, its
         * entire content can be used as our spec string.
         */
        spec = filespec;
    }

    if (strcmp(spec, ".") == 0 || strcmp(spec, "..") == 0) {
        /* On Windows, . and .. must return canonicalized names. */
        return findfirst_dotdot(filespec, fileinfo);
    } else if (rmslash == filespec) {
        /*
         * Since the rightmost slash is the first character, we're
         * looking for something located at the file system's root.
         */
        return findfirst_in_directory("/", spec, fileinfo);
    } else if (rmslash != NULL) {
        /*
         * Since the rightmost slash isn't the first one, we're
         * looking for something located in a specific folder. In
         * order to open this folder, we split the folder path from
         * the specification part by overwriting the rightmost
         * forward slash.
         */
        size_t pathlen = strlen(filespec) +1;
        char* dirpath = alloca(pathlen);
        memcpy(dirpath, filespec, pathlen);
        dirpath[rmslash - filespec] = '\0';
        return findfirst_in_directory(dirpath, spec, fileinfo);
    } else {
        /*
         * Since the filespec doesn't contain any forward slash,
         * we're looking for something located in the current
         * directory.
         */
        return findfirst_in_directory(".", spec, fileinfo);
    }
}

/* Perfom a scan in the directory identified by dirpath. */
static intptr_t findfirst_in_directory(const char* dirpath,
        const char* spec, struct _finddata_t* fileinfo) {
    DIR* dstream;
    fhandle_t* ffhandle;

    if (spec[0] == '\0') {
        errno = ENOENT;
        return INVALID_HANDLE;
    }

    if ((dstream = opendir(dirpath)) == NULL) {
        findfirst_set_errno();
        return INVALID_HANDLE;
    }

    if ((ffhandle = malloc(sizeof(fhandle_t))) == NULL) {
        closedir(dstream);
        errno = ENOMEM;
        return INVALID_HANDLE;
    }

    /* On Windows, *. returns only directories. */
    ffhandle->dironly = strcmp(spec, "*.") == 0 ? 1 : 0;
    ffhandle->dstream = dstream;
    ffhandle->spec = strdup(spec);

    if (_findnext((intptr_t) ffhandle, fileinfo) != 0) {
        _findclose((intptr_t) ffhandle);
        errno = ENOENT;
        return INVALID_HANDLE;
    }

    return (intptr_t) ffhandle;
}

/* On Windows, . and .. return canonicalized directory names. */
static intptr_t findfirst_dotdot(const char* filespec,
        struct _finddata_t* fileinfo) {
    char* dirname;
    char* canonicalized;
    struct stat st;

    if (stat(filespec, &st) != 0) {
        findfirst_set_errno();
        return INVALID_HANDLE;
    }

    /* Resolve filespec to an absolute path. */
    if ((canonicalized = realpath(filespec, NULL)) == NULL) {
        findfirst_set_errno();
        return INVALID_HANDLE;
    }

    /* Retrieve the basename from it. */
    dirname = basename(canonicalized);

    /* Make sure that we actually have a basename. */
    if (dirname[0] == '\0') {
        free(canonicalized);
        errno = ENOENT;
        return INVALID_HANDLE;
    }

    /* Make sure that we won't overflow finddata_t::name. */
    if (strlen(dirname) > 259) {
        free(canonicalized);
        errno = ENOMEM;
        return INVALID_HANDLE;
    }

    fill_finddata(&st, dirname, fileinfo);

    free(canonicalized);

    /*
     * Return a special handle since we can't return
     * NULL. The findnext and findclose functions know
     * about this custom handle.
     */
    return DOTDOT_HANDLE;
}

/*
 * Windows implementation of _findfirst either returns EINVAL,
 * ENOENT or ENOMEM. This function makes sure that the above
 * implementation doesn't return anything else when an error
 * condition is encountered.
 */
static void findfirst_set_errno() {
    if (errno != ENOENT &&
        errno != ENOMEM &&
        errno != EINVAL) {
        errno = EINVAL;
    }
}

static void fill_finddata(struct stat* st, const char* name,
        struct _finddata_t* fileinfo) {
    fileinfo->attrib = S_ISDIR(st->st_mode) ? _A_SUBDIR : _A_NORMAL;
    fileinfo->size = st->st_size;
    fileinfo->time_create = st->st_ctime;
    fileinfo->time_access = st->st_atime;
    fileinfo->time_write = st->st_mtime;
    strcpy(fileinfo->name, name);
}

int _findnext(intptr_t fhandle, struct _finddata_t* fileinfo) {
    struct dirent entry, *result;
    struct fhandle_t* handle;
    struct stat st;

    if (fhandle == DOTDOT_HANDLE) {
        errno = ENOENT;
        return -1;
    }

    if (fhandle == INVALID_HANDLE || !fileinfo) {
        errno = EINVAL;
        return -1;
    }

    handle = (struct fhandle_t*) fhandle;

    while (readdir_r(handle->dstream, &entry, &result) == 0 && result != NULL) {
        if (!handle->dironly && !match_spec(handle->spec, entry.d_name)) {
            continue;
        }

        if (fstatat(dirfd(handle->dstream), entry.d_name, &st, 0) == -1) {
            return -1;
        }

        if (handle->dironly && !S_ISDIR(st.st_mode)) {
            continue;
        }

        fill_finddata(&st, entry.d_name, fileinfo);

        return 0;
    }

    errno = ENOENT;
    return -1;
}

int _findclose(intptr_t fhandle) {
    struct fhandle_t* handle;

    if (fhandle == DOTDOT_HANDLE) {
        return 0;
    }

    if (fhandle == INVALID_HANDLE) {
        errno = ENOENT;
        return -1;
    }

    handle = (struct fhandle_t*) fhandle;

    closedir(handle->dstream);
    free(handle->spec);
    free(handle);

    return 0;
}

