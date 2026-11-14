/* ------------------------------------------------------------------------
 *   SYN-NETJUMPER: the SYN-OS web browser. Everything you touch is here:
 *   tabs, the address bar, downloads, find, history and the desktop's live
 *   theme. Pages are drawn by WebKitGTK, in its own sandboxed processes.
 *
 *   usage: syn-netjumper [ADDRESS...]
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NETJUMPER
 *   Inventor   : William Hayward-Holland (Syntax990)
 *   Written by : the Syntax Corporation
 *   Built on   : WebKitGTK and GTK 3
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <gdk/gdkkeysyms.h>
#include <libsecret/secret.h>
#include <string.h>

#define APP_ID "com.syntax990.synnetjumper"
#define APP_NAME "SYN-NETJUMPER"
#define SEARCH "https://duckduckgo.com/?q="
#define HISTORY_MAX 3000
#define STATUS_MS 4000

enum { H_URI, H_TITLE, H_COLUMNS };

typedef struct {
	WebKitWebView *web;
	GtkWidget *title;
	GtkWidget *tab_label;
} Tab;

static struct {
	GtkApplication *app;
	GtkWidget *win, *book, *bar, *entry, *back, *ahead, *reload, *status;
	GtkWidget *findbar, *findentry, *findcount;
	GtkWidget *savebar, *savelabel;
	char *save_origin, *save_user, *save_pass;
	WebKitWebContext *context;
	WebKitSettings *settings;
	GtkCssProvider *css;
	GtkListStore *history;
	char *theme_name, *data_dir;
	char bg[8], bg_alt[8], panel[8], hover[8], accent[8], text[8], border[8], urgent[8];
	char active_bg[8], active_text[8];
	guint status_timer;
} G;

static Tab *tab_new(const char *uri, WebKitWebView *related, gboolean focus);
static void sync_chrome(Tab *t);
static void load_home(Tab *t);
static void status_set(const char *text, gboolean keep);
static void show_save_prompt(const char *origin, const char *user, const char *pass);

/* ------------------------------------------------------------------------
 *   Saved passwords: kept in the desktop keyring (libsecret), the same
 *   place the rest of SYN-OS keeps its secrets. Chrome's own exported CSV
 *   is imported here; SYN-NETJUMPER never reads Chrome's encrypted store.
 *   A page is only ever filled with, or allowed to save, a login for its
 *   OWN real address, read from the tab, not from anything the page claims.
 * ------------------------------------------------------------------------ */
static const SecretSchema *pw_schema(void)
{
	static const SecretSchema s = {
		"com.syntax990.synnetjumper.Password", SECRET_SCHEMA_NONE,
		{{"origin", SECRET_SCHEMA_ATTRIBUTE_STRING}, {"username", SECRET_SCHEMA_ATTRIBUTE_STRING}, {"NULL", 0}}};
	return &s;
}

/* a string as a JSON literal, so it drops safely into a line of injected JS */
static char *json_str(const char *s)
{
	GString *o = g_string_new("\"");
	for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
		switch (*p) {
		case '"': g_string_append(o, "\\\""); break;
		case '\\': g_string_append(o, "\\\\"); break;
		case '\n': g_string_append(o, "\\n"); break;
		case '\r': g_string_append(o, "\\r"); break;
		case '\t': g_string_append(o, "\\t"); break;
		case '<': g_string_append(o, "\\u003c"); break;
		default:
			if (*p < 0x20) g_string_append_printf(o, "\\u%04x", *p);
			else g_string_append_c(o, *p);
		}
	}
	g_string_append_c(o, '"');
	return g_string_free(o, FALSE);
}

/* scheme://host[:port], as a browser's location.origin writes it */
static char *origin_of(const char *uri)
{
	if (!uri) return NULL;
	GUri *u = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
	if (!u) return NULL;
	const char *scheme = g_uri_get_scheme(u), *host = g_uri_get_host(u);
	char *out = NULL;
	if (scheme && host && *host && (g_str_equal(scheme, "http") || g_str_equal(scheme, "https"))) {
		int port = g_uri_get_port(u);
		gboolean dflt = port < 0 || (g_str_equal(scheme, "https") && port == 443) || (g_str_equal(scheme, "http") && port == 80);
		out = dflt ? g_strdup_printf("%s://%s", scheme, host) : g_strdup_printf("%s://%s:%d", scheme, host, port);
	}
	g_uri_unref(u);
	return out;
}

static gboolean pw_store(const char *origin, const char *user, const char *pass)
{
	char *label = g_strdup_printf("SYN-NETJUMPER: %s @ %s", user && *user ? user : "(no name)", origin);
	gboolean ok = secret_password_store_sync(pw_schema(), SECRET_COLLECTION_DEFAULT, label, pass, NULL, NULL,
		"origin", origin, "username", user ? user : "", NULL);
	g_free(label);
	return ok;
}

/* the first saved login for this origin, if any; caller frees both strings */
static gboolean pw_lookup_first(const char *origin, char **user_out, char **pass_out)
{
	GList *items = secret_password_search_sync(pw_schema(), SECRET_SEARCH_LOAD_SECRETS | SECRET_SEARCH_UNLOCK, NULL, NULL, "origin", origin, NULL);
	gboolean found = FALSE;
	if (items) {
		SecretRetrievable *it = items->data;
		GHashTable *attrs = secret_retrievable_get_attributes(it);
		SecretValue *val = secret_retrievable_retrieve_secret_sync(it, NULL, NULL);
		if (val) {
			*user_out = g_strdup(g_hash_table_lookup(attrs, "username"));
			*pass_out = g_strdup(secret_value_get_text(val));
			secret_value_unref(val);
			found = TRUE;
		}
		g_hash_table_unref(attrs);
	}
	g_list_free_full(items, g_object_unref);
	return found;
}

/* one CSV line into its fields, honouring "quotes" and "" */
static char **csv_row(const char *line)
{
	GPtrArray *a = g_ptr_array_new();
	GString *cur = g_string_new(NULL);
	gboolean q = FALSE;
	for (const char *p = line; *p; p++) {
		if (*p == '"') {
			if (q && p[1] == '"') { g_string_append_c(cur, '"'); p++; }
			else q = !q;
		} else if (*p == ',' && !q) {
			g_ptr_array_add(a, g_string_free(cur, FALSE));
			cur = g_string_new(NULL);
		} else {
			g_string_append_c(cur, *p);
		}
	}
	g_ptr_array_add(a, g_string_free(cur, FALSE));
	g_ptr_array_add(a, NULL);
	return (char **)g_ptr_array_free(a, FALSE);
}

/* Chrome's exported passwords (name,url,username,password,note); returns how many were stored, or -1 */
static int pw_import(const char *path)
{
	char *data = NULL;
	if (!g_file_get_contents(path, &data, NULL, NULL)) return -1;
	char **lines = g_strsplit(data, "\n", -1);
	g_free(data);
	int url_col = -1, user_col = -1, pass_col = -1, n = 0;
	if (lines[0]) {
		char **head = csv_row(lines[0]);
		for (int i = 0; head[i]; i++) {
			char *h = g_ascii_strdown(g_strchomp(head[i]), -1);
			if (g_str_equal(h, "url")) url_col = i;
			else if (g_str_equal(h, "username")) user_col = i;
			else if (g_str_equal(h, "password")) pass_col = i;
			g_free(h);
		}
		g_strfreev(head);
	}
	if (url_col >= 0 && pass_col >= 0) {
		for (int l = 1; lines[l]; l++) {
			if (!*g_strchomp(lines[l])) continue;
			char **f = csv_row(lines[l]);
			int cols = 0;
			while (f[cols]) cols++;
			if (url_col < cols && pass_col < cols && *f[pass_col]) {
				char *origin = origin_of(f[url_col]);
				if (origin) {
					if (pw_store(origin, user_col >= 0 && user_col < cols ? f[user_col] : "", f[pass_col])) n++;
					g_free(origin);
				}
			}
			g_strfreev(f);
		}
	}
	g_strfreev(lines);
	return url_col < 0 || pass_col < 0 ? -1 : n;
}

/* runs in every page once it's drawn: fills a login this origin has saved, and
   offers to save a new one when a form with a password is submitted */
static const char AUTOFILL_JS[] =
	"(function(){"
	"function pws(){return [].slice.call(document.querySelectorAll('input[type=password]')).filter(function(e){return e.offsetParent});}"
	"function userFor(p){var ins=[].slice.call(document.querySelectorAll('input'));var i=ins.indexOf(p);"
	"for(var j=i-1;j>=0;j--){var t=(ins[j].type||'text').toLowerCase();if(t=='text'||t=='email'||t=='tel')return ins[j];}return null;}"
	"window.__synpwFill=function(u,p){var ps=pws();if(!ps.length)return;var pw=ps[0],us=userFor(pw);"
	"if(us&&u){us.focus();us.value=u;us.dispatchEvent(new Event('input',{bubbles:true}));us.dispatchEvent(new Event('change',{bubbles:true}));}"
	"if(p){pw.focus();pw.value=p;pw.dispatchEvent(new Event('input',{bubbles:true}));pw.dispatchEvent(new Event('change',{bubbles:true}));}pw.blur();};"
	"function query(){if(pws().length)window.webkit.messageHandlers.synpw.postMessage({t:'query'});}"
	"if(document.readyState!='loading')query();else document.addEventListener('DOMContentLoaded',query);"
	"document.addEventListener('submit',function(e){var f=e.target;if(!f||!f.querySelector)return;"
	"var pw=f.querySelector('input[type=password]');if(!pw||!pw.value)return;var us=userFor(pw);"
	"window.webkit.messageHandlers.synpw.postMessage({t:'save',user:us?us.value:'',pass:pw.value});},true);"
	"})();";

