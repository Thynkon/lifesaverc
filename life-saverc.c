#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>

#include <getopt.h>
#include <string.h>

// contains allow() (to check if a file exists)
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <archive.h>
#include <archive_entry.h>

#include <fcntl.h>

#include "life-saverc.h"

// Based on: https://stackoverflow.com/a/309105
// Concatenate n strings
// Useful when we don't know neither the size of the strings nor the number of strings to concatenate
char *strmcat(char *header, const char **words) {
    size_t message_len = strlen(header) + 1; // + 1 for terminating NULL 
	size_t num_words = sizeof(words) / sizeof(char *);
    char *message = (char*) malloc(message_len);
    strncat(message, header, message_len);

    for(size_t i = 0; i < num_words; ++i) {
       message_len += strlen(words[i]);
       message = (char*) realloc(message, message_len);
       strncat(message, words[i], message_len);
    }

	return message;
}

void msg(const char *m) {
	write(1, m, strlen(m));
}

void errmsg(const char *m) {
	if (m == NULL) {
		m = "Error: No error description provided.\n";
	}

	fprintf(stderr, "%s", m);
}

void usage(void) {
	char *help_msg = "Usage:\n"
		"-f, --file filename\tFile to backup\n"
		"-h, --help\t\tDisplay this message\n"
		"-j, --bzip2\t\tFilter the archive through bzip2\n"
		"-z, --gzip\t\tFilter the archive through gzip";

	errmsg(help_msg);
	exit(EXIT_FAILURE);
}

int create(char *filename, int compression, char *argv, int verbose) {
	struct archive *a;
	struct archive_entry *entry;
	ssize_t len;
	int fd = 0;
	char buff[16384];

	a = archive_write_new();

	switch (compression) {
	case 'j': case 'y':
		archive_write_add_filter_bzip2(a);
		break;

	case 'Z':
		archive_write_add_filter_compress(a);
		break;

	case 'z':
		archive_write_add_filter_gzip(a);
		break;

	default:
		archive_write_add_filter_none(a);
		break;
	}

	archive_write_set_format_ustar(a);

	if (filename != NULL && strcmp(filename, "-") == 0) {
		filename = NULL;
	}

	archive_write_open_filename(a, filename);

	struct archive *disk = archive_read_disk_new();
	archive_read_disk_set_standard_lookup(disk);
	int r = 0;

	r = archive_read_disk_open(disk, argv);
	if (r != ARCHIVE_OK) {
		errmsg(archive_error_string(disk));
		errmsg("\n");
		return -1;
	}

	for (;;) {
		int needcr = 0;

		entry = archive_entry_new();
		r = archive_read_next_header2(disk, entry);
		if (r == ARCHIVE_EOF) {
			break;
		}

		if (r != ARCHIVE_OK) {
			errmsg(archive_error_string(disk));
			errmsg("\n");
			return -1;
		}

		archive_read_disk_descend(disk);
		if (verbose) {
			msg("a ");
			msg(archive_entry_pathname(entry));
			needcr = 1;
		}
		r = archive_write_header(a, entry);
		if (r < ARCHIVE_OK) {
			errmsg(": ");
			errmsg(archive_error_string(a));
			needcr = 1;
		}
		if (r == ARCHIVE_FATAL) {
			return -1;
		}

		if (r > ARCHIVE_FAILED) {
			/* For now, we use a simpler loop to copy data
			 * into the target archive. */
			fd = open(archive_entry_sourcepath(entry), O_RDONLY);
			len = read(fd, buff, sizeof(buff));
			while (len > 0) {
				archive_write_data(a, buff, len);
				len = read(fd, buff, sizeof(buff));
			}
			close(fd);
		}

		archive_entry_free(entry);
		if (needcr) {
			msg("\n");
		}
	}

	archive_read_close(disk);
	archive_read_free(disk);

	archive_write_close(a);
	archive_write_free(a);

	return 0;
}

int main(int argc, char **argv) {
	char *err_msg = NULL;
	char *filename = NULL;
	char *output_filename = NULL;
	const char *output_file_extension[] = {NULL};
	int compression = 0;

	int verbose = 0;
	int option = 0;
	int option_index = 0;

	struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"file", required_argument, 0, 'f'},
		{"gzip", no_argument, 0, 'z'},
		{"bzip2", no_argument, 0, 'j'}
	};

	// default compression algorithm set to bz2
	compression = 'j';
	output_file_extension[0] = ".tar.bz2";

	while ((option = getopt_long(argc, argv, "hf:jz", long_options, &option_index)) != -1) {
		switch(option) {
		case 'h':
			usage();
			break;

		case 'f':
			if ((filename = strdup(optarg)) == NULL) {
				fprintf(stderr, "Failed to retrieve file to backup!");

				exit(EXIT_FAILURE);
			}
			break;

		case 'j':
			compression = option;
			output_file_extension[0] = ".tar.bz2";
			break;

		case 'z':
			compression = option;
			output_file_extension[0] = ".tar.gz";
			break;

		default:
			break;
		}
	}

	// Check if file to backup exists
	if ((access(filename, F_OK)) == -1) {
		if (asprintf(&err_msg, "%s does not exist\n", filename) > 0) {
			errmsg(err_msg);

			free(err_msg);
			err_msg = NULL;
		}

		exit(EXIT_FAILURE);
	}

	output_filename = strmcat(filename, output_file_extension);

	if (create(output_filename, compression, filename, verbose) < 0) {
		if (asprintf(&err_msg, "Failed to compress %s", filename) > 0) {
			errmsg(err_msg);

			free(err_msg);
			err_msg = NULL;
		}
	}

	if (filename != NULL) {
		free(filename);
		filename = NULL;
	}

	if (output_filename != NULL) {
		free(output_filename);
		output_filename = NULL;
	}

	return EXIT_SUCCESS;
}
