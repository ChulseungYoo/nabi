/* Nabi - X Input Method server for hangul
 * Copyright (C) 2003,2004,2007 Choe Hwanjin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <dirent.h>
#include <locale.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "eggtrayicon.h"

#include "debug.h"
#include "gettext.h"
#include "nabi.h"
#include "ic.h"
#include "server.h"
#include "conf.h"

#include "default-icons.h"

enum {
    THEMES_LIST_PATH = 0,
    THEMES_LIST_NONE,
    THEMES_LIST_HANGUL,
    THEMES_LIST_ENGLISH,
    THEMES_LIST_NAME,
    N_COLS
};

/* from preference.c */
GtkWidget* preference_window_create(void);
void preference_window_update(void);

static GtkWidget *about_dialog = NULL;
static GtkWidget *preference_dialog = NULL;

static gboolean create_tray_icon(gpointer data);
static void remove_event_filter();
static void create_resized_icons(gint default_size);
static GtkWidget* create_menu(void);

static GtkWidget *main_label = NULL;
static GdkPixbuf *default_icon = NULL;
static EggTrayIcon *tray_icon = NULL;

static GdkPixbuf *none_pixbuf = NULL;
static GdkPixbuf *hangul_pixbuf = NULL;
static GdkPixbuf *english_pixbuf = NULL;
static GtkWidget *none_image = NULL;
static GtkWidget *hangul_image = NULL;
static GtkWidget *english_image = NULL;

static void
load_colors(void)
{
    gboolean ret;
    GdkColor color;
    GdkColormap *colormap;

    colormap = gdk_colormap_get_system();

    /* preedit foreground */
    gdk_color_parse(nabi->config->preedit_fg, &color);
    ret = gdk_colormap_alloc_color(colormap, &color, FALSE, TRUE);
    if (ret)
	nabi_server->preedit_fg = color;
    else {
	color.pixel = 1;
	color.red = 0xffff;
	color.green = 0xffff;
	color.blue = 0xffff;
	ret = gdk_colormap_alloc_color(colormap, &color, FALSE, TRUE);
	nabi_server->preedit_fg = color;
    }

    /* preedit background */
    gdk_color_parse(nabi->config->preedit_bg, &color);
    ret = gdk_colormap_alloc_color(colormap, &color, FALSE, TRUE);
    if (ret)
	nabi_server->preedit_bg = color;
    else {
	color.pixel = 0;
	color.red = 0;
	color.green = 0;
	color.blue = 0;
	ret = gdk_colormap_alloc_color(colormap, &color, FALSE, TRUE);
	nabi_server->preedit_fg = color;
    }
}

static void
set_up_keyboard(void)
{
    /* keyboard layout */
    if (g_path_is_absolute(nabi->config->keyboard_layouts_file)) {
	nabi_server_load_keyboard_layout(nabi_server,
					 nabi->config->keyboard_layouts_file);
    } else {
	char* filename = g_build_filename(NABI_DATA_DIR,
				  nabi->config->keyboard_layouts_file, NULL);
	nabi_server_load_keyboard_layout(nabi_server,
					 filename);
	g_free(filename);
    }
    nabi_server_set_keyboard_layout(nabi_server, nabi->config->latin_keyboard);

    /* set keyboard */
    nabi_server_set_hangul_keyboard(nabi_server, nabi->config->hangul_keyboard);
}

void
nabi_app_new(void)
{
    nabi = g_new(NabiApplication, 1);

    nabi->xim_name = NULL;
    nabi->main_window = NULL;
    nabi->status_only = FALSE;
    nabi->session_id = NULL;
    nabi->icon_size = 0;

    nabi->root_window = NULL;

    nabi->mode_info_atom = 0;
    nabi->mode_info_type = 0;
    nabi->mode_info_xatom = 0;

    nabi->config = NULL;
}

