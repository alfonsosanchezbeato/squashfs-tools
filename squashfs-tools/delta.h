#ifndef DELTA_H
#define DELTA_H
/*
 * Delta/merge functionality for SquashFS
 *
 * Copyright (c) 2025
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 */

#define DELTA_MAGIC 0x44454C54  /* "DELT" */
#define DELTA_VERSION 1

/* Delta file header */
struct delta_header {
	unsigned int magic;
	unsigned int version;
	long long squashfs1_size;
	long long squashfs2_size;
	unsigned int num_entries;
};

/* Delta entry types */
#define DELTA_ENTRY_NEW_FILE    1
#define DELTA_ENTRY_MODIFIED    2
#define DELTA_ENTRY_DELETED     3
#define DELTA_ENTRY_METADATA    4

/* Delta entry */
struct delta_entry {
	unsigned int type;
	unsigned int name_len;
	long long offset;      /* offset in original file */
	long long size;        /* size of data */
	/* followed by: name (name_len bytes) and data (size bytes) */
};

/* Function prototypes */
int delta_create(const char *squashfs1, const char *squashfs2, const char *deltafile);
int delta_merge(const char *squashfs1, const char *deltafile, const char *output);

#endif
