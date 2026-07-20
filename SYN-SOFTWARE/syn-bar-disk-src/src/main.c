/* ------------------------------------------------------------------------
 *                      S Y N - B A R - D I S K
 *
 *   Waybar custom/disk module backend: prints one JSON line (text,
 *   tooltip, class, percentage) for the root filesystem's usage, then
 *   exits. Waybar re-execs this on every interval tick (60s per
 *   config.jsonc) since this module has no persistent "exec" contract
 *   like syn-bar-window-title does.
 *
 *   Replaces the old syn-bar-disk.zsh, which shelled out to df/awk
 *   twice plus a python3 -c one-liner just to serialize three fields
 *   as JSON — statvfs(2)/getmntent(3) do the same job with zero forked
 *   processes.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-DISK (Waybar)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <mntent.h>

/* Same 4 pseudo-filesystem types the old script's "df -x" list excluded.
 * f_blocks==0 (checked below) already catches most dummy mounts, but a
 * few real-looking ones (e.g. overlay) can report nonzero blocks, so
 * both checks stay to match prior behavior exactly. */
static int is_excluded_fstype(const char *type) {
	static const char *excluded[] = {"tmpfs", "devtmpfs", "squashfs", "overlay"};
	for (size_t i = 0; i < sizeof(excluded) / sizeof(excluded[0]); i++) {
		if (strcmp(type, excluded[i]) == 0) {
			return 1;
		}
	}
	return 0;
}

/* Minimal JSON string escaper — tooltip text is mount paths and
 * human-readable sizes, so only quotes/backslashes/newlines can appear. */
static void print_json_escaped(const char *s) {
	for (const char *p = s; *p; p++) {
		switch (*p) {
		case '"': fputs("\\\"", stdout); break;
		case '\\': fputs("\\\\", stdout); break;
		case '\n': fputs("\\n", stdout); break;
		default: fputc(*p, stdout);
		}
	}
}

static void human_size(unsigned long long bytes, char *out, size_t outlen) {
	static const char *units[] = {"B", "K", "M", "G", "T", "P"};
	double size = (double)bytes;
	size_t unit = 0;
	while (size >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
		size /= 1024.0;
		unit++;
	}
	if (unit == 0) {
		snprintf(out, outlen, "%.0f%s", size, units[unit]);
	} else {
		snprintf(out, outlen, "%.1f%s", size, units[unit]);
	}
}

int main(void) {
	struct statvfs root_st;
	if (statvfs("/", &root_st) != 0) {
		fprintf(stderr, "syn-bar-disk: statvfs(\"/\") failed\n");
		return 1;
	}

	unsigned long long root_total = (unsigned long long)root_st.f_blocks * root_st.f_frsize;
	unsigned long long root_avail = (unsigned long long)root_st.f_bavail * root_st.f_frsize;
	unsigned long long root_used = root_total - (unsigned long long)root_st.f_bfree * root_st.f_frsize;
	int root_pct = root_total > 0
		? (int)((double)root_used / (double)root_total * 100.0 + 0.5)
		: 0;

	const char *class = "normal";
	if (root_pct >= 90) {
		class = "critical";
	} else if (root_pct >= 75) {
		class = "warning";
	}

	char used_str[32], total_str[32];
	human_size(root_used, used_str, sizeof(used_str));
	human_size(root_total, total_str, sizeof(total_str));

	FILE *mounts = setmntent("/proc/self/mounts", "r");
	char tooltip[4096] = {0};
	size_t tooltip_len = 0;
	if (mounts) {
		struct mntent *ent;
		while ((ent = getmntent(mounts)) != NULL) {
			if (is_excluded_fstype(ent->mnt_type)) {
				continue;
			}
			struct statvfs st;
			if (statvfs(ent->mnt_dir, &st) != 0 || st.f_blocks == 0) {
				continue;
			}
			unsigned long long total = (unsigned long long)st.f_blocks * st.f_frsize;
			unsigned long long avail = (unsigned long long)st.f_bavail * st.f_frsize;
			unsigned long long used = total - (unsigned long long)st.f_bfree * st.f_frsize;
			int pct = total > 0 ? (int)((double)used / (double)total * 100.0 + 0.5) : 0;
			char u[32], t[32];
			human_size(used, u, sizeof(u));
			human_size(total, t, sizeof(t));
			int n = snprintf(tooltip + tooltip_len, sizeof(tooltip) - tooltip_len,
				"%-20s %6s / %-6s (%d%%)\n", ent->mnt_dir, u, t, pct);
			if (n < 0 || (size_t)n >= sizeof(tooltip) - tooltip_len) {
				break;
			}
			tooltip_len += (size_t)n;
		}
		endmntent(mounts);
	}
	/* Strip the trailing newline df's output (and the old script) left off. */
	if (tooltip_len > 0 && tooltip[tooltip_len - 1] == '\n') {
		tooltip[tooltip_len - 1] = '\0';
	}

	printf("{\"text\": \"%s/%s\", \"tooltip\": \"", used_str, total_str);
	print_json_escaped(tooltip);
	printf("\", \"class\": \"%s\", \"percentage\": %d}\n", class, root_pct);

	return 0;
}