void
nabi_app_init(int *argc, char ***argv)
{
    gchar *icon_filename;

    /* set XMODIFIERS env var to none before creating any widget
     * If this is not set, xim server will try to connect herself.
     * So It would be blocked */
    putenv("XMODIFIERS=\"@im=none\"");
    putenv("GTK_IM_MODULE=gtk-im-context-simple");

    /* process command line options */
    if (argc != NULL && argv != NULL) {
	int i, j, k;
	for (i = 1; i < *argc;) {
	    if (strcmp("-s", (*argv)[i]) == 0 ||
		strcmp("--status-only", (*argv)[i]) == 0) {
		nabi->status_only = TRUE;
		(*argv)[i] = NULL;
	    } else if (strcmp("--sm-client-id", (*argv)[i]) == 0 ||
		       strncmp("--sm-client-id=", (*argv)[i], 15) == 0) {
		gchar *session_id = (*argv)[i] + 14;
		if (*session_id == '=') {
		    session_id++;
		} else {
		    (*argv)[i] = NULL;
		    i++;
		    session_id = (*argv)[i];
		}
		(*argv)[i] = NULL;

		nabi->session_id = g_strdup(session_id);
	    } else if (strcmp("--xim-name", (*argv)[i]) == 0 ||
		       strncmp("--xim-name=", (*argv)[i], 11) == 0) {
		gchar *xim_name = (*argv)[i] + 10;
		if (*xim_name == '=') {
		    xim_name++;
		} else {
		    (*argv)[i] = NULL;
		    i++;
		    xim_name = (*argv)[i];
		}
		(*argv)[i] = NULL;

		nabi->xim_name = g_strdup(xim_name);
	    } else if (strcmp("-d", (*argv)[i]) == 0) {
		gchar* log_level = "0";
		(*argv)[i] = NULL;
		i++;
		log_level = (*argv)[i];

		nabi_log_set_level(strtol(log_level, NULL, 10));
	    }
	    i++;
	}

	/* if we accept any command line options, compress rest of argc, argv */
	for (i = 1; i < *argc; i++) {
	    for (k = i; k < *argc; k++)
		if ((*argv)[k] == NULL)
		    break;
	    if (k > i) {
		k -= i;
		for (j = i + k; j < *argc; j++)
		    (*argv)[j - k] = (*argv)[j];
		*argc -= k;
	    }
	}
    }

    nabi->config = nabi_config_new();
    nabi_config_load(nabi->config);

    /* set atoms for hangul status */
    nabi->mode_info_atom = gdk_atom_intern("_HANGUL_INPUT_MODE", TRUE);
    nabi->mode_info_type = gdk_atom_intern("INTEGER", TRUE);
    nabi->mode_info_xatom = gdk_x11_atom_to_xatom(nabi->mode_info_atom);

    /* default icon */
    nabi->icon_size = 24;
    icon_filename = g_build_filename(NABI_DATA_DIR, "nabi.svg", NULL);
    default_icon = gdk_pixbuf_new_from_file(icon_filename, NULL);
    if (default_icon == NULL) {
	g_free(icon_filename);
	icon_filename = g_build_filename(NABI_DATA_DIR, "nabi.png", NULL);
	default_icon = gdk_pixbuf_new_from_file(icon_filename, NULL);
    }
    g_free(icon_filename);
}

static void
set_up_output_mode(void)
{
    NabiOutputMode mode;

    mode = NABI_OUTPUT_SYLLABLE;
    if (nabi->config->output_mode != NULL) {
	if (g_ascii_strcasecmp(nabi->config->output_mode, "jamo") == 0) {
	    mode = NABI_OUTPUT_JAMO;
	} else if (g_ascii_strcasecmp(nabi->config->output_mode, "manual") == 0) {
	    mode = NABI_OUTPUT_MANUAL;
	}
    }
    nabi_server_set_output_mode(nabi_server, mode);
}

void
nabi_app_setup_server(void)
{
    const char *locale;
    char **keys;

    if (nabi->status_only)
	return;

    locale = setlocale(LC_CTYPE, NULL);
    if (locale == NULL ||
	!nabi_server_is_locale_supported(nabi_server, locale)) {
	GtkWidget *message;
	const gchar *encoding = "";

	g_get_charset(&encoding);
	message = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
					 GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
	   "<span size=\"x-large\" weight=\"bold\">"
	   "한글 입력기 나비에서 알림</span>\n\n"
	   "현재 로캘이 나비에서 지원하는 것이 아닙니다. "
	   "이렇게 되어 있으면 한글 입력에 문제가 있을수 있습니다. "
	   "설정을 확인해보십시오.\n\n"
	   "현재 로캘 설정: <b>%s (%s)</b>", locale, encoding);
	gtk_label_set_use_markup(GTK_LABEL(GTK_MESSAGE_DIALOG(message)->label),
				 TRUE);
	gtk_widget_show(message);
	gtk_dialog_run(GTK_DIALOG(message));
	gtk_widget_destroy(message);
    }

    set_up_keyboard();
    load_colors();
    set_up_output_mode();
    keys = g_strsplit(nabi->config->trigger_keys, ",", 0);
    nabi_server_set_trigger_keys(nabi_server, keys);
    g_strfreev(keys);
    keys = g_strsplit(nabi->config->candidate_keys, ",", 0);
    nabi_server_set_candidate_keys(nabi_server, keys);
    g_strfreev(keys);
    nabi_server_set_candidate_font(nabi_server, nabi->config->candidate_font);
    nabi_server_set_dynamic_event_flow(nabi_server,
				       nabi->config->use_dynamic_event_flow);
    nabi_server_set_commit_by_word(nabi_server, nabi->config->commit_by_word);
    nabi_server_set_auto_reorder(nabi_server, nabi->config->auto_reorder);
}

