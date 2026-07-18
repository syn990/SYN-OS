/* ------------------------------------------------------------------------
 *              S Y N - B A R - W I N D O W - T I T L E
 *
 *   Waybar custom/window-title module backend: prints the focused
 *   window's title on stdout, one line per change, for as long as it
 *   runs. Waybar custom modules with no "interval" key run their "exec"
 *   as a persistent process and repaint on every line - this is that
 *   process.
 *
 *   Talks to LabWC directly via wlr-foreign-toplevel-management, the
 *   same protocol waybar's own built-in wlr/taskbar module uses (see
 *   docs/waybar.md) - there is no generic "window title" module left in
 *   current waybar upstream, and no non-AUR CLI that exposes this, so
 *   this talks the protocol directly instead.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-WINDOW-TITLE (Waybar)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

struct toplevel {
	struct toplevel *next;
	struct zwlr_foreign_toplevel_handle_v1 *handle;
	char *title;
	int activated;
};

static struct toplevel *toplevels = NULL;
static struct toplevel *active = NULL;
static char *last_printed = NULL;

static struct toplevel *toplevel_find(struct zwlr_foreign_toplevel_handle_v1 *handle) {
	for (struct toplevel *t = toplevels; t; t = t->next) {
		if (t->handle == handle) {
			return t;
		}
	}
	return NULL;
}

static void toplevel_remove(struct zwlr_foreign_toplevel_handle_v1 *handle) {
	struct toplevel **link = &toplevels;
	while (*link) {
		if ((*link)->handle == handle) {
			struct toplevel *dead = *link;
			*link = dead->next;
			free(dead->title);
			free(dead);
			return;
		}
		link = &(*link)->next;
	}
}

static void emit(const char *title) {
	if (last_printed && title && strcmp(last_printed, title) == 0) {
		return;
	}
	printf("%s\n", title ? title : "");
	fflush(stdout);
	free(last_printed);
	last_printed = title ? strdup(title) : strdup("");
}

static void handle_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
		const char *title) {
	(void)data;
	struct toplevel *t = toplevel_find(handle);
	if (!t) {
		return;
	}
	free(t->title);
	t->title = strdup(title);
}

/* libwayland's dispatcher aborts if a compositor sends an event with no
 * registered handler in the listener struct — every opcode below needs a
 * real function pointer even though these ones do nothing. */
static void handle_noop(void) {
}

static void handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
		struct wl_array *state) {
	(void)data;
	struct toplevel *t = toplevel_find(handle);
	if (!t) {
		return;
	}
	t->activated = 0;
	uint32_t *entry;
	wl_array_for_each(entry, state) {
		if (*entry == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
			t->activated = 1;
		}
	}
	if (t->activated) {
		active = t;
	}
}

static void handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
	(void)data;
	struct toplevel *t = toplevel_find(handle);
	if (t && t == active) {
		emit(t->title);
	}
}

static void handle_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
	(void)data;
	int was_active = (active && active->handle == handle);
	zwlr_foreign_toplevel_handle_v1_destroy(handle);
	toplevel_remove(handle);
	if (was_active) {
		active = NULL;
		emit("");
	}
}

static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
	.title = handle_title,
	.app_id = (void (*)(void *, struct zwlr_foreign_toplevel_handle_v1 *,
		const char *))handle_noop,
	.output_enter = (void (*)(void *, struct zwlr_foreign_toplevel_handle_v1 *,
		struct wl_output *))handle_noop,
	.output_leave = (void (*)(void *, struct zwlr_foreign_toplevel_handle_v1 *,
		struct wl_output *))handle_noop,
	.state = handle_state,
	.done = handle_done,
	.closed = handle_closed,
	.parent = (void (*)(void *, struct zwlr_foreign_toplevel_handle_v1 *,
		struct zwlr_foreign_toplevel_handle_v1 *))handle_noop,
};

static void manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager,
		struct zwlr_foreign_toplevel_handle_v1 *handle) {
	(void)data;
	(void)manager;
	struct toplevel *t = calloc(1, sizeof(*t));
	t->handle = handle;
	t->title = strdup("");
	t->next = toplevels;
	toplevels = t;
	zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener, NULL);
}

static void manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager) {
	(void)data;
	zwlr_foreign_toplevel_manager_v1_destroy(manager);
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
	.toplevel = manager_toplevel,
	.finished = manager_finished,
};

static struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager = NULL;

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version) {
	(void)data;
	if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
		toplevel_manager = wl_registry_bind(registry, name,
			&zwlr_foreign_toplevel_manager_v1_interface, version);
		zwlr_foreign_toplevel_manager_v1_add_listener(toplevel_manager,
			&manager_listener, NULL);
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = (void (*)(void *, struct wl_registry *, uint32_t))handle_noop,
};

int main(void) {
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "syn-bar-window-title: cannot connect to Wayland display\n");
		return 1;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	if (!toplevel_manager) {
		fprintf(stderr,
			"syn-bar-window-title: compositor has no "
			"zwlr_foreign_toplevel_manager_v1\n");
		return 1;
	}

	while (wl_display_dispatch(display) != -1) {
		/* handle_done() emits on every state settle */
	}

	return 0;
}
