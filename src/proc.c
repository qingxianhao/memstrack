#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <stddef.h>
#include "memory-tracer.h"
#include "proc.h"


static struct slab_info *slab_info_table;
static int slab_info_size = 0, slab_info_number = 0;

static int sort_slab(const void *a, const void *b) {
	struct slab_info *info_a = (struct slab_info*)a;
	struct slab_info *info_b = (struct slab_info*)b;

	return (info_b->num_slabs * info_b->pagesperslab) - (info_a->num_slabs * info_a->pagesperslab);
}

int print_slab_usage()
{
	FILE *file;
	char line[PROC_MAX_LINE];
	int slab_debug, total_pages, entry_number;
	struct slab_info *entry;

	file = fopen(SLABINFO, "r");
	if (!file) {
		return -EINVAL;
	}

	entry_number = 0;
	total_pages = 0;

	for (int line_number = 0; fgets(line, PROC_MAX_LINE, file); ++line_number) {
		if (line_number == 0) {
			if (!strncmp(line, SLABINFO_DEBUG_HEAD, sizeof(SLABINFO_DEBUG_HEAD))) {
				slab_debug = 1;
			} else if (!strncmp(line, SLABINFO_HEAD, sizeof(SLABINFO_HEAD))) {
				slab_debug = 0;
			} else {
				/* Unrecognized ? */
				fclose(file);
				return -EINVAL;
			}
			continue;
		}

		/* Skip the header */
		if (line_number == 1) {
			continue;
		}

		entry_number = line_number - 2;
		if (entry_number >= slab_info_number) {
			slab_info_number = entry_number;
			if (slab_info_number >= slab_info_size) {
				if (slab_info_size == 0) {
					slab_info_size = 32;
					slab_info_table = malloc(slab_info_size * sizeof(*slab_info_table));
				} else {
					struct slab_info *slab_info_new;

					slab_info_size *= 2;
					slab_info_new = malloc(slab_info_size * sizeof(*slab_info_table));
					memcpy(slab_info_new, slab_info_table, sizeof(*slab_info_table) * slab_info_size / 2);
					free(slab_info_table);
					slab_info_table = slab_info_new;
				}
			}
		}

		entry = &slab_info_table[entry_number];

		sscanf(line, "%s %lu %lu %u %u %d : tunables %u %u %u : slabdata %lu %lu %lu",
				entry->name, &entry->active_objs, &entry->num_objs,
				&entry->objsize, &entry->objperslab, &entry->pagesperslab,
				&entry->limit, &entry->batchcount, &entry->sharedfactor,
				&entry->active_slabs, &entry->num_slabs, &entry->sharedavail);

		total_pages += entry->num_slabs * entry->pagesperslab;
	}

	slab_info_number = entry_number;
	qsort((void*)slab_info_table, slab_info_number, sizeof(struct slab_info), sort_slab);

	log_info("Top Slab Usage:\n");
	for (int i = 0; i < slab_info_number; ++i) {
		entry = &slab_info_table[i];
		unsigned long size_in_mb = entry->num_slabs * entry->pagesperslab * page_size / 1024 / 1024;
		log_info("%17s: %lu MB\n", entry->name, size_in_mb);
	}

	return 0;
}

int parse_keyword(int until, FILE *file, char *buf, const char* keyword, const char *__restrict fmt, ...){
	char *src;
	va_list args;
	char *read;

	while (!(src = strstr(buf, keyword)) && read) {
		read = fgets(buf, PROC_MAX_LINE, file);
	}

	if (!read) {
		log_info("Failed\n", keyword);
		return -EAGAIN;
	}

	va_start (args, fmt);
	vsscanf(src, fmt, args);
	fgets(buf, PROC_MAX_LINE, file);
	va_end (args);

	return 0;
}

int parse_zone_info(struct zone_info **zone)
{
	FILE *file;
	char line[PROC_MAX_LINE], zone_name[ZONENAMELEN];
	int node;

	file = fopen(ZONEINFO, "r");
	if (!file) {
		return -EINVAL;
	}

	fgets(line, PROC_MAX_LINE, file);
	for (;;) {
		if (parse_keyword(1, file, line, "Node", "Node %d, zone %s", &node, zone_name))
			break;

		*zone = calloc(sizeof(struct zone_info), 1);
		(*zone)->node = node;
		strncpy((*zone)->name, zone_name, ZONENAMELEN);

		if (parse_keyword(0, file, line, "free", "free %d", &(*zone)->free))
			continue;
		if (parse_keyword(0, file, line, "min", "min %d", &(*zone)->min))
			continue;
		if (parse_keyword(0, file, line, "low", "low %d", &(*zone)->low))
			continue;
		if (parse_keyword(0, file, line, "spanned", "spanned %d", &(*zone)->spanned))
			continue;
		if (parse_keyword(0, file, line, "present", "present %d", &(*zone)->present))
			continue;
		if (parse_keyword(0, file, line, "managed", "managed %d", &(*zone)->managed))
			continue;
		if (parse_keyword(1, file, line, "start_pfn", "start_pfn: %d", &(*zone)->start_pfn))
			continue;
		zone = &(*zone)->next_zone;
	}
	return 0;
}