void
nabi_app_quit(void)
{
    if (about_dialog != NULL) {
	gtk_dialog_response(GTK_DIALOG(about_dialog), GTK_RESPONSE_CANCEL);
    }

    if (preference_dialog != NULL) {
	gtk_dialog_response(GTK_DIALOG(preference_dialog), GTK_RESPONSE_CANCEL);
    }

    if (nabi != NULL && nabi->main_window != NULL) {
	gtk_window_get_position(GTK_WINDOW(nabi->main_window),
				&nabi->config->x, &nabi->config->y);
	gtk_widget_destroy(nabi->main_window);
	nabi->main_window = NULL;
    }
}

void
nabi_app_free(void)
{
    /* remove default icon */
    if (default_icon != NULL) {
	gdk_pixbuf_unref(default_icon);
	default_icon = NULL;
    }

    if (nabi == NULL)
	return;

    nabi_app_save_config();
    nabi_config_delete(nabi->config);
    nabi->config = NULL;

    if (nabi->main_window != NULL) {
	gtk_widget_destroy(nabi->main_window);
	nabi->main_window = NULL;
    }

    g_free(nabi->xim_name);

    g_free(nabi);
    nabi = NULL;
}

static void
on_tray_icon_embedded(GtkWidget *widget, gpointer data)
{
    if (nabi != NULL &&
	nabi->main_window != NULL &&
	GTK_WIDGET_VISIBLE(nabi->main_window)) {
	gtk_window_get_position(GTK_WINDOW(nabi->main_window),
				&nabi->config->x, &nabi->config->y);
	gtk_widget_hide(GTK_WIDGET(nabi->main_window));
    }
}

static void
on_tray_icon_destroyed(GtkWidget *widget, gpointer data)
{
    g_object_unref(G_OBJECT(none_pixbuf));
    g_object_unref(G_OBJECT(hangul_pixbuf));
    g_object_unref(G_OBJECT(english_pixbuf));

    /* clean tray icon widget static variable */
    none_pixbuf = NULL;
    hangul_pixbuf = NULL;
    english_pixbuf = NULL;
    none_image = NULL;
    hangul_image = NULL;
    english_image = NULL;

    tray_icon = NULL;
    g_idle_add(create_tray_icon, NULL);
    g_print("Nabi: tray icon destroyed\n");

    if (nabi != NULL &&
	nabi->main_window != NULL &&
	!GTK_WIDGET_VISIBLE(nabi->main_window)) {
	gtk_window_move(GTK_WINDOW(nabi->main_window),
			nabi->config->x, nabi->config->y);
	gtk_widget_show(GTK_WIDGET(nabi->main_window));
    }
}

static void
on_main_window_destroyed(GtkWidget *widget, gpointer data)
{
    if (tray_icon != NULL)
	gtk_widget_destroy(GTK_WIDGET(tray_icon));
    remove_event_filter();
    gtk_main_quit();
    g_print("Nabi: main window destroyed\n");
}

static void
nabi_menu_position_func(GtkMenu *menu,
			gint *x, gint *y,
			gboolean *push_in,
			gpointer data)
{
    GdkWindow *window;
    GdkScreen *screen;
    gint width, height, menu_width, menu_height;
    gint screen_width, screen_height;

    window = GDK_WINDOW(GTK_WIDGET(data)->window);
    screen = gdk_drawable_get_screen(GDK_DRAWABLE(window));
    screen_width = gdk_screen_get_width(screen);
    screen_height = gdk_screen_get_height(screen);
    gdk_window_get_origin(window, x, y);
    gdk_drawable_get_size(window, &width, &height);
    if (!GTK_WIDGET_REALIZED(menu))
	gtk_widget_realize(GTK_WIDGET(menu));
    gdk_drawable_get_size(GDK_WINDOW(GTK_WIDGET(menu)->window),
			  &menu_width, &menu_height);

    if (*y + height < screen_height / 2)
	*y += height;
    else
	*y -= menu_height;

    if (*x + menu_width > screen_width)
	*x = screen_width - menu_width;
}

static gboolean
on_tray_icon_button_press(GtkWidget *widget,
			  GdkEventButton *event,
			  gpointer data)
{
    static GtkWidget *menu = NULL;

    if (event->type != GDK_BUTTON_PRESS)
	return FALSE;

    switch (event->button) {
    case 1:
    case 3:
	if (menu != NULL)
	    gtk_widget_destroy(menu);
	menu = create_menu();
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
		       nabi_menu_position_func, widget,
		       event->button, event->time);
	return TRUE;
    case 2:
	if (GTK_WIDGET_VISIBLE(nabi->main_window)) {
	    gtk_window_get_position(GTK_WINDOW(nabi->main_window),
			    &nabi->config->x, &nabi->config->y);
	    gtk_widget_hide(GTK_WIDGET(nabi->main_window));
	} else {
	    gtk_window_move(GTK_WINDOW(nabi->main_window),
			    nabi->config->x, nabi->config->y);
	    gtk_widget_show(GTK_WIDGET(nabi->main_window));
	}
	return TRUE;
    default:
	break;
    }

    return FALSE;
}