static void on_save_yes(GtkWidget *w, gpointer u)
{
	if (G.save_origin && G.save_pass) {
		pw_store(G.save_origin, G.save_user, G.save_pass);
		status_set("Password saved to the keyring.", FALSE);
	}
	gtk_widget_hide(G.savebar);
}

static void on_save_no(GtkWidget *w, gpointer u)
{
	gtk_widget_hide(G.savebar);
}

static void show_save_prompt(const char *origin, const char *user, const char *pass)
{
	/* don't nag when this exact login is already saved */
	char *had_u = NULL, *had_p = NULL;
	if (pw_lookup_first(origin, &had_u, &had_p)) {
		gboolean same = !g_strcmp0(had_p, pass) && !g_strcmp0(had_u ? had_u : "", user ? user : "");
		g_free(had_u);
		g_free(had_p);
		if (same) return;
	}
	g_free(G.save_origin);
	g_free(G.save_user);
	g_free(G.save_pass);
	G.save_origin = g_strdup(origin);
	G.save_user = g_strdup(user ? user : "");
	G.save_pass = g_strdup(pass);
	char *msg = g_strdup_printf("Save the password for %s at %s?", *G.save_user ? G.save_user : "this login", origin);
	gtk_label_set_text(GTK_LABEL(G.savelabel), msg);
	g_free(msg);
	gtk_widget_show(G.savebar);
}

static void on_script_message(WebKitUserContentManager *m, WebKitJavascriptResult *r, Tab *t)
{
	JSCValue *v = webkit_javascript_result_get_js_value(r);
	if (!v || !jsc_value_is_object(v)) return;
	JSCValue *tp = jsc_value_object_get_property(v, "t");
	char *type = jsc_value_is_string(tp) ? jsc_value_to_string(tp) : NULL;
	g_object_unref(tp);
	char *origin = origin_of(webkit_web_view_get_uri(t->web));
	if (origin && type) {
		if (g_str_equal(type, "query")) {
			char *user = NULL, *pass = NULL;
			if (pw_lookup_first(origin, &user, &pass)) {
				char *ju = json_str(user), *jp = json_str(pass);
				char *js = g_strdup_printf("window.__synpwFill(%s,%s)", ju, jp);
				webkit_web_view_evaluate_javascript(t->web, js, -1, NULL, NULL, NULL, NULL, NULL);
				g_free(js);
				g_free(ju);
				g_free(jp);
			}
			g_free(user);
			g_free(pass);
		} else if (g_str_equal(type, "save")) {
			JSCValue *up = jsc_value_object_get_property(v, "user"), *pp = jsc_value_object_get_property(v, "pass");
			char *user = jsc_value_to_string(up), *pass = jsc_value_to_string(pp);
			if (pass && *pass) show_save_prompt(origin, user, pass);
			g_free(user);
			g_free(pass);
			g_object_unref(up);
			g_object_unref(pp);
		}
	}
	g_free(origin);
	g_free(type);
}

/* ---- the desktop's theme, read live ---- */

/* ---- the desktop's theme, read live ---- */
static void color_of(GHashTable *t, const char *key, char *out, const char *fallback)
{
	const char *v = g_hash_table_lookup(t, key);
	gboolean ok = v && strlen(v) == 7 && v[0] == '#';
	for (int i = 1; ok && i < 7; i++) ok = g_ascii_isxdigit(v[i]);
	g_strlcpy(out, ok ? v : fallback, 8);
}

static char *live_theme_name(void)
{
	char *path = g_build_filename(g_get_user_config_dir(), "syn-os", "current-theme", NULL), *s = NULL;
	if (g_file_get_contents(path, &s, NULL, NULL)) g_strstrip(s);
	g_free(path);
	return s ? s : g_strdup("SYN-OS-M141");
}

static void overlay_refresh_all(void);

static void apply_theme(const char *name)
{
	char *file = g_strconcat(name, ".theme", NULL);
	char *path = g_build_filename(g_get_user_config_dir(), "syn-os", "themes", file, NULL), *data = NULL;
	GHashTable *t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	if (g_file_get_contents(path, &data, NULL, NULL)) {
		char **lines = g_strsplit(data, "\n", -1);
		for (char **l = lines; *l; l++) {
			char *eq = strchr(*l, '=');
			if (!eq || **l == '#') continue;
			char *v = g_strdup(eq + 1);
			g_strstrip(v);
			if (*v == '"') { memmove(v, v + 1, strlen(v)); char *q = strchr(v, '"'); if (q) *q = 0; }
			g_hash_table_insert(t, g_strndup(*l, eq - *l), v);
		}
		g_strfreev(lines);
		g_free(data);
	}
	color_of(t, "SYN_BG", G.bg, "#000000");
	color_of(t, "SYN_BG_ALT", G.bg_alt, "#0a0a0a");
	color_of(t, "SYN_PANEL", G.panel, "#1a0000");
	color_of(t, "SYN_PANEL_HOVER", G.hover, "#330000");
	color_of(t, "SYN_ACCENT", G.accent, "#e00000");
	color_of(t, "SYN_TEXT", G.text, "#ffffff");
	color_of(t, "SYN_BORDER", G.border, "#555555");
	color_of(t, "SYN_URGENT", G.urgent, "#ff2020");

	/* the same per-family rule the rest of SYN-OS uses for what "selected" looks like */
	const char *family = g_hash_table_lookup(t, "SYN_THEME_FAMILY");
	gboolean flat = family && (strstr(family, "FLATLINE") || strstr(family, "HALO"));
	gboolean slab = family && strstr(family, "SLAB");
	gboolean matrix = g_str_has_suffix(name, "-MATRIX");
	gboolean win95 = g_str_has_suffix(name, "-WIN95");
	if (flat || matrix) {
		g_strlcpy(G.active_bg, matrix ? G.panel : G.bg, 8);
		g_strlcpy(G.active_text, G.accent, 8);
	} else {
		g_strlcpy(G.active_bg, G.accent, 8);
		g_strlcpy(G.active_text, slab ? G.bg : "#ffffff", 8);
	}
	if (win95) g_strlcpy(G.text, "#000000", 8);

	char *css = g_strdup_printf(
		"* { font-family: \"Terminus\", monospace; font-size: 16px; border-radius: 0; box-shadow: none; text-shadow: none;"
		"    -gtk-icon-shadow: none; background-image: none; outline-width: 0; }"
		"window, #bar, #findbar, notebook > header { background-color: %s; color: %s; }"
		"#bar, #findbar { padding: 4px 6px; }"
		"#bar { border-bottom: 1px solid %s; }"
		"#findbar { border-top: 1px solid %s; }"
		"#brand { color: %s; font-weight: bold; padding: 0 8px; }"
		"button { background-color: %s; color: %s; border: 1px solid %s; padding: 2px 10px; min-height: 0; min-width: 0; }"
		"button:hover { background-color: %s; color: %s; }"
		"button:disabled, button:disabled label { color: %s; border-color: %s; background-color: %s; }"
		"entry { background-color: %s; color: %s; border: 1px solid %s; padding: 3px 8px; min-height: 0; caret-color: %s; }"
		"entry:focus { border-color: %s; }"
		"entry progress { background-color: transparent; border: 0; border-bottom: 2px solid %s; }"
		"entry selection, selection { background-color: %s; color: %s; }"
		"notebook { background-color: %s; }"
		"notebook > header { border: 0; padding: 0; }"
		"notebook > header > tabs > tab { background-color: %s; color: %s; border: 0; border-right: 1px solid %s; padding: 3px 4px 3px 10px; min-height: 0; }"
		"notebook > header > tabs > tab:checked { background-color: %s; color: %s; font-weight: bold; }"
		"notebook > header > tabs > tab:checked label { color: %s; }"
		"notebook > header > tabs > tab:hover:not(:checked) { background-color: %s; }"
		"notebook > header arrow { background-color: %s; color: %s; }"
		".tabclose { border: 0; padding: 0 6px; background-color: transparent; color: inherit; }"
		".tabclose:hover { background-color: %s; color: #ffffff; }"
		"#status { background-color: %s; color: %s; border: 1px solid %s; padding: 1px 8px; }"
		"tooltip, tooltip * { background-color: %s; color: %s; }"
		"treeview { background-color: %s; color: %s; }"
		"treeview:selected { background-color: %s; color: %s; }"
		"scrollbar { background-color: %s; }"
		"scrollbar slider { background-color: %s; min-width: 8px; min-height: 8px; border: 0; }",
		G.bg_alt, G.text, G.accent, G.accent, G.accent,
		G.panel, G.text, G.accent, G.active_bg, G.active_text, G.border, G.border, G.bg_alt,
		G.bg, G.text, G.border, G.accent, G.accent, G.accent, G.active_bg, G.active_text,
		G.bg,
		G.bg_alt, G.text, G.border, G.active_bg, G.active_text, G.active_text, G.hover, G.bg_alt, G.text,
		G.urgent, G.panel, G.text, G.accent, G.panel, G.text, G.panel, G.text, G.active_bg, G.active_text, G.bg_alt, G.accent);
	gtk_css_provider_load_from_data(G.css, css, -1, NULL);
	g_free(css);

	g_hash_table_destroy(t);
	g_free(path);
	g_free(file);
	g_free(G.theme_name);
	G.theme_name = g_strdup(name);
	overlay_refresh_all();
}

