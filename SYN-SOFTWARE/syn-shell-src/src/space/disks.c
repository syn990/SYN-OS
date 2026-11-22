/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "disks.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

int disks_list(Disk *out, int max) {
	char drives[512];
	if (!GetLogicalDriveStringsA(sizeof drives, drives)) {
		return 0;
	}
	int n = 0;
	for (const char *d = drives; *d && n < max; d += strlen(d) + 1) {
		UINT type = GetDriveTypeA(d);
		ULARGE_INTEGER avail, total, free_bytes;
		if (type == DRIVE_NO_ROOT_DIR || !GetDiskFreeSpaceExA(d, &avail, &total, &free_bytes)) {
			continue; /* an empty card reader or optical drive */
		}
		Disk *k = &out[n++];
		snprintf(k->path, sizeof k->path, "%c:/", d[0]);
		char name[128] = "", fs[32] = "";
		GetVolumeInformationA(d, name, sizeof name, NULL, NULL, NULL, fs, sizeof fs);
		snprintf(k->label, sizeof k->label, "%s%s%s", name, name[0] ? "  " : "", fs);
		k->total = total.QuadPart;
		k->free = free_bytes.QuadPart;
		k->remote = type == DRIVE_REMOTE;
	}
	return n;
}

#else
#include <mntent.h>
#include <sys/statvfs.h>

static bool one_of(const char *s, const char *const *list) {
	for (; *list; list++) {
		if (!strcmp(s, *list)) {
			return true;
		}
	}
	return false;
}

int disks_list(Disk *out, int max) {
	static const char *const REMOTE[] = {"nfs", "nfs4", "cifs", "smb3", "smbfs", "fuse.sshfs",
	                                     "9p",  "ceph", "afs",  "davfs", NULL};
	/* kept in memory or made up by the kernel: nothing a scan frees space on */
	static const char *const NOT_A_DISK[] = {"tmpfs", "devtmpfs", "ramfs", "squashfs", "overlay",
	                                         "efivarfs", "fuse.portal", NULL};
	FILE *f = setmntent("/proc/self/mounts", "r");
	if (!f) {
		return 0;
	}
	int n = 0;
	struct mntent *m;
	while (n < max && (m = getmntent(f)) != NULL) {
		struct statvfs v;
		if (one_of(m->mnt_type, NOT_A_DISK) || statvfs(m->mnt_dir, &v) != 0 || v.f_blocks == 0) {
			continue;
		}
		bool seen = false; /* a device mounted twice (bind mounts) is listed once */
		for (int i = 0; i < n && !seen; i++) {
			seen = !strncmp(out[i].label, m->mnt_fsname, strlen(m->mnt_fsname)) &&
			       out[i].label[strlen(m->mnt_fsname)] == ' ';
		}
		if (seen) {
			continue;
		}
		Disk *k = &out[n++];
		snprintf(k->path, sizeof k->path, "%s", m->mnt_dir);
		snprintf(k->label, sizeof k->label, "%s  %s", m->mnt_fsname, m->mnt_type);
		k->total = (uint64_t)v.f_blocks * v.f_frsize;
		k->free = (uint64_t)v.f_bavail * v.f_frsize;
		k->remote = one_of(m->mnt_type, REMOTE);
	}
	endmntent(f);
	return n;
}
#endif