static void
on_tray_icon_size_allocate (GtkWidget *widget,
			    GtkAllocation *allocation,
			    gpointer data)
{
    GtkOrientation orientation;
    int size;

    orientation = egg_tray_icon_get_orientation (tray_icon);
    if (orientation == GTK_ORIENTATION_HORIZONTAL)
	size = allocation->height;
    else
	size = allocation->width;

    /* We set minimum icon size */
    if (size <= 18) {
	size = 16;
    } else if (size <= 24) {
	size = 24;
    }

    if (size != nabi->icon_size) {
	nabi->icon_size = size;
	create_resized_icons (size);
    }
}

static void get_statistic_string(GString *str)
{
    if (nabi_server == NULL) {
	g_string_assign(str, _("XIM Server is not running"));
    } else {
	int i;
	int sum;

	g_string_append_printf(str, 
		 "%s: %3d\n"
		 "%s: %3d\n"
		 "%s: %3d\n"
		 "%s: %3d\n"
		 "\n",
		 _("Total"), nabi_server->statistics.total,
		 _("Space"), nabi_server->statistics.space,
		 _("BackSpace"), nabi_server->statistics.backspace,
		 _("Shift"), nabi_server->statistics.shift);

	/* choseong */
	g_string_append(str, _("Choseong"));
	g_string_append_c(str, '\n');
	for (i = 0x00; i <= 0x12; i++) {
	    char label[8] = { '\0', };
	    int len = g_unichar_to_utf8(0x1100 + i, label);
	    label[len] = '\0';
	    g_string_append_printf(str, "%s: %-3d ",
				   label, nabi_server->statistics.jamo[i]);
	    if ((i + 1) % 10 == 0)
		g_string_append_c(str, '\n');
	}
	g_string_append(str, "\n\n");

	/* jungseong */
	g_string_append(str, _("Jungseong"));
	g_string_append_c(str, '\n');
	for (i = 0x61; i <= 0x75; i++) {
	    char label[8] = { '\0', };
	    int len = g_unichar_to_utf8(0x1100 + i, label);
	    label[len] = '\0';
	    g_string_append_printf(str, "%s: %-3d ",
				   label, nabi_server->statistics.jamo[i]);
	    if ((i - 0x60) % 10 == 0)
		g_string_append_c(str, '\n');
	}
	g_string_append(str, "\n\n");

	/* print only when a user have pressed any jonseong keys */
	sum = 0;
	for (i = 0xa8; i <= 0xc2; i++) {
	    sum += nabi_server->statistics.jamo[i];
	}

	if (sum > 0) {
	    /* jongseong */
	    g_string_append(str, _("Jongseong"));
	    g_string_append_c(str, '\n');
	    for (i = 0xa8; i <= 0xc2; i++) {
		char label[8] = { '\0', };
		int len = g_unichar_to_utf8(0x1100 + i, label);
		label[len] = '\0';
		g_string_append_printf(str, "%s: %-3d ",
				       label, nabi_server->statistics.jamo[i]);
		if ((i - 0xa7) % 10 == 0)
		    g_string_append_c(str, '\n');
	    }
	    g_string_append(str, "\n");
	}
    }
}

static void
on_about_statistics_clicked(GtkWidget *widget, gpointer parent)
{
    GString *str;
    GtkWidget *hbox;
    GtkWidget *stat_label = NULL;
    GtkWidget *dialog = NULL;

    dialog = gtk_dialog_new_with_buttons(_("Nabi keypress statistics"),
	    				 GTK_WINDOW(parent),
					 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_STOCK_CLOSE,
					 GTK_RESPONSE_CLOSE,
					 NULL);

    hbox = gtk_hbox_new(FALSE, 10);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 0);

    str = g_string_new(NULL);
    get_statistic_string(str);
    stat_label = gtk_label_new(str->str);
    gtk_label_set_selectable(GTK_LABEL(stat_label), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), stat_label, TRUE, TRUE, 6);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    g_string_free(str, TRUE);
}