/* ---- SYN-TUBE: the desktop's theme laid over youtube.com ----
   Not an app of its own: YouTube itself, in this browser, wearing the live
   SYN-OS palette and Terminus. A user stylesheet limited to YouTube's hosts,
   rebuilt in every tab when the desktop theme changes. YouTube colours its
   pages through --yt-spec-* custom properties, so most of this sets those;
   the rest covers what it paints directly. */
static const char *const YT_HOSTS[] = {
	"https://youtube.com/*", "https://*.youtube.com/*", NULL
};

static const char YT_CSS[] =
	"html, html[dark], html:not([dark]), ytd-app, ytd-app[dark], tp-yt-iron-dropdown, ytd-popup-container {"
	"  --yt-spec-base-background: %1$s; --yt-spec-raised-background: %2$s; --yt-spec-menu-background: %2$s;"
	"  --yt-spec-general-background-a: %1$s; --yt-spec-general-background-b: %1$s; --yt-spec-general-background-c: %2$s;"
	"  --yt-spec-brand-background-solid: %2$s; --yt-spec-brand-background-primary: %2$s; --yt-spec-brand-background-secondary: %3$s;"
	"  --yt-spec-additive-background: %3$s; --yt-spec-10-percent-layer: %4$s; --yt-spec-outline: %4$s;"
	"  --yt-spec-badge-chip-background: %3$s; --yt-spec-button-chip-background-hover: %5$s; --yt-spec-touch-response: %5$s;"
	"  --yt-spec-text-primary: %6$s; --yt-spec-text-secondary: %7$s; --yt-spec-text-disabled: %4$s;"
	"  --yt-spec-icon-active-other: %6$s; --yt-spec-icon-inactive: %7$s; --yt-spec-icon-disabled: %4$s;"
	"  --yt-spec-call-to-action: %8$s; --yt-spec-themed-blue: %8$s; --yt-spec-brand-link-text: %8$s;"
	"  --yt-spec-static-brand-red: %8$s; --yt-spec-red-indicator: %8$s; --yt-spec-brand-icon-active: %8$s;"
	"  --yt-spec-mono-filled: %8$s; --yt-spec-static-overlay-background-brand: %8$s;"
	"  --yt-spec-text-primary-inverse: %1$s; --yt-spec-inverted-background: %6$s;"
	"  --yt-spec-wordmark-text: %8$s; --yt-spec-shadow: transparent;"
	"}"
	"html, body, ytd-app { background: %1$s !important; color: %6$s !important; }"
	"html, body, ytd-app, ytd-app *, ytd-popup-container *, tp-yt-paper-dialog *, input, button, textarea,"
	"  .ytp-tooltip-text, .ytp-menuitem, .ytp-menuitem *, .ytp-time-display, .ytp-chapter-title {"
	"  font-family: \"Terminus\", monospace !important; letter-spacing: 0 !important; }"
	"#masthead-container, ytd-masthead, #background.ytd-masthead, #frosted-glass,"
	"  #guide-content, tp-yt-app-drawer #contentContainer, ytd-mini-guide-renderer { background: %2$s !important; }"
	"ytd-masthead { border-bottom: 1px solid %8$s; }"
	"ytd-searchbox #container, .ytSearchboxComponentInputBox, input#search { background: %1$s !important; color: %6$s !important; border-color: %4$s !important; border-radius: 0 !important; }"
	"#search-icon-legacy, .ytSearchboxComponentSearchButton { background: %3$s !important; border-color: %4$s !important; border-radius: 0 !important; }"
	"yt-chip-cloud-chip-renderer, .ytChipShapeChip { background: %3$s !important; color: %6$s !important; border-radius: 0 !important; }"
	"yt-chip-cloud-chip-renderer[selected], .ytChipShapeActive { background: %8$s !important; color: %9$s !important; }"
	"ytd-guide-entry-renderer[active] tp-yt-paper-item, ytd-guide-entry-renderer tp-yt-paper-item:hover { background: %5$s !important; }"
	"ytd-guide-entry-renderer[active] yt-formatted-string, ytd-guide-entry-renderer[active] .title { color: %8$s !important; }"
	"ytd-thumbnail, ytd-thumbnail img, #thumbnail, yt-image img, .yt-core-image, .ytThumbnailViewModelImage,"
	"  #player-container-outer, .html5-video-player, ytd-player #container { border-radius: 0 !important; }"
	"yt-button-shape button, .yt-spec-button-shape-next { border-radius: 0 !important; }"
	"ytd-topbar-logo-renderer #logo-icon, ytd-topbar-logo-renderer yt-icon { display: none !important; }"
	"ytd-topbar-logo-renderer #logo::after { content: \"SYN-TUBE\"; color: %8$s; font: bold 18px \"Terminus\", monospace; padding-left: 4px; }"
	"#country-code { display: none !important; }"
	".ytp-play-progress, .ytp-swatch-background-color, .ytp-volume-slider-handle, .ytp-volume-slider-handle::before,"
	"  .ytp-settings-button.ytp-hd-quality-badge::after { background: %8$s !important; }"
	".ytp-scrubber-button, .ytp-swatch-color { color: %8$s !important; background: %8$s !important; }"
	".ytp-popup, .ytp-settings-menu, .ytp-panel { background: %2$s !important; border-radius: 0 !important; }"
	"::selection { background: %8$s; color: %9$s; }"
	"* { scrollbar-color: %8$s %2$s; }"
	/* no adverts or sponsored items */
	"ytd-ad-slot-renderer, ytd-in-feed-ad-layout-renderer, ytd-promoted-sparkles-web-renderer, ytd-promoted-video-renderer,"
	"  ytd-compact-promoted-video-renderer, ytd-display-ad-renderer, ytd-action-companion-ad-renderer, ytd-banner-promo-renderer,"
	"  ytd-statement-banner-renderer, ytd-player-legacy-desktop-watch-ads-renderer, ytd-search-pyv-renderer, ytd-merch-shelf-renderer,"
	"  ytd-brand-video-singleton-renderer, ytd-brand-video-shelf-renderer, ytd-primetime-promo-renderer, #masthead-ad, #player-ads,"
	"  ytd-rich-item-renderer:has(ytd-ad-slot-renderer), ytd-rich-item-renderer:has(feed-ad-metadata-view-model),"
	"  ytd-rich-item-renderer:has(badge-shape[aria-label=\"Sponsored\"]), ytd-rich-section-renderer:has(ytd-statement-banner-renderer),"
	"  .ytp-ad-module, .ytp-ad-overlay-container, .ytp-ad-image-overlay, .video-ads { display: none !important; }"
	/* no Shorts */
	"ytd-reel-shelf-renderer, ytd-rich-shelf-renderer[is-shorts], ytd-rich-section-renderer:has(ytd-rich-shelf-renderer[is-shorts]),"
	"  grid-shelf-view-model:has(ytm-shorts-lockup-view-model), ytm-shorts-lockup-view-model, ytm-shorts-lockup-view-model-v2,"
	"  ytd-video-renderer:has(a[href^=\"/shorts/\"]), ytd-rich-item-renderer:has(a[href^=\"/shorts/\"]),"
	"  ytd-guide-entry-renderer:has(a[title=\"Shorts\"]), ytd-mini-guide-entry-renderer[aria-label=\"Shorts\"],"
	"  yt-tab-shape[tab-title=\"Shorts\"] { display: none !important; }"
	/* no algorithm: recommendations beside and after a video, trending/explore */
	"#related, ytd-watch-next-secondary-results-renderer, .ytp-endscreen-content, .ytp-ce-element, .videowall-endscreen,"
	"  .ytp-autonav-toggle-button-container, ytd-guide-section-renderer:has(a[href=\"/feed/trending\"]),"
	"  ytd-guide-section-renderer:has(a[href^=\"/feed/explore\"]) { display: none !important; }"
	/* the SYN button in YouTube's top bar */
	"#syn-tube-button { font: bold 15px \"Terminus\", monospace; color: %9$s; background: %8$s; border: 0; border-radius: 0;"
	"  padding: 6px 14px; margin: 0 12px; cursor: pointer; letter-spacing: 1px; }"
	"#syn-tube-button:hover { filter: brightness(1.15); }";

/* YouTube in its own dark mode, which the overlay's palette sits on: the dark
   attribute now, and YouTube's own preference cookie (PREF f6=400) so later
   pages are served dark. */
static const char YT_DARK_JS[] =
	"(function(){var d=document.documentElement;"
	"function dark(){if(!d.hasAttribute('dark'))d.setAttribute('dark','');}"
	"dark();new MutationObserver(dark).observe(d,{attributes:true,attributeFilter:['dark']});"
	"var m=document.cookie.match(/(?:^|; )PREF=([^;]*)/),v=m?m[1]:'';"
	"if(!/(^|&)f6=4/.test(v)){v=v.replace(/(^|&)f6=[^&]*/,'');v=(v?v+'&':'')+'f6=400';"
	"document.cookie='PREF='+v+'; domain=.youtube.com; path=/; max-age=31536000; secure';}"
	"})();";

/* text that reads on the accent: the themes' own "selected text" can be the
   accent itself (HALO, FLATLINE, MATRIX), which vanishes on an accent fill */
