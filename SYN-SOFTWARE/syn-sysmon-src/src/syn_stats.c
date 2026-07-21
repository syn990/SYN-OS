/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SYSMON (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_stats.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_cpu_line(const char *line, syn_cpu_ticks *out) {
	unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
	/* guest/guest_nice fields (9th/10th) exist on modern kernels but are
	 * already folded into user/nice by the kernel — not read separately. */
	int n = sscanf(line, "%*s %llu %llu %llu %llu %llu %llu %llu %llu",
	                &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
	if (n < 8) {
		return -1;
	}
	out->user = user;
	out->nice = nice;
	out->system = system;
	out->idle = idle;
	out->iowait = iowait;
	out->irq = irq;
	out->softirq = softirq;
	out->steal = steal;
	return 0;
}

void syn_stats_cpu_read(syn_cpu_snapshot *out) {
	memset(out, 0, sizeof(*out));

	FILE *f = fopen("/proc/stat", "r");
	if (!f) {
		return;
	}

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "cpu", 3) != 0) {
			break; /* "cpu"/"cpuN" lines are always first and contiguous */
		}
		if (line[3] == ' ') {
			parse_cpu_line(line, &out->total);
		} else if (line[3] >= '0' && line[3] <= '9') {
			int idx = atoi(line + 3);
			if (idx >= 0 && idx < SYN_STATS_MAX_CORES) {
				parse_cpu_line(line, &out->per_core[idx]);
				if (idx + 1 > out->core_count) {
					out->core_count = idx + 1;
				}
			}
		}
	}
	fclose(f);
}

static double ticks_usage(const syn_cpu_ticks *a, const syn_cpu_ticks *b) {
	unsigned long long a_idle = a->idle + a->iowait;
	unsigned long long b_idle = b->idle + b->iowait;
	unsigned long long a_total = a_idle + a->user + a->nice + a->system + a->irq + a->softirq + a->steal;
	unsigned long long b_total = b_idle + b->user + b->nice + b->system + b->irq + b->softirq + b->steal;

	if (b_total <= a_total) {
		return 0.0;
	}
	unsigned long long total_delta = b_total - a_total;
	unsigned long long idle_delta = b_idle - a_idle;
	if (idle_delta > total_delta) {
		return 0.0;
	}
	return (double)(total_delta - idle_delta) * 100.0 / (double)total_delta;
}

double syn_stats_cpu_usage(const syn_cpu_snapshot *prev, const syn_cpu_snapshot *cur, int core) {
	if (core < 0) {
		return ticks_usage(&prev->total, &cur->total);
	}
	if (core >= prev->core_count || core >= cur->core_count) {
		return 0.0;
	}
	return ticks_usage(&prev->per_core[core], &cur->per_core[core]);
}

static unsigned long long meminfo_value(const char *line, const char *key) {
	size_t key_len = strlen(key);
	if (strncmp(line, key, key_len) != 0) {
		return 0;
	}
	const char *rest = line + key_len;
	while (*rest == ' ' || *rest == ':') {
		rest++;
	}
	return strtoull(rest, NULL, 10); /* kB unit already implied by /proc/meminfo's own contract */
}

void syn_stats_mem_read(syn_mem_snapshot *out) {
	memset(out, 0, sizeof(*out));

	FILE *f = fopen("/proc/meminfo", "r");
	if (!f) {
		return;
	}

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		unsigned long long v;
		if ((v = meminfo_value(line, "MemTotal"))) {
			out->total_kb = v;
		} else if ((v = meminfo_value(line, "MemFree"))) {
			out->free_kb = v;
		} else if ((v = meminfo_value(line, "MemAvailable"))) {
			out->available_kb = v;
		} else if ((v = meminfo_value(line, "Buffers"))) {
			out->buffers_kb = v;
		} else if ((v = meminfo_value(line, "Cached")) && strncmp(line, "SwapCached", 10) != 0) {
			out->cached_kb = v;
		} else if ((v = meminfo_value(line, "SwapTotal"))) {
			out->swap_total_kb = v;
		} else if ((v = meminfo_value(line, "SwapFree"))) {
			out->swap_free_kb = v;
		}
	}
	fclose(f);
}

int syn_stats_sensors_read(syn_sensor_reading *out, int max) {
	int count = 0;
	DIR *hwmon_dir = opendir("/sys/class/hwmon");
	if (!hwmon_dir) {
		return 0;
	}

	struct dirent *hwmon_ent;
	while (count < max && (hwmon_ent = readdir(hwmon_dir))) {
		if (hwmon_ent->d_name[0] == '.') {
			continue;
		}

		char chip_path[512];
		snprintf(chip_path, sizeof(chip_path), "/sys/class/hwmon/%s", hwmon_ent->d_name);

		char chip_name[64] = "?";
		char name_path[600];
		snprintf(name_path, sizeof(name_path), "%s/name", chip_path);
		FILE *nf = fopen(name_path, "r");
		if (nf) {
			if (fgets(chip_name, sizeof(chip_name), nf)) {
				size_t len = strlen(chip_name);
				while (len > 0 && (chip_name[len - 1] == '\n' || chip_name[len - 1] == '\r')) {
					chip_name[--len] = '\0';
				}
			}
			fclose(nf);
		}

		DIR *chip_dir = opendir(chip_path);
		if (!chip_dir) {
			continue;
		}

		struct dirent *ent;
		while (count < max && (ent = readdir(chip_dir))) {
			size_t nlen = strlen(ent->d_name);
			/* match "tempN_input" */
			if (nlen < 11 || strncmp(ent->d_name, "temp", 4) != 0 ||
			    strcmp(ent->d_name + nlen - 6, "_input") != 0) {
				continue;
			}

			char input_path[600];
			snprintf(input_path, sizeof(input_path), "%s/%s", chip_path, ent->d_name);
			FILE *tf = fopen(input_path, "r");
			if (!tf) {
				continue;
			}
			long millideg;
			int ok = fscanf(tf, "%ld", &millideg) == 1;
			fclose(tf);
			if (!ok) {
				continue;
			}

			/* tempN_label is optional — falls back to the raw "tempN" stem
			 * (input_path's basename minus "_input") when the chip doesn't
			 * name its own sensors (e.g. acpitz's single unlabeled zone). */
			char label[64] = {0};
			char label_path[600];
			snprintf(label_path, sizeof(label_path), "%.*s_label", (int)(nlen - 6), ent->d_name);
			char label_full_path[600];
			snprintf(label_full_path, sizeof(label_full_path), "%s/%s", chip_path, label_path);
			FILE *lf = fopen(label_full_path, "r");
			if (lf) {
				if (fgets(label, sizeof(label), lf)) {
					size_t len = strlen(label);
					while (len > 0 && (label[len - 1] == '\n' || label[len - 1] == '\r')) {
						label[--len] = '\0';
					}
				}
				fclose(lf);
			}
			if (label[0] == '\0') {
				snprintf(label, sizeof(label), "%.*s", (int)(nlen - 6), ent->d_name);
			}

			snprintf(out[count].label, sizeof(out[count].label), "%s: %s", chip_name, label);
			out[count].celsius = (double)millideg / 1000.0;
			count++;
		}
		closedir(chip_dir);
	}
	closedir(hwmon_dir);
	return count;
}