static void
on_menu_about(GtkWidget *widget)
{
    GtkWidget *dialog = NULL;
    GtkWidget *vbox;
    GtkWidget *comment;
    GtkWidget *server_info;
    GtkWidget *image;
    GtkWidget *label;
    GList *list;
    gchar *image_filename;
    gchar *comment_str;
    gchar buf[256];
    const char *encoding = "";

    if (about_dialog != NULL) {
	gtk_window_present(GTK_WINDOW(about_dialog));
	return;
    }

    dialog = gtk_dialog_new_with_buttons(_("About Nabi"),
	    				 NULL,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_STOCK_CLOSE,
					 GTK_RESPONSE_CLOSE,
					 NULL);
    about_dialog = dialog;
    vbox = GTK_DIALOG(dialog)->vbox;

    image_filename = g_build_filename(NABI_DATA_DIR, "nabi-about.png", NULL);
    image = gtk_image_new_from_file(image_filename);
    gtk_misc_set_padding(GTK_MISC(image), 0, 6);
    gtk_box_pack_start(GTK_BOX(vbox), image, FALSE, TRUE, 0);
    gtk_widget_show(image);
    g_free(image_filename);

    comment = gtk_label_new(NULL);
    comment_str = g_strdup_printf(
			_("<span size=\"large\">An Easy Hangul XIM</span>\n"
			  "version %s\n\n"
			  "Copyright (c) 2003-2004 Choe Hwanjin"), VERSION);
    gtk_label_set_markup(GTK_LABEL(comment), comment_str);
    gtk_label_set_justify(GTK_LABEL(comment), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), comment, FALSE, TRUE, 0);
    gtk_widget_show(comment);

    server_info = gtk_table_new(4, 2, TRUE);
    label = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label),
			 _("<span weight=\"bold\">XIM name</span>: "));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(server_info), label, 0, 1, 0, 1);

    label = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label),
			 _("<span weight=\"bold\">Locale</span>: "));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(server_info), label, 0, 1, 1, 2);

    label = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label),
			 _("<span weight=\"bold\">Encoding</span>: "));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(server_info), label, 0, 1, 2, 3);

    label = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label),
			 _("<span weight=\"bold\">Connected</span>: "));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(server_info), label, 0, 1, 3, 4);

    label = gtk_label_new(nabi_server->name);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(server_info), label, 1, 2, 0, 1);

    label = gtk_label_new(setlocale(LC_CTYPE, NULL));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(server_info), label, 1, 2, 1, 2);

    g_get_charset(&encoding);
    label = gtk_label_new(encoding);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(server_info), label, 1, 2, 2, 3);

    snprintf(buf, sizeof(buf), "%d", g_slist_length(nabi_server->connections));
    label = gtk_label_new(buf);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(server_info), label, 1, 2, 3, 4);

    gtk_box_pack_start(GTK_BOX(vbox), server_info, FALSE, TRUE, 5);
    gtk_widget_show_all(server_info);

    if (!nabi->status_only) {
	GtkWidget *hbox;
	GtkWidget *button;

	hbox = gtk_hbox_new(TRUE, 10);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
			   hbox, FALSE, TRUE, 0);
	gtk_widget_show(hbox);

	button = gtk_button_new_with_label(_("Keypress Statistics"));
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_widget_show(button);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 5);

	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(on_about_statistics_clicked), dialog);
    }

    gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 200);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    gtk_widget_show(dialog);

    list = gtk_container_get_children(GTK_CONTAINER(GTK_DIALOG(dialog)->action_area));
    if (list != NULL) {
	GList *child = g_list_last(list);
	if (child != NULL && child->data != NULL)
	    gtk_widget_grab_focus(GTK_WIDGET(child->data));
	g_list_free(list);
    }

    gtk_dialog_run(GTK_DIALOG(about_dialog));
    gtk_widget_destroy(about_dialog);
    about_dialog = NULL;
}

static void
on_menu_preference(GtkWidget *widget)
{
    if (preference_dialog != NULL) {
	gtk_window_present(GTK_WINDOW(preference_dialog));
	return;
    }

    preference_dialog = preference_window_create();
    gtk_dialog_run(GTK_DIALOG(preference_dialog));
    gtk_widget_destroy(preference_dialog);
    preference_dialog = NULL;
}

static void
on_menu_keyboard(GtkWidget *widget, gpointer data)
{
    nabi_app_set_hangul_keyboard((const char*)data);
    preference_window_update();
}

static void
on_menu_quit(GtkWidget *widget)
{
    nabi_app_quit();
}