static const char *on_accent(void)
{
	unsigned r = 0, g = 0, b = 0;
	sscanf(G.accent + 1, "%2x%2x%2x", &r, &g, &b);
	return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255 > 0.55 ? "#000000" : "#ffffff";
}

/* YouTube without the algorithm or adverts, and the SYN button: the home
   page's recommendations become the subscriptions feed; an advert in the
   player is skipped; the button opens the same page in SYN-TUBE through
   the syn-tube: address (see on_decide). */
static const char YT_CLEAN_JS[] =
	"(function(){"
	"function home(){if(location.pathname==='/'&&!location.search)location.replace('/feed/subscriptions');}"
	"home();window.addEventListener('yt-navigate-start',function(e){try{var u=e.detail.url||'';if(u==='/'||u==='/?')"
	"{e.stopImmediatePropagation();location.replace('/feed/subscriptions');}}catch(_){}} ,true);"
	"window.addEventListener('yt-navigate-finish',home);"
	"setInterval(function(){var p=document.querySelector('.html5-video-player');"
	"if(p&&(p.classList.contains('ad-showing')||p.classList.contains('ad-interrupting'))){var v=p.querySelector('video');"
	"if(v){v.muted=true;if(isFinite(v.duration))v.currentTime=v.duration;}"
	"var b=document.querySelector('.ytp-skip-ad-button,.ytp-ad-skip-button,.ytp-ad-skip-button-modern');if(b)b.click();}},300);"
	"function route(){var u=new URL(location.href),p=u.pathname,q=u.searchParams,m;"
	"if(p==='/watch'&&q.get('v'))return'watch/'+q.get('v');"
	"if(p==='/results'&&q.get('search_query'))return'search/'+encodeURIComponent(q.get('search_query'));"
	"if(p==='/playlist'&&q.get('list'))return'playlist/'+q.get('list');"
	"if((m=p.match(/^\\/channel\\/(UC[\\w-]{22})/)))return'channel/'+m[1];"
	"if(/^\\/(@|c\\/|user\\/)/.test(p)){var id=(document.querySelector('meta[itemprop=identifier]')||{}).content;"
	"try{id=id||ytInitialData.metadata.channelMetadataRenderer.externalId;}catch(_){}if(id)return'channel/'+id;}"
	"return'feed';}"
	"function button(){if(document.getElementById('syn-tube-button'))return;var end=document.querySelector('ytd-masthead #end');"
	"if(!end)return;var b=document.createElement('button');b.id='syn-tube-button';b.textContent='SYN';"
	"b.title='Open this in SYN-TUBE';b.onclick=function(){location.href='syn-tube:'+route();};end.insertBefore(b,end.firstChild);}"
	"setInterval(button,800);"
	"})();";

static void overlay_apply(WebKitUserContentManager *ucm)
{
	webkit_user_content_manager_remove_all_style_sheets(ucm);
	char *css = g_strdup_printf(YT_CSS, G.bg, G.bg_alt, G.panel, G.border, G.hover, G.text,
		G.border, G.accent, on_accent());
	WebKitUserStyleSheet *sheet = webkit_user_style_sheet_new(css, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
		WEBKIT_USER_STYLE_LEVEL_USER, YT_HOSTS, NULL);
	webkit_user_content_manager_add_style_sheet(ucm, sheet);
	webkit_user_style_sheet_unref(sheet);
	g_free(css);
}

static void overlay_refresh_all(void)
{
	if (!G.book) return;
	int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(G.book));
	for (int i = 0; i < n; i++) {
		GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(G.book), i);
		if (!WEBKIT_IS_WEB_VIEW(page)) continue;
		overlay_apply(webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(page)));
		/* the new-tab page is HTML filled in with the palette: draw it again */
		const char *uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(page));
		Tab *t = g_object_get_data(G_OBJECT(page), "tab");
		if (t && (!uri || !*uri || g_str_equal(uri, "about:blank"))) load_home(t);
	}
}

static gboolean poll_theme(gpointer unused)
{
	char *now = live_theme_name();
	if (g_strcmp0(now, G.theme_name)) apply_theme(now);
	g_free(now);
	return G_SOURCE_CONTINUE;
}

/* ---- a message at the bottom of the page ---- */
static gboolean status_clear(gpointer unused)
{
	G.status_timer = 0;
	gtk_widget_hide(G.status);
	return G_SOURCE_REMOVE;
}

static void status_set(const char *text, gboolean keep)
{
	if (G.status_timer) { g_source_remove(G.status_timer); G.status_timer = 0; }
	if (!text || !*text) { gtk_widget_hide(G.status); return; }
	gtk_label_set_text(GTK_LABEL(G.status), text);
	gtk_widget_show(G.status);
	if (!keep) G.status_timer = g_timeout_add(STATUS_MS, status_clear, NULL);
}

/* ---- what you typed, as an address ---- */
/* An address copied from the end of a sentence carries its full stop: "host.com." (or "host.com./"). The site's
 * certificate names "host.com", so the trailing dot failed with "Unacceptable TLS certificate" (14 Nov 2026). Takes
 * ownership of uri and returns it with the host's trailing dot(s) removed. */
static char *no_end_dot(char *uri)
{
	if (!g_str_has_prefix(uri, "http://") && !g_str_has_prefix(uri, "https://")) return uri;
	GUri *u = g_uri_parse(uri, G_URI_FLAGS_ENCODED, NULL);
	if (!u) return uri;
	const char *host = g_uri_get_host(u);
	size_t n = host ? strlen(host) : 0;
	if (n > 1 && host[n - 1] == '.') {
		while (n > 1 && host[n - 1] == '.') n--;
		char *h = g_strndup(host, n);
		GUri *v = g_uri_build(G_URI_FLAGS_ENCODED, g_uri_get_scheme(u), g_uri_get_userinfo(u), h, g_uri_get_port(u),
		                      g_uri_get_path(u), g_uri_get_query(u), g_uri_get_fragment(u));
		g_free(uri);
		uri = g_uri_to_string(v);
		g_uri_unref(v);
		g_free(h);
	}
	g_uri_unref(u);
	return uri;
}

static char *to_uri(const char *typed)
{
	char *s = g_strstrip(g_strdup(typed)), *out;
	static GRegex *host;
	if (!host) host = g_regex_new("^(localhost|[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)+\\.?|\\[[0-9a-fA-F:]+\\])(:[0-9]+)?([/?#].*)?$", 0, 0, NULL);
	if (strstr(s, "://") || g_str_has_prefix(s, "about:") || g_str_has_prefix(s, "file:")) {
		out = g_strdup(s);
	} else if (!strchr(s, ' ') && g_regex_match(host, s, 0, NULL)) {
		gboolean local = g_str_has_prefix(s, "localhost") || g_str_has_prefix(s, "127.") || g_str_has_prefix(s, "[::1]");
		out = g_strconcat(local ? "http://" : "https://", s, NULL);
	} else {
		char *q = g_uri_escape_string(s, NULL, FALSE);
		out = g_strconcat(SEARCH, q, NULL);
		g_free(q);
	}
	g_free(s);
	return no_end_dot(out);
}

/* ---- tabs ---- */
static Tab *tab_of(GtkWidget *page)
{
	return page ? g_object_get_data(G_OBJECT(page), "tab") : NULL;
}

static Tab *current(void)
{
	return tab_of(gtk_notebook_get_nth_page(GTK_NOTEBOOK(G.book), gtk_notebook_get_current_page(GTK_NOTEBOOK(G.book))));
}

static gboolean is_current(Tab *t)
{
	return t == current();
}

static const char *shown_uri(Tab *t)
{
	const char *u = webkit_web_view_get_uri(t->web);
	return !u || g_str_equal(u, "about:blank") ? "" : u;
}

static void sync_chrome(Tab *t)
{
	if (!t) return;
	if (!gtk_widget_has_focus(G.entry)) gtk_entry_set_text(GTK_ENTRY(G.entry), shown_uri(t));
	gtk_widget_set_sensitive(G.back, webkit_web_view_can_go_back(t->web));
	gtk_widget_set_sensitive(G.ahead, webkit_web_view_can_go_forward(t->web));
	gtk_button_set_label(GTK_BUTTON(G.reload), webkit_web_view_is_loading(t->web) ? "STOP" : "RELOAD");
	double p = webkit_web_view_is_loading(t->web) ? webkit_web_view_get_estimated_load_progress(t->web) : 0;
	gtk_entry_set_progress_fraction(GTK_ENTRY(G.entry), p >= 1 ? 0 : p);
	const char *title = webkit_web_view_get_title(t->web);
	char *w = title && *title ? g_strconcat(title, " - ", APP_NAME, NULL) : g_strdup(APP_NAME);
	gtk_window_set_title(GTK_WINDOW(G.win), w);
	g_free(w);
}

static void close_page(GtkWidget *page)
{
	GtkNotebook *b = GTK_NOTEBOOK(G.book);
	int n = gtk_notebook_page_num(b, page);
	if (n < 0) return;
	if (gtk_notebook_get_n_pages(b) == 1) { gtk_window_close(GTK_WINDOW(G.win)); return; }
	gtk_notebook_remove_page(b, n);
}

static void on_close_clicked(GtkButton *b, gpointer page) { close_page(page); }

static gboolean on_tab_click(GtkWidget *w, GdkEventButton *e, gpointer page)
{
	if (e->button != GDK_BUTTON_MIDDLE) return FALSE;
	close_page(page);
	return TRUE;
}

