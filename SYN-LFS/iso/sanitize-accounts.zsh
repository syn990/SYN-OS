#!/usr/bin/env zsh
# sanitize-accounts.zsh ETC OUT: the system accounts only (uid/gid below
# 1000, and nobody) from ETC's passwd, group, shadow and gshadow, with any
# other user taken out of group member lists, written to OUT. build.zsh iso
# puts these on the ISO in place of the build image's own, so a user added
# with `build.zsh user` stays in the build image and off the ISO.
setopt err_exit
etc=$1 out=$2

awk -F: '$3 < 1000 || $3 == 65534' "$etc/passwd" > "$out/passwd"
users=" $(cut -d: -f1 "$out/passwd" | tr '\n' ' ')"

keep='function keep(list,   n, i, a, r) {
	n = split(list, a, ","); r = ""
	for (i = 1; i <= n; i++)
		if (index(users, " " a[i] " ")) r = r (r == "" ? "" : ",") a[i]
	return r
}'
awk -F: -v OFS=: -v users="$users" "$keep"'
	$3 < 1000 || $3 == 65534 { $4 = keep($4); print }' "$etc/group" > "$out/group"
groups=" $(cut -d: -f1 "$out/group" | tr '\n' ' ')"

awk -F: -v users="$users" 'index(users, " " $1 " ")' "$etc/shadow" > "$out/shadow"
if [ -f "$etc/gshadow" ]; then
	awk -F: -v OFS=: -v users="$users" -v groups="$groups" "$keep"'
		index(groups, " " $1 " ") { $3 = keep($3); $4 = keep($4); print }' \
		"$etc/gshadow" > "$out/gshadow"
	chmod 600 "$out/gshadow"
fi
chmod 644 "$out/passwd" "$out/group"
chmod 600 "$out/shadow"