static GtkWidget*
create_menu(void)
{
    GtkWidget* menu;
    GtkWidget* menu_item;
    GtkWidget* image;
    GtkAccelGroup *accel_group;
    GSList *radio_group = NULL;

    accel_group = gtk_accel_group_new();

    menu = gtk_menu_new();
    gtk_widget_show(menu);

    /* menu about */
    image = gtk_image_new_from_stock(GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_MENU);
    gtk_widget_show(image);
    menu_item = gtk_image_menu_item_new_with_mnemonic(_("_About..."));
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menu_item), image);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);
    g_signal_connect_swapped(G_OBJECT(menu_item), "activate",
			     G_CALLBACK(on_menu_about), menu_item);

    /* separator */
    menu_item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);

    /* menu preferences */
    image = gtk_image_new_from_stock(GTK_STOCK_PREFERENCES, GTK_ICON_SIZE_MENU);
    gtk_widget_show(image);
    menu_item = gtk_image_menu_item_new_with_mnemonic(_("_Preferences..."));
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menu_item), image);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);
    g_signal_connect_swapped(G_OBJECT(menu_item), "activate",
			     G_CALLBACK(on_menu_preference), menu_item);

    /* separator */
    menu_item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);

    /* keyboard list */
    if (!nabi->status_only) {
	int i = 0;
	const NabiHangulKeyboard* keyboards;
	keyboards = nabi_server_get_hangul_keyboard_list(nabi_server);
	while (keyboards[i].id != NULL) {
	    const char* id = keyboards[i].id;
	    const char* name = keyboards[i].name;
	    menu_item = gtk_radio_menu_item_new_with_label(radio_group,
							   _(name));
	    radio_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
	    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
	    gtk_widget_show(menu_item);
	    g_signal_connect(G_OBJECT(menu_item), "activate",
			     G_CALLBACK(on_menu_keyboard), (gpointer)id);
	    if (strcmp(id, nabi->config->hangul_keyboard) == 0)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
					       TRUE);
	    i++;
	}

	/* separator */
	menu_item = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
	gtk_widget_show(menu_item);
    }

    /* menu quit */
    menu_item = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, accel_group);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    gtk_widget_show(menu_item);
    g_signal_connect_swapped(G_OBJECT(menu_item), "activate",
			     G_CALLBACK(on_menu_quit), menu_item);

    return menu;
}

static void
create_resized_icons(gint default_size)
{
    GtkOrientation orientation;
    double factor;
    gint new_width, new_height;
    gint orig_width, orig_height;
    GdkPixbuf *pixbuf;
    GdkInterpType scale_method = GDK_INTERP_NEAREST;

    orig_width = gdk_pixbuf_get_width(none_pixbuf);
    orig_height = gdk_pixbuf_get_height(none_pixbuf);

    orientation = egg_tray_icon_get_orientation (tray_icon);
    if (orientation == GTK_ORIENTATION_VERTICAL) {
	factor =  (double)default_size / (double)orig_width;
	new_width = default_size;
	new_height = (int)(orig_height * factor);
    } else {
	factor = (double)default_size / (double)orig_height;
	new_width = (int)(orig_width * factor);
	new_height = default_size;
    }

    if (factor < 1)
	scale_method = GDK_INTERP_BILINEAR;

    pixbuf = gdk_pixbuf_scale_simple(none_pixbuf, new_width, new_height,
	    			     scale_method);
    if (none_image == NULL)
	none_image = gtk_image_new_from_pixbuf(pixbuf);
    else
	gtk_image_set_from_pixbuf(GTK_IMAGE(none_image), pixbuf);
    g_object_unref(G_OBJECT(pixbuf));

    pixbuf = gdk_pixbuf_scale_simple(hangul_pixbuf, new_width, new_height,
	    			     scale_method);
    if (hangul_image == NULL)
	hangul_image = gtk_image_new_from_pixbuf(pixbuf);
    else
	gtk_image_set_from_pixbuf(GTK_IMAGE(hangul_image), pixbuf);
    g_object_unref(G_OBJECT(pixbuf));

    pixbuf = gdk_pixbuf_scale_simple(english_pixbuf, new_width, new_height,
	    			     scale_method);
    if (english_image == NULL)
	english_image = gtk_image_new_from_pixbuf(pixbuf);
    else
	gtk_image_set_from_pixbuf(GTK_IMAGE(english_image), pixbuf);
    g_object_unref(G_OBJECT(pixbuf));
}