static void on_title(WebKitWebView *web, GParamSpec *p, Tab *t)
{
	const char *title = webkit_web_view_get_title(web);
	gtk_label_set_text(GTK_LABEL(t->title), title && *title ? title : "New tab");
	gtk_widget_set_tooltip_text(t->tab_label, title);
	if (is_current(t)) sync_chrome(t);
}

static void on_changed(WebKitWebView *web, GParamSpec *p, Tab *t)
{
	if (is_current(t)) sync_chrome(t);
}

static void on_history_moved(WebKitBackForwardList *l, WebKitBackForwardListItem *added, gpointer removed, Tab *t)
{
	if (is_current(t)) sync_chrome(t);
}

/* ---- history: what the address bar suggests from ---- */
static char *history_path(void) { return g_build_filename(G.data_dir, "history", NULL); }

static void history_add(const char *uri, const char *title)
{
	if (!uri || !(g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://"))) return;
	GtkTreeModel *m = GTK_TREE_MODEL(G.history);
	GtkTreeIter it;
	gboolean more = gtk_tree_model_get_iter_first(m, &it);
	while (more) {
		char *u;
		gtk_tree_model_get(m, &it, H_URI, &u, -1);
		gboolean same = g_str_equal(u, uri);
		g_free(u);
		if (same) { gtk_list_store_remove(G.history, &it); break; }
		more = gtk_tree_model_iter_next(m, &it);
	}
	gtk_list_store_insert_with_values(G.history, NULL, 0, H_URI, uri, H_TITLE, title ? title : "", -1);
	int n = gtk_tree_model_iter_n_children(m, NULL);
	if (n > HISTORY_MAX && gtk_tree_model_iter_nth_child(m, &it, NULL, n - 1)) gtk_list_store_remove(G.history, &it);
}

static void history_load(void)
{
	char *path = history_path(), *data = NULL;
	if (g_file_get_contents(path, &data, NULL, NULL)) {
		char **lines = g_strsplit(data, "\n", HISTORY_MAX + 1);
		for (char **l = lines; *l && **l; l++) {
			char *tab = strchr(*l, '\t');
			if (tab) *tab = 0;
			gtk_list_store_insert_with_values(G.history, NULL, -1, H_URI, *l, H_TITLE, tab ? tab + 1 : "", -1);
		}
		g_strfreev(lines);
		g_free(data);
	}
	g_free(path);
}

static void clean_line(GString *out, const char *s)
{
	for (; s && *s; s++) g_string_append_c(out, *s == '\t' || *s == '\n' || *s == '\r' ? ' ' : *s);
}

static void history_save(void)
{
	GString *out = g_string_new(NULL);
	GtkTreeModel *m = GTK_TREE_MODEL(G.history);
	GtkTreeIter it;
	for (gboolean more = gtk_tree_model_get_iter_first(m, &it); more; more = gtk_tree_model_iter_next(m, &it)) {
		char *u, *t;
		gtk_tree_model_get(m, &it, H_URI, &u, H_TITLE, &t, -1);
		clean_line(out, u);
		g_string_append_c(out, '\t');
		clean_line(out, t);
		g_string_append_c(out, '\n');
		g_free(u);
		g_free(t);
	}
	char *path = history_path();
	g_file_set_contents(path, out->str, out->len, NULL);
	g_free(path);
	g_string_free(out, TRUE);
}

static gboolean complete_match(GtkEntryCompletion *c, const char *key, GtkTreeIter *it, gpointer unused)
{
	char *u, *t;
	gtk_tree_model_get(GTK_TREE_MODEL(G.history), it, H_URI, &u, H_TITLE, &t, -1);
	char *ul = g_utf8_strdown(u, -1), *tl = g_utf8_strdown(t, -1), *k = g_utf8_strdown(key, -1);
	gboolean hit = strstr(ul, k) || strstr(tl, k);
	g_free(u); g_free(t); g_free(ul); g_free(tl); g_free(k);
	return hit;
}

static void go(Tab *t, const char *typed)
{
	char *uri = to_uri(typed);
	webkit_web_view_load_uri(t->web, uri);
	g_free(uri);
	gtk_widget_grab_focus(GTK_WIDGET(t->web));
}

static gboolean on_complete_pick(GtkEntryCompletion *c, GtkTreeModel *m, GtkTreeIter *it, gpointer unused)
{
	char *u;
	gtk_tree_model_get(m, it, H_URI, &u, -1);
	if (current()) go(current(), u);
	g_free(u);
	return TRUE;
}

/* ---- what happens as pages load ---- */
static void on_load(WebKitWebView *web, WebKitLoadEvent e, Tab *t)
{
	if (e == WEBKIT_LOAD_FINISHED) history_add(webkit_web_view_get_uri(web), webkit_web_view_get_title(web));
	if (is_current(t)) sync_chrome(t);
}

static void on_hover(WebKitWebView *web, WebKitHitTestResult *hit, guint mods, Tab *t)
{
	if (!is_current(t) || G.status_timer) return;
	status_set(webkit_hit_test_result_context_is_link(hit) ? webkit_hit_test_result_get_link_uri(hit) : NULL, TRUE);
}

static void on_ready_to_show(WebKitWebView *web, gpointer unused)
{
	gtk_widget_show(GTK_WIDGET(web));
}

/* a page's new window becomes a tab, and only when you clicked for it: popups nobody asked for go nowhere */
static GtkWidget *on_create(WebKitWebView *web, WebKitNavigationAction *a, gpointer unused)
{
	if (!webkit_navigation_action_is_user_gesture(a)) {
		status_set("Blocked a popup this page tried to open by itself.", FALSE);
		return NULL;
	}
	Tab *t = tab_new(NULL, web, TRUE);
	g_signal_connect(t->web, "ready-to-show", G_CALLBACK(on_ready_to_show), NULL);
	return GTK_WIDGET(t->web);
}

static void on_page_close(WebKitWebView *web, gpointer unused) { close_page(GTK_WIDGET(web)); }

static gboolean on_crash(WebKitWebView *web, WebKitWebProcessTerminationReason why, Tab *t)
{
	status_set("That page crashed. Press RELOAD to try it again.", FALSE);
	return FALSE;
}

/* ---- syn-tube:ROUTE, from the SYN button on YouTube ----
   `syn-tube --no-open` starts SYN-TUBE's server if it isn't running and
   prints its address; this tab then opens ADDRESS#/ROUTE (watch/ID,
   channel/ID, search/Q, playlist/ID or feed). */
typedef struct { WebKitWebView *web; char *route; } SynTubeJob;

static void syn_tube_done(GObject *proc, GAsyncResult *res, gpointer data)
{
	SynTubeJob *j = data;
	char *out = NULL;
	GError *err = NULL;
	if (g_subprocess_communicate_utf8_finish(G_SUBPROCESS(proc), res, &out, NULL, &err)
			&& g_subprocess_get_successful(G_SUBPROCESS(proc)) && out && g_str_has_prefix(out, "http")) {
		g_strstrip(out);
		char *uri = g_strconcat(out, "#/", j->route, NULL);
		webkit_web_view_load_uri(j->web, uri);
		g_free(uri);
	} else status_set("SYN-TUBE didn't start (run syn-tube in a terminal to see why).", FALSE);
	if (err) g_error_free(err);
	g_free(out);
	g_object_unref(j->web);
	g_free(j->route);
	g_free(j);
}

static void syn_tube_open(WebKitWebView *web, const char *route)
{
	char *prog = g_find_program_in_path("syn-tube");
	if (!prog) prog = g_build_filename(g_get_home_dir(), ".local", "bin", "syn-tube", NULL);
	GError *err = NULL;
	GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &err,
		prog, "--no-open", NULL);
	g_free(prog);
	if (!p) {
		status_set("SYN-TUBE isn't installed here.", FALSE);
		g_error_free(err);
		return;
	}
	status_set("Opening SYN-TUBE...", TRUE);
	SynTubeJob *j = g_new0(SynTubeJob, 1);
	j->web = g_object_ref(web);
	j->route = g_strdup(*route ? route : "feed");
	g_subprocess_communicate_utf8_async(p, NULL, NULL, syn_tube_done, j);
	g_object_unref(p);
}

static gboolean on_decide(WebKitWebView *web, WebKitPolicyDecision *d, WebKitPolicyDecisionType type, gpointer unused)
{
	if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
		WebKitResponsePolicyDecision *r = WEBKIT_RESPONSE_POLICY_DECISION(d);
		if (!webkit_response_policy_decision_is_mime_type_supported(r)) {
			webkit_policy_decision_download(d);
			return TRUE;
		}
		return FALSE;
	}
	if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) return FALSE;
	WebKitNavigationAction *a = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(d));
	const char *uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(a));
	char *scheme = g_uri_parse_scheme(uri);
	gboolean web_scheme = !scheme || !g_ascii_strcasecmp(scheme, "http") || !g_ascii_strcasecmp(scheme, "https") || !g_ascii_strcasecmp(scheme, "about")
		|| !g_ascii_strcasecmp(scheme, "file") || !g_ascii_strcasecmp(scheme, "data") || !g_ascii_strcasecmp(scheme, "blob");
	g_free(scheme);
	if (web_scheme) return FALSE;
	if (g_str_has_prefix(uri, "syn-tube:")) {
		syn_tube_open(web, uri + strlen("syn-tube:"));
		webkit_policy_decision_ignore(d);
		return TRUE;
	}
	/* mailto: and friends go to the desktop's own app, and only for a real click */
	if (webkit_navigation_action_is_user_gesture(a)) gtk_show_uri_on_window(GTK_WINDOW(G.win), uri, GDK_CURRENT_TIME, NULL);
	webkit_policy_decision_ignore(d);
	return TRUE;
}

