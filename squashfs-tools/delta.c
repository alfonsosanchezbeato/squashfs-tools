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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "delta.h"
#include "mksquashfs_error.h"

/*
 * Create a delta file containing the differences between two SquashFS files
 * The delta contains the entire second file as uncompressed data with metadata
 */
int delta_create(const char *squashfs1, const char *squashfs2, const char *deltafile)
{
	int fd1 = -1, fd2 = -1, fdout = -1;
	struct stat st1, st2;
	struct delta_header header;
	unsigned char *buf1 = NULL, *buf2 = NULL;
	long long offset;
	int res = -1;
	size_t read_size;
	ssize_t bytes_read;

	/* Open input files */
	fd1 = open(squashfs1, O_RDONLY);
	if(fd1 < 0) {
		ERROR("Failed to open %s: %s\n", squashfs1, strerror(errno));
		goto cleanup;
	}

	fd2 = open(squashfs2, O_RDONLY);
	if(fd2 < 0) {
		ERROR("Failed to open %s: %s\n", squashfs2, strerror(errno));
		goto cleanup;
	}

	/* Get file sizes */
	if(fstat(fd1, &st1) < 0) {
		ERROR("Failed to stat %s: %s\n", squashfs1, strerror(errno));
		goto cleanup;
	}

	if(fstat(fd2, &st2) < 0) {
		ERROR("Failed to stat %s: %s\n", squashfs2, strerror(errno));
		goto cleanup;
	}

	/* Create output delta file */
	fdout = open(deltafile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if(fdout < 0) {
		ERROR("Failed to create %s: %s\n", deltafile, strerror(errno));
		goto cleanup;
	}

	/* Write delta header */
	memset(&header, 0, sizeof(header));
	header.magic = DELTA_MAGIC;
	header.version = DELTA_VERSION;
	header.squashfs1_size = st1.st_size;
	header.squashfs2_size = st2.st_size;
	header.num_entries = 1; /* One entry: the entire squashfs2 file */

	if(write(fdout, &header, sizeof(header)) != sizeof(header)) {
		ERROR("Failed to write delta header: %s\n", strerror(errno));
		goto cleanup;
	}

	/* Write a single delta entry containing the entire squashfs2 file */
	struct delta_entry entry;
	memset(&entry, 0, sizeof(entry));
	entry.type = DELTA_ENTRY_METADATA;
	entry.name_len = 0;
	entry.offset = 0;
	entry.size = st2.st_size;

	if(write(fdout, &entry, sizeof(entry)) != sizeof(entry)) {
		ERROR("Failed to write delta entry: %s\n", strerror(errno));
		goto cleanup;
	}

	/* Copy entire squashfs2 file to delta file */
	buf2 = malloc(1024 * 1024); /* 1MB buffer */
	if(!buf2) {
		ERROR("Failed to allocate buffer\n");
		goto cleanup;
	}

	offset = 0;
	while(offset < st2.st_size) {
		read_size = (st2.st_size - offset > 1024 * 1024) ? 1024 * 1024 : (st2.st_size - offset);
		bytes_read = read(fd2, buf2, read_size);
		if(bytes_read < 0) {
			ERROR("Failed to read from %s: %s\n", squashfs2, strerror(errno));
			goto cleanup;
		}
		if(bytes_read == 0)
			break;

		if(write(fdout, buf2, bytes_read) != bytes_read) {
			ERROR("Failed to write to delta file: %s\n", strerror(errno));
			goto cleanup;
		}

		offset += bytes_read;
	}

	printf("Created delta file: %s\n", deltafile);
	printf("  Source file 1 size: %lld bytes\n", (long long)st1.st_size);
	printf("  Source file 2 size: %lld bytes\n", (long long)st2.st_size);
	printf("  Delta file size: %lld bytes\n", offset + sizeof(header) + sizeof(entry));

	res = 0;

cleanup:
	if(buf1)
		free(buf1);
	if(buf2)
		free(buf2);
	if(fd1 >= 0)
		close(fd1);
	if(fd2 >= 0)
		close(fd2);
	if(fdout >= 0)
		close(fdout);

	return res;
}

/*
 * Apply a delta file to a SquashFS file to recreate the second SquashFS file
 */
int delta_merge(const char *squashfs1, const char *deltafile, const char *output)
{
	int fd1 = -1, fddelta = -1, fdout = -1;
	struct stat st1, stdelta;
	struct delta_header header;
	struct delta_entry entry;
	unsigned char *buf = NULL;
	int res = -1;
	ssize_t bytes_read;
	long long offset;
	size_t read_size;

	/* Open input files */
	fd1 = open(squashfs1, O_RDONLY);
	if(fd1 < 0) {
		ERROR("Failed to open %s: %s\n", squashfs1, strerror(errno));
		goto cleanup;
	}

	fddelta = open(deltafile, O_RDONLY);
	if(fddelta < 0) {
		ERROR("Failed to open %s: %s\n", deltafile, strerror(errno));
		goto cleanup;
	}

	/* Get file sizes */
	if(fstat(fd1, &st1) < 0) {
		ERROR("Failed to stat %s: %s\n", squashfs1, strerror(errno));
		goto cleanup;
	}

	if(fstat(fddelta, &stdelta) < 0) {
		ERROR("Failed to stat %s: %s\n", deltafile, strerror(errno));
		goto cleanup;
	}

	/* Read delta header */
	if(read(fddelta, &header, sizeof(header)) != sizeof(header)) {
		ERROR("Failed to read delta header: %s\n", strerror(errno));
		goto cleanup;
	}

	/* Validate delta header */
	if(header.magic != DELTA_MAGIC) {
		ERROR("Invalid delta file magic number\n");
		goto cleanup;
	}

	if(header.version != DELTA_VERSION) {
		ERROR("Unsupported delta file version: %u\n", header.version);
		goto cleanup;
	}

	if(header.squashfs1_size != st1.st_size) {
		ERROR("Source file size mismatch: expected %lld, got %lld\n",
			header.squashfs1_size, (long long)st1.st_size);
		goto cleanup;
	}

	/* Create output file */
	fdout = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if(fdout < 0) {
		ERROR("Failed to create %s: %s\n", output, strerror(errno));
		goto cleanup;
	}

	/* Process delta entries */
	if(read(fddelta, &entry, sizeof(entry)) != sizeof(entry)) {
		ERROR("Failed to read delta entry: %s\n", strerror(errno));
		goto cleanup;
	}

	/* Allocate buffer */
	buf = malloc(1024 * 1024); /* 1MB buffer */
	if(!buf) {
		ERROR("Failed to allocate buffer\n");
		goto cleanup;
	}

	/* Copy data from delta file to output */
	offset = 0;
	while(offset < entry.size) {
		read_size = (entry.size - offset > 1024 * 1024) ? 1024 * 1024 : (entry.size - offset);
		bytes_read = read(fddelta, buf, read_size);
		if(bytes_read < 0) {
			ERROR("Failed to read from delta file: %s\n", strerror(errno));
			goto cleanup;
		}
		if(bytes_read == 0)
			break;

		if(write(fdout, buf, bytes_read) != bytes_read) {
			ERROR("Failed to write to output file: %s\n", strerror(errno));
			goto cleanup;
		}

		offset += bytes_read;
	}

	printf("Merged delta file to create: %s\n", output);
	printf("  Output file size: %lld bytes\n", offset);

	res = 0;

cleanup:
	if(buf)
		free(buf);
	if(fd1 >= 0)
		close(fd1);
	if(fddelta >= 0)
		close(fddelta);
	if(fdout >= 0)
		close(fdout);

	return res;
}