static void
update_state (int state)
{
    switch (state) {
    case 0:
	gtk_window_set_title(GTK_WINDOW(nabi->main_window), _("Nabi: None"));
	if (main_label != NULL)
	    gtk_label_set_text(GTK_LABEL(main_label), _("Nabi: None"));
	if (none_image != NULL &&
	    hangul_image != NULL &&
	    english_image != NULL) {
	    gtk_widget_show(none_image);
	    gtk_widget_hide(hangul_image);
	    gtk_widget_hide(english_image);
	}
	break;
    case 1:
	gtk_window_set_title(GTK_WINDOW(nabi->main_window), _("Nabi: English"));
	if (main_label != NULL)
	    gtk_label_set_text(GTK_LABEL(main_label), _("Nabi: English"));
	if (none_image != NULL &&
	    hangul_image != NULL &&
	    english_image != NULL) {
	    gtk_widget_hide(none_image);
	    gtk_widget_hide(hangul_image);
	    gtk_widget_show(english_image);
	}
	break;
    case 2:
	gtk_window_set_title(GTK_WINDOW(nabi->main_window), _("Nabi: Hangul"));
	if (main_label != NULL)
	    gtk_label_set_text(GTK_LABEL(main_label), _("Nabi: Hangul"));
	if (none_image != NULL &&
	    hangul_image != NULL &&
	    english_image != NULL) {
	    gtk_widget_hide(none_image);
	    gtk_widget_show(hangul_image);
	    gtk_widget_hide(english_image);
	}
	break;
    default:
	gtk_window_set_title(GTK_WINDOW(nabi->main_window), _("Nabi: None"));
	if (main_label != NULL)
	    gtk_label_set_text(GTK_LABEL(main_label), _("Nabi: None"));
	gtk_widget_show(none_image);
	gtk_widget_hide(hangul_image);
	gtk_widget_hide(english_image);
	break;
    }
}

static void
nabi_set_input_mode_info (int state)
{
    long data = state;

    if (nabi->root_window == NULL)
	return;

    gdk_property_change (nabi->root_window,
		         gdk_atom_intern ("_HANGUL_INPUT_MODE", FALSE),
		         gdk_atom_intern ("INTEGER", FALSE),
		         32, GDK_PROP_MODE_REPLACE,
		         (const guchar *)&data, 1);
}

static GdkFilterReturn
root_window_event_filter (GdkXEvent *gxevent, GdkEvent *event, gpointer data)
{
    XEvent *xevent;
    XPropertyEvent *pevent;

    xevent = (XEvent*)gxevent;
    switch (xevent->type) {
    case PropertyNotify:
	pevent = (XPropertyEvent*)xevent;
	if (pevent->atom == nabi->mode_info_xatom) {
	    int state;
	    guchar *buf;
	    gboolean ret;

	    ret = gdk_property_get (nabi->root_window,
				    nabi->mode_info_atom,
				    nabi->mode_info_type,
				    0, 32, 0,
				    NULL, NULL, NULL,
				    &buf);
	    memcpy(&state, buf, sizeof(state));
	    update_state(state);
	    g_free(buf);
	}
	break;
    default:
	break;
    }
    return GDK_FILTER_CONTINUE;
}

static void
install_event_filter(GtkWidget *widget)
{
    GdkScreen *screen;

    screen = gdk_drawable_get_screen(GDK_DRAWABLE(widget->window));
    nabi->root_window = gdk_screen_get_root_window(screen);
    gdk_window_set_events(nabi->root_window, GDK_PROPERTY_CHANGE_MASK);
    gdk_window_add_filter(nabi->root_window, root_window_event_filter, NULL);
}

static void
remove_event_filter()
{
    gdk_window_remove_filter(nabi->root_window, root_window_event_filter, NULL);
}

static gboolean
on_main_window_button_pressed(GtkWidget *widget,
			      GdkEventButton *event,
			      gpointer data)
{
    static GtkWidget *menu = NULL;

    if (event->type != GDK_BUTTON_PRESS)
	return FALSE;

    switch (event->button) {
    case 1:
	gtk_window_begin_move_drag(GTK_WINDOW(data),
				   event->button,
				   event->x_root, event->y_root, 
				   event->time);
	return TRUE;
	break;
    case 3:
	if (menu != NULL)
	    gtk_widget_destroy(menu);
	menu = create_menu();
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
		       event->button, event->time);
	return TRUE;
    default:
	break;
    }
    return FALSE;
}

static void
on_main_window_realized(GtkWidget *widget, gpointer data)
{
    install_event_filter(widget);
    if (!nabi->status_only)
	nabi_server_set_mode_info_cb(nabi_server, nabi_set_input_mode_info);
}

static GdkPixbuf*
load_icon(const char* theme, const char* name, const char** default_xpm)
{
    char* path;
    char* filename;
    GError *error = NULL;
    GdkPixbuf *pixbuf;

    filename = g_strconcat(name, ".svg", NULL);
    path = g_build_filename(NABI_THEMES_DIR, theme, filename, NULL);
    pixbuf = gdk_pixbuf_new_from_file(path, &error);
    if (pixbuf != NULL)
	goto done;

    g_free(filename);
    g_free(path);
    if (error != NULL) {
	g_error_free(error);
	error = NULL;
    }

    filename = g_strconcat(name, ".png", NULL);
    path = g_build_filename(NABI_THEMES_DIR, theme, filename, NULL);
    pixbuf = gdk_pixbuf_new_from_file(path, &error);
    if (pixbuf == NULL) {
	if (error != NULL) {
	    g_print("Error on reading image file: %s\n", error->message);
	    g_error_free(error);
	}
	pixbuf = gdk_pixbuf_new_from_xpm_data(none_default_xpm);
    }

done:
    g_free(filename);
    g_free(path);
    if (error != NULL)
	g_error_free(error);

    return pixbuf;
}