static void set_chrome_visible(gboolean on)
{
	gtk_widget_set_visible(G.bar, on);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(G.book), on);
}

static gboolean on_enter_full(WebKitWebView *w, gpointer u) { set_chrome_visible(FALSE); return FALSE; }
static gboolean on_leave_full(WebKitWebView *w, gpointer u) { set_chrome_visible(TRUE); return FALSE; }

static const char HOME_PAGE[] =
	"<!doctype html><meta charset=utf-8><title>New tab</title><style>"
	"html,body{height:100%%;margin:0}"
	"body{display:flex;align-items:center;justify-content:center;background:%s;color:%s;font:16px/1.5 Terminus,monospace}"
	"h1{color:%s;font-size:32px;margin:0 0 8px}p{margin:0 0 20px}table{border-collapse:collapse}"
	"td{padding:3px 24px 3px 0}td:first-child{color:%s}"
	"</style><div><h1>SYN-NETJUMPER</h1><p>Type an address or a search in the bar at the top and press Enter.</p><table>"
	"<tr><td>Ctrl+L</td><td>Go to the address bar</td></tr>"
	"<tr><td>Ctrl+T / Ctrl+W</td><td>Open / close a tab</td></tr>"
	"<tr><td>Ctrl+Tab</td><td>Next tab</td></tr>"
	"<tr><td>Ctrl+F</td><td>Find on this page</td></tr>"
	"<tr><td>Ctrl + / - / 0</td><td>Zoom in / out / reset</td></tr>"
	"<tr><td>Alt+Left / Right</td><td>Back / ahead</td></tr>"
	"<tr><td>F5, F11, F12</td><td>Reload, full screen, page inspector</td></tr>"
	"</table></div>";

static void load_home(Tab *t)
{
	char *html = g_strdup_printf(HOME_PAGE, G.bg, G.text, G.accent, G.accent);
	webkit_web_view_load_html(t->web, html, "about:blank");
	g_free(html);
}

static Tab *tab_new(const char *uri, WebKitWebView *related, gboolean focus)
{
	Tab *t = g_new0(Tab, 1);
	WebKitUserContentManager *ucm = webkit_user_content_manager_new();
	webkit_user_content_manager_add_script(ucm, webkit_user_script_new(
		AUTOFILL_JS, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, NULL, NULL));
	webkit_user_content_manager_register_script_message_handler(ucm, "synpw");
	overlay_apply(ucm);
	webkit_user_content_manager_add_script(ucm, webkit_user_script_new(YT_DARK_JS,
		WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, YT_HOSTS, NULL));
	webkit_user_content_manager_add_script(ucm, webkit_user_script_new(YT_CLEAN_JS,
		WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, YT_HOSTS, NULL));
	g_signal_connect(ucm, "script-message-received::synpw", G_CALLBACK(on_script_message), t);
	t->web = WEBKIT_WEB_VIEW(related
		? g_object_new(WEBKIT_TYPE_WEB_VIEW, "related-view", related, "user-content-manager", ucm, NULL)
		: g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", G.context, "user-content-manager", ucm, NULL));
	g_object_unref(ucm);
	webkit_web_view_set_settings(t->web, G.settings);
	GtkWidget *page = GTK_WIDGET(t->web);
	g_object_set_data_full(G_OBJECT(page), "tab", t, g_free);

	t->title = gtk_label_new("New tab");
	gtk_label_set_ellipsize(GTK_LABEL(t->title), PANGO_ELLIPSIZE_END);
	gtk_label_set_width_chars(GTK_LABEL(t->title), 18);
	gtk_label_set_max_width_chars(GTK_LABEL(t->title), 18);
	gtk_label_set_xalign(GTK_LABEL(t->title), 0);
	GtkWidget *x = gtk_button_new_with_label("x");
	gtk_style_context_add_class(gtk_widget_get_style_context(x), "tabclose");
	gtk_button_set_relief(GTK_BUTTON(x), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(x, "Close this tab");
	g_signal_connect(x, "clicked", G_CALLBACK(on_close_clicked), page);
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_pack_start(GTK_BOX(row), t->title, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(row), x, FALSE, FALSE, 0);
	t->tab_label = gtk_event_box_new();
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(t->tab_label), FALSE);
	gtk_container_add(GTK_CONTAINER(t->tab_label), row);
	g_signal_connect(t->tab_label, "button-press-event", G_CALLBACK(on_tab_click), page);
	gtk_widget_show_all(t->tab_label);

	g_signal_connect(t->web, "notify::title", G_CALLBACK(on_title), t);
	g_signal_connect(t->web, "notify::uri", G_CALLBACK(on_changed), t);
	g_signal_connect(t->web, "notify::is-loading", G_CALLBACK(on_changed), t);
	g_signal_connect(t->web, "notify::estimated-load-progress", G_CALLBACK(on_changed), t);
	g_signal_connect(t->web, "load-changed", G_CALLBACK(on_load), t);
	g_signal_connect(t->web, "mouse-target-changed", G_CALLBACK(on_hover), t);
	g_signal_connect(t->web, "create", G_CALLBACK(on_create), NULL);
	g_signal_connect(t->web, "close", G_CALLBACK(on_page_close), NULL);
	g_signal_connect(t->web, "decide-policy", G_CALLBACK(on_decide), NULL);
	g_signal_connect(t->web, "web-process-terminated", G_CALLBACK(on_crash), t);
	g_signal_connect(t->web, "enter-fullscreen", G_CALLBACK(on_enter_full), NULL);
	g_signal_connect(t->web, "leave-fullscreen", G_CALLBACK(on_leave_full), NULL);
	g_signal_connect(webkit_web_view_get_back_forward_list(t->web), "changed", G_CALLBACK(on_history_moved), t);

	gtk_widget_show(page);
	GtkNotebook *b = GTK_NOTEBOOK(G.book);
	int n = gtk_notebook_append_page(b, page, t->tab_label);
	gtk_notebook_set_tab_reorderable(b, page, TRUE);
	gtk_container_child_set(GTK_CONTAINER(b), page, "tab-expand", FALSE, NULL);
	if (focus) gtk_notebook_set_current_page(b, n);
	if (!related) {
		if (uri) {   /* a restored or opened address gets the same trailing-dot fix as a typed one */
			char *u = no_end_dot(g_strdup(uri));
			webkit_web_view_load_uri(t->web, u);
			g_free(u);
		} else load_home(t);
	}
	return t;
}

static void new_blank_tab(void)
{
	tab_new(NULL, NULL, TRUE);
	gtk_widget_grab_focus(G.entry);
}

/* ---- downloads: straight to ~/Downloads, never over a file already there ---- */
static char *download_name(WebKitDownload *d) { return g_object_get_data(G_OBJECT(d), "name"); }

static gboolean on_decide_destination(WebKitDownload *d, const char *suggested, gpointer unused)
{
	const char *dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
	char *base = g_path_get_basename(suggested && *suggested ? suggested : "download");
	char *folder = g_strdup(dir ? dir : g_get_home_dir());
	g_mkdir_with_parents(folder, 0755);
	char *path = g_build_filename(folder, base, NULL);
	for (int i = 1; g_file_test(path, G_FILE_TEST_EXISTS); i++) {
		g_free(path);
		char *dot = strrchr(base, '.');
		char *name = dot && dot != base
			? g_strdup_printf("%.*s (%d)%s", (int)(dot - base), base, i, dot)
			: g_strdup_printf("%s (%d)", base, i);
		path = g_build_filename(folder, name, NULL);
		g_free(name);
	}
	char *uri = g_filename_to_uri(path, NULL, NULL);
	webkit_download_set_destination(d, uri);
	g_object_set_data_full(G_OBJECT(d), "name", g_path_get_basename(path), g_free);
	g_object_set_data_full(G_OBJECT(d), "path", path, g_free);
	g_free(uri);
	g_free(base);
	g_free(folder);
	return TRUE;
}

static void on_download_progress(WebKitDownload *d, GParamSpec *p, gpointer unused)
{
	if (!download_name(d)) return;
	char *s = g_strdup_printf("Downloading %s: %d%%", download_name(d), (int)(webkit_download_get_estimated_progress(d) * 100));
	status_set(s, TRUE);
	g_free(s);
}

static void on_download_failed(WebKitDownload *d, GError *e, gpointer unused)
{
	g_object_set_data(G_OBJECT(d), "failed", GINT_TO_POINTER(1));
	char *s = g_strdup_printf("Download failed: %s", e->message);
	status_set(s, FALSE);
	g_free(s);
}

static void on_download_done(WebKitDownload *d, gpointer unused)
{
	if (g_object_get_data(G_OBJECT(d), "failed") || !download_name(d)) return;
	char *s = g_strdup_printf("Saved to %s", (char *)g_object_get_data(G_OBJECT(d), "path"));
	status_set(s, FALSE);
	g_free(s);
}

static void on_download(WebKitWebContext *c, WebKitDownload *d, gpointer unused)
{
	g_signal_connect(d, "decide-destination", G_CALLBACK(on_decide_destination), NULL);
	g_signal_connect(d, "notify::estimated-progress", G_CALLBACK(on_download_progress), NULL);
	g_signal_connect(d, "failed", G_CALLBACK(on_download_failed), NULL);
	g_signal_connect(d, "finished", G_CALLBACK(on_download_done), NULL);
}

/* ---- find on the page ---- */
static void find_run(gboolean backwards)
{
	Tab *t = current();
	const char *text = gtk_entry_get_text(GTK_ENTRY(G.findentry));
	if (!t) return;
	WebKitFindController *f = webkit_web_view_get_find_controller(t->web);
	if (!*text) { webkit_find_controller_search_finish(f); gtk_label_set_text(GTK_LABEL(G.findcount), ""); return; }
	guint32 o = WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND;
	if (backwards) o |= WEBKIT_FIND_OPTIONS_BACKWARDS;
	webkit_find_controller_search(f, text, o, G_MAXUINT);
}

static void on_found(WebKitFindController *f, guint n, gpointer unused)
{
	char *s = g_strdup_printf(n == 1 ? "1 match" : "%u matches", n);
	gtk_label_set_text(GTK_LABEL(G.findcount), s);
	g_free(s);
}

static void on_not_found(WebKitFindController *f, gpointer unused)
{
	gtk_label_set_text(GTK_LABEL(G.findcount), "Not found");
}

static void find_open(void)
{
	Tab *t = current();
	if (t) {
		WebKitFindController *f = webkit_web_view_get_find_controller(t->web);
		if (!g_object_get_data(G_OBJECT(f), "wired")) {
			g_object_set_data(G_OBJECT(f), "wired", GINT_TO_POINTER(1));
			g_signal_connect(f, "found-text", G_CALLBACK(on_found), NULL);
			g_signal_connect(f, "failed-to-find-text", G_CALLBACK(on_not_found), NULL);
		}
	}
	gtk_widget_show(G.findbar);
	gtk_widget_grab_focus(G.findentry);
}

static void find_close(void)
{
	Tab *t = current();
	if (t) webkit_find_controller_search_finish(webkit_web_view_get_find_controller(t->web));
	gtk_widget_hide(G.findbar);
	if (t) gtk_widget_grab_focus(GTK_WIDGET(t->web));
}

static void on_find_changed(GtkEditable *e, gpointer unused) { find_run(FALSE); }
static void on_find_next(GtkWidget *w, gpointer unused) { find_run(FALSE); }
static void on_find_prev(GtkWidget *w, gpointer unused) { find_run(TRUE); }
static void on_find_close(GtkWidget *w, gpointer unused) { find_close(); }

/* ---- the bar's buttons ---- */
static void on_back(GtkWidget *w, gpointer u) { if (current()) webkit_web_view_go_back(current()->web); }
static void on_ahead(GtkWidget *w, gpointer u) { if (current()) webkit_web_view_go_forward(current()->web); }
static void on_new_tab(GtkWidget *w, gpointer u) { new_blank_tab(); }

static void on_reload(GtkWidget *w, gpointer u)
{
	Tab *t = current();
	if (!t) return;
	if (webkit_web_view_is_loading(t->web)) webkit_web_view_stop_loading(t->web);
	else webkit_web_view_reload(t->web);
}

static void on_entry_activate(GtkEntry *e, gpointer u)
{
	if (current() && *gtk_entry_get_text(e)) go(current(), gtk_entry_get_text(e));
}

static gboolean on_entry_focus(GtkWidget *e, GdkEvent *ev, gpointer u)
{
	gtk_editable_select_region(GTK_EDITABLE(e), 0, -1);
	return FALSE;
}

static void on_switch(GtkNotebook *b, GtkWidget *page, guint n, gpointer u)
{
	gtk_entry_set_text(GTK_ENTRY(G.entry), "");
	Tab *t = tab_of(page);
	if (!t) return;
	gtk_entry_set_text(GTK_ENTRY(G.entry), shown_uri(t));
	gtk_widget_set_sensitive(G.back, webkit_web_view_can_go_back(t->web));
	gtk_widget_set_sensitive(G.ahead, webkit_web_view_can_go_forward(t->web));
	gtk_button_set_label(GTK_BUTTON(G.reload), webkit_web_view_is_loading(t->web) ? "STOP" : "RELOAD");
	const char *title = webkit_web_view_get_title(t->web);
	char *s = title && *title ? g_strconcat(title, " - ", APP_NAME, NULL) : g_strdup(APP_NAME);
	gtk_window_set_title(GTK_WINDOW(G.win), s);
	g_free(s);
}

static void zoom(double by)
{
	Tab *t = current();
	if (!t) return;
	double z = by == 0 ? 1 : CLAMP(webkit_web_view_get_zoom_level(t->web) * by, 0.3, 4);
	webkit_web_view_set_zoom_level(t->web, z);
	char *s = g_strdup_printf("Zoom %d%%", (int)(z * 100 + 0.5));
	status_set(s, FALSE);
	g_free(s);
}

static void toggle_full(void)
{
	GdkWindow *w = gtk_widget_get_window(G.win);
	if (gdk_window_get_state(w) & GDK_WINDOW_STATE_FULLSCREEN) { gtk_window_unfullscreen(GTK_WINDOW(G.win)); set_chrome_visible(TRUE); }
	else { gtk_window_fullscreen(GTK_WINDOW(G.win)); set_chrome_visible(FALSE); }
}

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer u)
{
	GtkNotebook *b = GTK_NOTEBOOK(G.book);
	Tab *t = current();
	guint mods = e->state & gtk_accelerator_get_default_mod_mask();
	guint k = gdk_keyval_to_lower(e->keyval);
	if (mods == GDK_CONTROL_MASK || mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
		gboolean shift = mods & GDK_SHIFT_MASK;
		switch (k) {
		case GDK_KEY_l: gtk_widget_grab_focus(G.entry); return TRUE;
		case GDK_KEY_t: new_blank_tab(); return TRUE;
		case GDK_KEY_w: if (t) close_page(GTK_WIDGET(t->web)); return TRUE;
		case GDK_KEY_r: on_reload(NULL, NULL); return TRUE;
		case GDK_KEY_f: find_open(); return TRUE;
		case GDK_KEY_equal: case GDK_KEY_plus: case GDK_KEY_KP_Add: zoom(1.1); return TRUE;
		case GDK_KEY_minus: case GDK_KEY_KP_Subtract: zoom(1 / 1.1); return TRUE;
		case GDK_KEY_0: zoom(0); return TRUE;
		case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab: {
			int n = gtk_notebook_get_n_pages(b), c = gtk_notebook_get_current_page(b);
			gtk_notebook_set_current_page(b, (c + (shift ? n - 1 : 1)) % n);
			return TRUE;
		}
		default: break;
		}
	}
	if (mods == GDK_MOD1_MASK && t) {
		if (k == GDK_KEY_Left) { webkit_web_view_go_back(t->web); return TRUE; }
		if (k == GDK_KEY_Right) { webkit_web_view_go_forward(t->web); return TRUE; }
	}
	if (!mods) {
		switch (k) {
		case GDK_KEY_F5: on_reload(NULL, NULL); return TRUE;
		case GDK_KEY_F11: toggle_full(); return TRUE;
		case GDK_KEY_F12: if (t) webkit_web_inspector_show(webkit_web_view_get_inspector(t->web)); return TRUE;
		case GDK_KEY_Escape:
			if (gtk_widget_get_visible(G.findbar)) { find_close(); return TRUE; }
			if (gtk_widget_has_focus(G.entry) && t) { sync_chrome(t); gtk_widget_grab_focus(GTK_WIDGET(t->web)); sync_chrome(t); return TRUE; }
			break;
		default: break;
		}
	}
	return FALSE;
}

/* ---- the tabs you had open come back next time ---- */
static char *session_path(void) { return g_build_filename(G.data_dir, "session", NULL); }

static void session_save(void)
{
	GString *out = g_string_new(NULL);
	GtkNotebook *b = GTK_NOTEBOOK(G.book);
	for (int i = 0; i < gtk_notebook_get_n_pages(b); i++) {
		Tab *t = tab_of(gtk_notebook_get_nth_page(b, i));
		const char *u = t ? shown_uri(t) : "";
		if (*u) g_string_append_printf(out, "%s\n", u);
	}
	char *path = session_path();
	g_file_set_contents(path, out->str, out->len, NULL);
	g_free(path);
	g_string_free(out, TRUE);
}

static int session_restore(void)
{
	char *path = session_path(), *data = NULL;
	int n = 0;
	if (g_file_get_contents(path, &data, NULL, NULL)) {
		char **lines = g_strsplit(data, "\n", -1);
		for (char **l = lines; *l; l++) if (**l) { tab_new(*l, NULL, n == 0); n++; }
		g_strfreev(lines);
		g_free(data);
	}
	g_free(path);
	return n;
}

static gboolean on_delete(GtkWidget *w, GdkEvent *e, gpointer u)
{
	session_save();
	history_save();
	return FALSE;
}

/* ---- the window ---- */
static GtkWidget *bar_button(const char *label, const char *tip, GCallback fn)
{
	GtkWidget *b = gtk_button_new_with_label(label);
	gtk_widget_set_tooltip_text(b, tip);
	gtk_widget_set_focus_on_click(b, FALSE);
	g_signal_connect(b, "clicked", fn, NULL);
	return b;
}