static void
load_base_icons(const gchar *theme)
{
    if (theme == NULL)
    	theme = "Jini";

    none_pixbuf = load_icon(theme, "none", none_default_xpm);
    english_pixbuf = load_icon(theme, "english", english_default_xpm);
    hangul_pixbuf = load_icon(theme, "hangul", hangul_default_xpm);
}

static gboolean
create_tray_icon(gpointer data)
{
    GtkWidget *eventbox;
    GtkWidget *hbox;
    GtkTooltips *tooltips;

    if (tray_icon != NULL)
	return FALSE;

    tray_icon = egg_tray_icon_new("Tray icon");

    eventbox = gtk_event_box_new();
    gtk_widget_show(eventbox);
    gtk_container_add(GTK_CONTAINER(tray_icon), eventbox);
    g_signal_connect(G_OBJECT(eventbox), "button-press-event",
		     G_CALLBACK(on_tray_icon_button_press), NULL);

    tooltips = gtk_tooltips_new();
    gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), eventbox,
			 _("Hangul input method: Nabi"),
			 _("Hangul input method: Nabi"
			   " - You can input hangul using this program"));

    load_base_icons(nabi->config->theme);
    create_resized_icons(nabi->icon_size);

    hbox = gtk_hbox_new(TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), none_image, TRUE, TRUE, 0);
    gtk_widget_show(none_image);
    gtk_box_pack_start(GTK_BOX(hbox), hangul_image, TRUE, TRUE, 0);
    gtk_widget_hide(hangul_image);
    gtk_box_pack_start(GTK_BOX(hbox), english_image, TRUE, TRUE, 0);
    gtk_widget_hide(english_image);
    gtk_container_add(GTK_CONTAINER(eventbox), hbox);
    gtk_widget_show(hbox);

    g_signal_connect(G_OBJECT(tray_icon), "size-allocate",
		     G_CALLBACK(on_tray_icon_size_allocate), NULL);

    g_signal_connect(G_OBJECT(tray_icon), "embedded",
		     G_CALLBACK(on_tray_icon_embedded), NULL);
    g_signal_connect(G_OBJECT(tray_icon), "destroy",
		     G_CALLBACK(on_tray_icon_destroyed), NULL);
    gtk_widget_show(GTK_WIDGET(tray_icon));
    return FALSE;
}

GtkWidget*
nabi_app_create_main_widget(void)
{
    GtkWidget *window;
    GtkWidget *frame;
    GtkWidget *ebox;

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), _("Nabi: None"));
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_stick(GTK_WINDOW(window));
    gtk_window_move(GTK_WINDOW(window), nabi->config->x, nabi->config->y);
    g_signal_connect_after(G_OBJECT(window), "realize",
	    		   G_CALLBACK(on_main_window_realized), NULL);
    g_signal_connect(G_OBJECT(window), "destroy",
		     G_CALLBACK(on_main_window_destroyed), NULL);

    frame = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(window), frame);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
    gtk_widget_show(frame);

    ebox = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(frame), ebox);
    g_signal_connect(G_OBJECT(ebox), "button-press-event",
		     G_CALLBACK(on_main_window_button_pressed), window);
    gtk_widget_show(ebox);

    main_label = gtk_label_new(_("Nabi: None"));
    gtk_container_add(GTK_CONTAINER(ebox), main_label);
    gtk_widget_show(main_label);

    if (nabi != NULL)
	nabi->main_window = window;

    g_idle_add(create_tray_icon, NULL);

    if (default_icon != NULL) {
	GList *list = g_list_prepend(NULL, default_icon);
	gtk_window_set_default_icon_list(list);
	g_list_free(list);
    }

    return window;
}

void
nabi_app_set_theme(const gchar *name)
{
    load_base_icons(name);
    create_resized_icons(nabi->icon_size);

    g_free(nabi->config->theme);
    nabi->config->theme = g_strdup(name);
}

void
nabi_app_set_hangul_keyboard(const char *id)
{
    nabi_server_set_hangul_keyboard(nabi_server, id);
    g_free(nabi->config->hangul_keyboard);
    if (id == NULL)
	nabi->config->hangul_keyboard = NULL;
    else
	nabi->config->hangul_keyboard = g_strdup(id);
}

void
nabi_app_save_config()
{
    if (nabi != NULL && nabi->config != NULL)
	nabi_config_save(nabi->config);
}

/* vim: set ts=8 sts=4 sw=4 : */