static void build_window(GtkApplication *app)
{
	G.win = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(G.win), APP_NAME);
	gtk_window_set_default_size(GTK_WINDOW(G.win), 1400, 950);
	gtk_window_set_icon_name(GTK_WINDOW(G.win), "syn-netjumper");
	g_signal_connect(G.win, "delete-event", G_CALLBACK(on_delete), NULL);
	g_signal_connect(G.win, "key-press-event", G_CALLBACK(on_key), NULL);

	G.bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_set_name(G.bar, "bar");
	GtkWidget *brand = gtk_label_new("NETJUMPER");
	gtk_widget_set_name(brand, "brand");
	G.back = bar_button("BACK", "Go back a page (Alt+Left)", G_CALLBACK(on_back));
	G.ahead = bar_button("AHEAD", "Go forward a page (Alt+Right)", G_CALLBACK(on_ahead));
	G.reload = bar_button("RELOAD", "Reload this page, or stop it loading (F5)", G_CALLBACK(on_reload));
	G.entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(G.entry), "Type an address or a search, then press Enter");
	gtk_entry_set_input_purpose(GTK_ENTRY(G.entry), GTK_INPUT_PURPOSE_URL);
	g_signal_connect(G.entry, "activate", G_CALLBACK(on_entry_activate), NULL);
	g_signal_connect_after(G.entry, "focus-in-event", G_CALLBACK(on_entry_focus), NULL);
	GtkWidget *plus = bar_button("NEW TAB", "Open a new tab (Ctrl+T)", G_CALLBACK(on_new_tab));
	gtk_box_pack_start(GTK_BOX(G.bar), brand, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(G.bar), G.back, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(G.bar), G.ahead, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(G.bar), G.reload, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(G.bar), G.entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(G.bar), plus, FALSE, FALSE, 0);

	GtkEntryCompletion *c = gtk_entry_completion_new();
	gtk_entry_completion_set_model(c, GTK_TREE_MODEL(G.history));
	gtk_entry_completion_set_text_column(c, H_URI);
	gtk_entry_completion_set_minimum_key_length(c, 2);
	gtk_entry_completion_set_popup_set_width(c, TRUE);
	gtk_entry_completion_set_match_func(c, complete_match, NULL, NULL);
	g_signal_connect(c, "match-selected", G_CALLBACK(on_complete_pick), NULL);
	gtk_entry_set_completion(GTK_ENTRY(G.entry), c);
	g_object_unref(c);

	G.book = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(G.book), TRUE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(G.book), FALSE);
	g_signal_connect(G.book, "switch-page", G_CALLBACK(on_switch), NULL);

	G.status = gtk_label_new("");
	gtk_widget_set_name(G.status, "status");
	gtk_label_set_ellipsize(GTK_LABEL(G.status), PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_max_width_chars(GTK_LABEL(G.status), 110);
	gtk_widget_set_halign(G.status, GTK_ALIGN_START);
	gtk_widget_set_valign(G.status, GTK_ALIGN_END);
	gtk_widget_set_no_show_all(G.status, TRUE);
	GtkWidget *over = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(over), G.book);
	gtk_overlay_add_overlay(GTK_OVERLAY(over), G.status);
	gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(over), G.status, TRUE);

	G.savebar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_name(G.savebar, "findbar");
	gtk_widget_set_no_show_all(G.savebar, TRUE);
	G.savelabel = gtk_label_new("");
	gtk_label_set_ellipsize(GTK_LABEL(G.savelabel), PANGO_ELLIPSIZE_END);
	gtk_widget_set_halign(G.savelabel, GTK_ALIGN_START);
	GtkWidget *saveyes = bar_button("SAVE PASSWORD", "Keep this login in the keyring", G_CALLBACK(on_save_yes));
	GtkWidget *saveno = bar_button("NOT NOW", "Don't save this one", G_CALLBACK(on_save_no));
	gtk_box_pack_start(GTK_BOX(G.savebar), G.savelabel, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(G.savebar), saveyes, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(G.savebar), saveno, FALSE, FALSE, 0);
	gtk_widget_show(G.savelabel);
	gtk_widget_show(saveyes);
	gtk_widget_show(saveno);

	G.findbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_set_name(G.findbar, "findbar");
	gtk_widget_set_no_show_all(G.findbar, TRUE);
	G.findentry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(G.findentry), "Find on this page");
	gtk_entry_set_width_chars(GTK_ENTRY(G.findentry), 40);
	g_signal_connect(G.findentry, "changed", G_CALLBACK(on_find_changed), NULL);
	g_signal_connect(G.findentry, "activate", G_CALLBACK(on_find_next), NULL);
	G.findcount = gtk_label_new("");
	GtkWidget *next = bar_button("NEXT", "Next match (Enter)", G_CALLBACK(on_find_next));
	GtkWidget *prev = bar_button("PREVIOUS", "Previous match", G_CALLBACK(on_find_prev));
	GtkWidget *done = bar_button("CLOSE", "Close find (Esc)", G_CALLBACK(on_find_close));
	GtkWidget *kids[] = {G.findentry, next, prev, G.findcount, done};
	for (size_t i = 0; i < G_N_ELEMENTS(kids); i++) {
		gtk_box_pack_start(GTK_BOX(G.findbar), kids[i], FALSE, FALSE, 0);
		gtk_widget_show(kids[i]);
	}

	GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(col), G.bar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(col), over, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(col), G.savebar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(col), G.findbar, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(G.win), col);
	gtk_widget_show_all(G.win);
}

/* ---- starting up: logins and site data stay in your home, third-party cookies don't ---- */
static void setup_engine(void)
{
	char *cache = g_build_filename(g_get_user_cache_dir(), "syn-netjumper", NULL);
	WebKitWebsiteDataManager *data = webkit_website_data_manager_new("base-data-directory", G.data_dir, "base-cache-directory", cache, NULL);
	webkit_website_data_manager_set_itp_enabled(data, TRUE);
	G.context = webkit_web_context_new_with_website_data_manager(data);
	webkit_web_context_set_cache_model(G.context, WEBKIT_CACHE_MODEL_WEB_BROWSER);
	WebKitCookieManager *cookies = webkit_web_context_get_cookie_manager(G.context);
	char *jar = g_build_filename(G.data_dir, "cookies.sqlite", NULL);
	webkit_cookie_manager_set_persistent_storage(cookies, jar, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
	webkit_cookie_manager_set_accept_policy(cookies, WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);
	g_signal_connect(G.context, "download-started", G_CALLBACK(on_download), NULL);
	g_free(jar);
	g_free(cache);
	g_object_unref(data);

	G.settings = webkit_settings_new();
	g_object_set(G.settings, "enable-developer-extras", TRUE, "enable-smooth-scrolling", TRUE, "javascript-can-open-windows-automatically", FALSE,
		"enable-back-forward-navigation-gestures", TRUE, "enable-media-capabilities", TRUE, "enable-mediasource", TRUE, NULL);
}

static void open_addresses(GFile **files, int n)
{
	for (int i = 0; i < n; i++) {
		char *uri = g_file_get_uri(files[i]);
		tab_new(uri, NULL, TRUE);
		g_free(uri);
	}
}

static void first_window(GApplication *app, gboolean restore)
{
	if (G.win) return;
	build_window(GTK_APPLICATION(app));
	if (restore && session_restore()) return;
	if (restore) tab_new(NULL, NULL, TRUE);
}

static void on_activate(GApplication *app, gpointer u)
{
	gboolean fresh = !G.win;
	first_window(app, TRUE);
	if (!fresh) new_blank_tab();
	gtk_window_present(GTK_WINDOW(G.win));
}

static void on_open(GApplication *app, GFile **files, int n, const char *hint, gpointer u)
{
	first_window(app, FALSE);
	open_addresses(files, n);
	gtk_window_present(GTK_WINDOW(G.win));
}

static void on_startup(GApplication *app, gpointer u)
{
	G.data_dir = g_build_filename(g_get_user_data_dir(), "syn-netjumper", NULL);
	g_mkdir_with_parents(G.data_dir, 0700);
	gtk_window_set_default_icon_name("syn-netjumper");
	G.css = gtk_css_provider_new();
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(G.css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
	char *name = live_theme_name();
	apply_theme(name);
	g_free(name);
	g_timeout_add_seconds(3, poll_theme, NULL);
	G.history = gtk_list_store_new(H_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);
	history_load();
	setup_engine();
}

int main(int argc, char **argv)
{
	/* syn-netjumper --import-passwords FILE.csv: import Chrome's own export, then stop */
	if (argc >= 3 && g_str_equal(argv[1], "--import-passwords")) {
		int n = pw_import(argv[2]);
		if (n < 0) {
			g_printerr("Couldn't read %s as a Chrome password export (needs a url and password column).\n", argv[2]);
			return 1;
		}
		g_print("Imported %d saved password%s into the keyring for SYN-NETJUMPER.\n", n, n == 1 ? "" : "s");
		return 0;
	}
	/* No AT-SPI accessibility bridge: under a dbus-run-session desktop (SYN-OS's
	   `synos` login) WebKit's page accessibility tree reaches atk-bridge with no
	   registry behind it, and atk-bridge segfaults in spi_register_object_to_path
	   the moment the pointer crosses the window (seen on the title bar). SYN-OS
	   ships no screen reader; set NO_AT_BRIDGE=0 to have the bridge anyway. */
	g_setenv("NO_AT_BRIDGE", "1", FALSE);
	/* the first run's name for the desktop; a second run hands its addresses to the first */
	GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_HANDLES_OPEN);
	g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
	g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
	g_signal_connect(app, "open", G_CALLBACK(on_open), NULL);
	int r = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return r;
}
