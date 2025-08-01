/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * panel-action-button.c: panel "Action Button" module
 *
 * Copyright (C) 2002 Sun Microsystems, Inc.
 * Copyright (C) 2004 Red Hat, Inc.
 * Copyright (C) 2012-2021 MATE Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors:
 *	Mark McLoughlin <mark@skynet.ie>
 */

#include <config.h>
#include <string.h>
#include <stdlib.h>

#include "panel-action-button.h"

#include <glib/gi18n.h>
#include <gio/gio.h>

#define MATE_DESKTOP_USE_UNSTABLE_API
#include <libmate-desktop/mate-desktop-utils.h>
#include <libmate-desktop/mate-gsettings.h>

#include <libpanel-util/panel-error.h>
#include <libpanel-util/panel-launch.h>
#include <libpanel-util/panel-session-manager.h>
#include <libpanel-util/panel-show.h>

#include "applet.h"
#include "panel-config-global.h"
#include "panel-profile.h"
#include "panel-typebuiltins.h"
#include "panel-util.h"
#include "panel-session.h"
#include "panel-globals.h"
#include "panel-run-dialog.h"
#include "panel-a11y.h"
#include "panel-lockdown.h"
#include "panel-icon-names.h"
#include "panel-schemas.h"

#ifdef HAVE_X11
#include <gdk/gdkx.h>
#include "panel-force-quit.h"
#endif

#ifdef HAVE_WAYLAND
#include <gdk/gdkwayland.h>

#endif

enum {
	PROP_0,
	PROP_ACTION_TYPE,
	PROP_DND_ENABLED
};

struct _PanelActionButtonPrivate {
	PanelActionButtonType  type;
	AppletInfo            *info;
	GSettings             *settings;

	guint                  dnd_enabled : 1;
};

G_DEFINE_TYPE_WITH_PRIVATE (PanelActionButton, panel_action_button, BUTTON_TYPE_WIDGET)

static void
panel_action_button_type_changed (GSettings       *settings,
								  gchar           *key,
								  PanelActionButton *button);

static const char *panel_action_type [PANEL_ACTION_LAST] = {
  [PANEL_ACTION_NONE] =           "none",
  [PANEL_ACTION_LOCK] =           "lock",
  [PANEL_ACTION_LOGOUT] =         "logout",
  [PANEL_ACTION_RUN] =            "run",
  [PANEL_ACTION_SEARCH] =         "search",
  [PANEL_ACTION_FORCE_QUIT] =     "force-quit",
  [PANEL_ACTION_CONNECT_SERVER] = "connect-server",
  [PANEL_ACTION_SHUTDOWN] =       "shutdown"
};

static const gchar*
get_action_type_name (gint id)
{
  if ((id >= 0) && (id < PANEL_ACTION_LAST))
    return panel_action_type [id];

  return NULL;
}

static gboolean
get_action_type_id (const gchar *name,
                    gint        *id)
{
  gint i;

  for (i = 0; i < PANEL_ACTION_LAST; i++)
    {
      if (g_ascii_strcasecmp (panel_action_type [i], name) == 0)
        {
          *id = i;
          return TRUE;
        }
    }
  *id = 0;
  return FALSE;
}

/* Lock Screen
 */
static void panel_action_lock_screen(GtkWidget* widget)
{
	panel_lock_screen(gtk_widget_get_screen(widget));
}

static gboolean
screensaver_properties_enabled (void)
{
	if (panel_lockdown_get_locked_down () ||
	    panel_lockdown_get_disable_lock_screen ())
		return FALSE;

	return panel_lock_screen_action_available ("prefs");
}

static gboolean
screensaver_enabled (void)
{
	if (panel_lockdown_get_disable_lock_screen ())
		return FALSE;

	return panel_lock_screen_action_available ("lock");
}

static gboolean
panel_action_lock_is_disabled (void)
{
	return !screensaver_enabled ();
}

static void
panel_action_lock_setup_menu (PanelActionButton *button)
{
	mate_panel_applet_add_callback (button->priv->info,
				   "activate",
				   NULL,
				   _("_Activate Screensaver"),
				   screensaver_enabled);

	mate_panel_applet_add_callback (button->priv->info,
				   "lock",
				   NULL,
				   _("_Lock Screen"),
				   screensaver_enabled);

	mate_panel_applet_add_callback (button->priv->info,
				   "prefs",
				   "document-properties",
				   _("_Properties"),
				   screensaver_properties_enabled);
}

static void
panel_action_lock_invoke_menu (PanelActionButton *button,
			       const char *callback_name)
{
	g_return_if_fail (PANEL_IS_ACTION_BUTTON (button));
	g_return_if_fail (callback_name != NULL);

	panel_lock_screen_action (gtk_widget_get_screen (GTK_WIDGET (button)),
				  callback_name);
}

/* Log Out
 */
static void
panel_action_logout (GtkWidget *widget)
{
	PanelSessionManager *manager;
	gboolean             prompt = TRUE;

	if (mate_gsettings_schema_exists (MATE_SESSION_SCHEMA)) {
		GSettings *msm_settings;
		msm_settings = g_settings_new (MATE_SESSION_SCHEMA);
		prompt = g_settings_get_boolean (msm_settings, MATE_SESSION_LOGOUT_PROMPT_KEY);
		g_object_unref (msm_settings);
	}

	manager = panel_session_manager_get ();

	if (!prompt)
		panel_session_manager_request_logout (manager,
						      PANEL_SESSION_MANAGER_LOGOUT_MODE_NO_CONFIRMATION);
	else
		/* FIXME: we need to use widget to get the screen for the
		 * confirmation dialog, see
		 * http://bugzilla.gnome.org/show_bug.cgi?id=536914 */
		panel_session_manager_request_logout (manager,
						      PANEL_SESSION_MANAGER_LOGOUT_MODE_NORMAL);
}

/* Shutdown
 */
#ifdef HAVE_WAYLAND
static void
show_error_dialog (GtkWidget *label)
{
	GtkWidget *dialog, *box, *content_area, *button, *image;
	dialog = gtk_dialog_new ();
	button = gtk_dialog_add_button (GTK_DIALOG(dialog),
			       "Cancel",
			       GTK_RESPONSE_CANCEL);

	image = gtk_image_new_from_icon_name ("process-stop", GTK_ICON_SIZE_MENU);
	gtk_button_set_image (GTK_BUTTON (button), image);

	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (box), 16);
	gtk_container_add (GTK_CONTAINER (content_area), box);
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy),
				  dialog);

	gtk_widget_show_all (dialog);
}
static void wayland_shutdown_response_cb (GtkWidget *dialog, gint response_id)
	{
		int ret;

		if (response_id == GTK_RESPONSE_CANCEL)
		{
			gtk_widget_destroy (dialog);
			return;
		}

		if (response_id == GTK_RESPONSE_OK)
		{
			gtk_widget_destroy (dialog);
			system ("shutdown now");
			/*Try the system shutdown command first.
			 *This will fail if root is logged in
			 *Note that if -f --force was used this would block proper unmounting
			 *if this fails try systemd in case it's installed as it commonly is
			 *FIXME: we also need a logind equivalent for non-systemd users
			 *with session managers running as root
			 */
			system ("systemctl poweroff -i");
			return;
		}

		if (response_id == GTK_RESPONSE_ACCEPT)
		{
			gtk_widget_destroy (dialog);
			system ("reboot now");
			/*Same approach as shutdown command
			 *FIXME: we also need a logind equivalent for non-systemd users
			 *with session managers running as root
			 */
			system ("systemctl reboot -i");
			return;
		}

		if (response_id == GTK_RESPONSE_REJECT)
		{
			gtk_widget_destroy (dialog);
			/*FIXME: we also need a logind equivalent for non-systemd users
			 *with session managers running as root
			 */
			ret = system ("systemctl hibernate -i");
			if (ret != 0)
			{
				GtkWidget *label;
				label = gtk_label_new ("Hibernation not supported on this system" "\n" "\n"
						      "The \"resume = \"  boot command line option must be set to a swap partition or file" "\n"
						      "Swapfile or partition must be large enough to support hibernation" "\n"
						      "System and hardware must support hibernation");
				show_error_dialog (label);
			}
			return;
		}

		if (response_id == GTK_RESPONSE_APPLY)
		{
			gtk_widget_destroy (dialog);
			/*FIXME: we also need a logind equivalent for non-systemd users
			 *with session managers running as root
			 */
			ret = system ("systemctl suspend -i");
			if (ret != 0)
			{
				GtkWidget *label;
				label = gtk_label_new ("Suspend not supported on this system" "\n" "\n"
						      "Hardware and Firmware must support sleep / suspend");
				show_error_dialog (label);
			}

			return;
		}
	}

static GtkWidget*
wayland_shutdown_dialog_add_button (GtkDialog   *dialog,
				    const gchar *button_text,
				    const gchar *icon_name,
				    gint         response_id)
{
	GtkWidget *button;

	button = gtk_button_new_with_mnemonic (button_text);
	gtk_button_set_image (GTK_BUTTON (button), gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON));

	gtk_button_set_use_underline (GTK_BUTTON (button), TRUE);
	gtk_style_context_add_class (gtk_widget_get_style_context (button), "text-button");
	gtk_widget_set_can_default (button, TRUE);
	gtk_widget_show (button);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, response_id);

	return button;
}
#endif
static void
panel_action_shutdown (GtkWidget *widget)
{
#ifdef HAVE_WAYLAND
	GdkDisplay *display = gdk_display_get_default ();
	if (GDK_IS_WAYLAND_DISPLAY (display))
	{
		GtkWidget *dialog, *hbox, *buttonbox, *shutdown_btn, *label;
		GtkStyleContext *context;

		dialog = gtk_dialog_new_with_buttons ("System Shutdown",
						     NULL,
						     GTK_DIALOG_DESTROY_WITH_PARENT,
						     NULL,
						     NULL,
						     NULL);

		/*Window icons in dialogs are currently broken or unsupported
		 *in many wayland compositors but this may not always be so
		 */
		gtk_window_set_icon_name (GTK_WINDOW (dialog), "system-shutdown");
		context = gtk_widget_get_style_context (GTK_WIDGET (dialog));
		gtk_style_context_add_class (context, "logout-dialog");
		context = NULL;

		/*We use the inbuilt gtk response types for simplicity*/
		wayland_shutdown_dialog_add_button (GTK_DIALOG (dialog),
						    _("S_uspend"), "battery",
						    GTK_RESPONSE_APPLY);

		wayland_shutdown_dialog_add_button (GTK_DIALOG (dialog),
						    _("_Hibernate"), "drive-harddisk",
						    GTK_RESPONSE_REJECT);

		wayland_shutdown_dialog_add_button (GTK_DIALOG (dialog),
						    _("_Restart"), "view-refresh",
						    GTK_RESPONSE_ACCEPT);

		wayland_shutdown_dialog_add_button (GTK_DIALOG (dialog),
						    _("_Cancel"), "process-stop",
						    GTK_RESPONSE_CANCEL);

		shutdown_btn = wayland_shutdown_dialog_add_button (GTK_DIALOG (dialog),
								   _("_Shut Down"), "system-shutdown",
								   GTK_RESPONSE_OK);

		g_signal_connect_swapped (dialog, "response",
					  G_CALLBACK (wayland_shutdown_response_cb),
					  dialog);

		hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

		label = gtk_label_new ("Shut this system down now?");
		gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
		gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
		gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 6);
		gtk_container_set_border_width (GTK_CONTAINER (hbox), 16);
		gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
						  hbox);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

		buttonbox = gtk_widget_get_parent (shutdown_btn);
		gtk_widget_set_halign (buttonbox,GTK_ALIGN_CENTER);
		context = gtk_widget_get_style_context (buttonbox);
		gtk_style_context_add_class (context, "linked");
		context = NULL;
		gtk_widget_show_all (dialog);
	}
	else
#endif
	{
		PanelSessionManager *manager;
		manager = panel_session_manager_get ();
		panel_session_manager_request_shutdown (manager);
	}
}

static gboolean
panel_action_shutdown_reboot_is_disabled (void)
{
	PanelSessionManager *manager;

	if (panel_lockdown_get_disable_log_out())
		return TRUE;
#ifdef HAVE_WAYLAND
	GdkDisplay *display = gdk_display_get_default ();
	if (!(panel_lockdown_get_disable_log_out()) && (GDK_IS_WAYLAND_DISPLAY (display)))
		return FALSE;
#endif
	manager = panel_session_manager_get ();

	return (!panel_session_manager_is_shutdown_available (manager));
}

/* Run Application
 */
static void
panel_action_run_program (GtkWidget *widget)
{
	panel_run_dialog_present (gtk_widget_get_screen (widget),
				  gtk_get_current_event_time ());
}

/* Search For Files
 */
static void
panel_action_search (GtkWidget *widget)
{
	GdkScreen *screen;

	screen = gtk_widget_get_screen (widget);
	panel_launch_desktop_file_with_fallback ("mate-search-tool.desktop",
						 "mate-search-tool",
						 screen, NULL);
}

/* Force Quit
 */
static void
panel_action_force_quit (GtkWidget *widget)
{
	GtkWidget *dialog;
	GtkDialogFlags flags;

#ifdef HAVE_X11
	if (GDK_IS_X11_DISPLAY (gtk_widget_get_display (widget))) {
		panel_force_quit (gtk_widget_get_screen (widget),
				  gtk_get_current_event_time ());
		return;
	}
#endif
	flags = GTK_DIALOG_DESTROY_WITH_PARENT;
	dialog = gtk_message_dialog_new (NULL,
					 flags,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 "Force quit only available in X11");
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/* Connect Server
 */
static void
panel_action_connect_server (GtkWidget *widget)
{
	GdkScreen *screen;
	char      *command;
	GError    *error;

	screen = gtk_widget_get_screen (GTK_WIDGET (widget));
	error = NULL;

	if (panel_is_program_in_path ("caja-connect-server"))
		command = g_strdup ("caja-connect-server");
	else if (panel_is_program_in_path ("nautilus-connect-server"))
		command = g_strdup ("nautilus-connect-server");
	else
		command = g_strdup ("nemo-connect-server");

	mate_gdk_spawn_command_line_on_screen (screen, command, &error);
	g_free (command);

	if (error) {
		panel_error_dialog (NULL, screen,
				    "cannot_connect_server",
				    TRUE,
				    _("Could not connect to server"),
				    error->message);
		g_clear_error (&error);
	}
}

typedef struct {
	char                   *icon_name;
	char                   *text;
	char                   *tooltip;
	char                   *help_index;
	char                   *drag_id;
	void                  (*invoke)      (GtkWidget         *widget);
	void                  (*setup_menu)  (PanelActionButton *button);
	void                  (*invoke_menu) (PanelActionButton *button,
					      const char        *callback_name);
	gboolean              (*is_disabled) (void);
} PanelAction;

/* Keep order in sync with PanelActionButtonType
 */
static PanelAction actions [PANEL_ACTION_LAST] = {
	[PANEL_ACTION_NONE] = {
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL
	},
	[PANEL_ACTION_LOCK] = {
		PANEL_ICON_LOCKSCREEN,
		N_("Lock Screen"),
		N_("Protect your computer from unauthorized use"),
		"gospanel-21",
		"ACTION:lock:NEW",
		panel_action_lock_screen,
		panel_action_lock_setup_menu,
		panel_action_lock_invoke_menu,
		panel_action_lock_is_disabled
	},
	[PANEL_ACTION_LOGOUT] = {
		PANEL_ICON_LOGOUT,
		/* when changing one of those two strings, don't forget to
		 * update the ones in panel-menu-items.c (look for
		 * "1" (msgctxt: "panel:showusername")) */
		N_("Log Out..."),
		N_("Log out of this session to log in as a different user"),
		"gospanel-20",
		"ACTION:logout:NEW",
		panel_action_logout, NULL, NULL,
		panel_lockdown_get_disable_log_out
	},
	[PANEL_ACTION_RUN] = {
		PANEL_ICON_RUN,
		N_("Run Application..."),
		N_("Run an application by typing a command or choosing from a list"),
		"gospanel-555",
		"ACTION:run:NEW",
		panel_action_run_program, NULL, NULL,
		panel_lockdown_get_disable_command_line
	},
	[PANEL_ACTION_SEARCH] = {
		PANEL_ICON_SEARCHTOOL,
		N_("Search for Files..."),
		N_("Locate documents and folders on this computer by name or content"),
		"gospanel-554",
		"ACTION:search:NEW",
		panel_action_search, NULL, NULL, NULL
	},
	[PANEL_ACTION_FORCE_QUIT] = {
		PANEL_ICON_FORCE_QUIT,
		N_("Force Quit"),
		N_("Force a misbehaving application to quit"),
		"gospanel-563",
		"ACTION:force-quit:NEW",
		panel_action_force_quit, NULL, NULL,
		panel_lockdown_get_disable_force_quit
	},
	[PANEL_ACTION_CONNECT_SERVER] = {
		PANEL_ICON_REMOTE, /* FIXME icon */
		N_("Connect to Server..."),
		N_("Connect to a remote computer or shared disk"),
		"caja-server-connect",
		"ACTION:connect-server:NEW",
		panel_action_connect_server, NULL, NULL, NULL
	},
	[PANEL_ACTION_SHUTDOWN] = {
		PANEL_ICON_SHUTDOWN,
		N_("Shut Down..."),
		N_("Shut down the computer"),
		"gospanel-20",
		"ACTION:shutdown:NEW",
		panel_action_shutdown, NULL, NULL,
		panel_action_shutdown_reboot_is_disabled
	}
};

gboolean
panel_action_get_is_disabled (PanelActionButtonType type)
{
	g_return_val_if_fail (type > PANEL_ACTION_NONE && type < PANEL_ACTION_LAST, FALSE);

	if (actions [type].is_disabled)
		return actions [type].is_disabled ();

	return FALSE;
}

GCallback
panel_action_get_invoke (PanelActionButtonType type)
{
	g_return_val_if_fail (type > PANEL_ACTION_NONE && type < PANEL_ACTION_LAST, NULL);

	g_assert (actions[type].invoke != NULL);

	return G_CALLBACK (actions[type].invoke);
}

const char* panel_action_get_icon_name(PanelActionButtonType type)
{
	g_return_val_if_fail(type > PANEL_ACTION_NONE && type < PANEL_ACTION_LAST, NULL);

	return actions[type].icon_name;
}

const char* panel_action_get_text(PanelActionButtonType type)
{
	g_return_val_if_fail(type > PANEL_ACTION_NONE && type < PANEL_ACTION_LAST, NULL);

	return _(actions[type].text);
}

const char* panel_action_get_tooltip(PanelActionButtonType type)
{
	g_return_val_if_fail(type > PANEL_ACTION_NONE && type < PANEL_ACTION_LAST, NULL);

	return _(actions[type].tooltip);
}

const char* panel_action_get_drag_id(PanelActionButtonType type)
{
	g_return_val_if_fail(type > PANEL_ACTION_NONE && type < PANEL_ACTION_LAST, NULL);

	return actions[type].drag_id;
}

static void
panel_action_button_update_sensitivity (PanelActionButton *button)
{
	if (actions [button->priv->type].is_disabled)
		button_widget_set_activatable (BUTTON_WIDGET (button),
					       !actions [button->priv->type].is_disabled ());
}

static void
panel_action_button_finalize (GObject *object)
{
	PanelActionButton *button = PANEL_ACTION_BUTTON (object);

	if (button->priv->settings) {
		g_signal_handlers_disconnect_by_func (button->priv->settings,
		                                      G_CALLBACK (panel_action_button_type_changed),
		                                      button);
		g_clear_object (&button->priv->settings);
	}

	button->priv->info = NULL;
	button->priv->type = PANEL_ACTION_NONE;

	panel_lockdown_notify_remove (G_CALLBACK (panel_action_button_update_sensitivity),
				      button);

	G_OBJECT_CLASS (panel_action_button_parent_class)->finalize (object);
}

static void
panel_action_button_get_property (GObject    *object,
				  guint       prop_id,
				  GValue     *value,
				  GParamSpec *pspec)
{
	PanelActionButton *button;

	g_return_if_fail (PANEL_IS_ACTION_BUTTON (object));

	button = PANEL_ACTION_BUTTON (object);

	switch (prop_id) {
	case PROP_ACTION_TYPE:
		g_value_set_enum (value, button->priv->type);
		break;
	case PROP_DND_ENABLED:
		g_value_set_boolean (value, button->priv->dnd_enabled);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
panel_action_button_set_property (GObject      *object,
				  guint         prop_id,
				  const GValue *value,
				  GParamSpec   *pspec)
{
	PanelActionButton *button;

	g_return_if_fail (PANEL_IS_ACTION_BUTTON (object));

	button = PANEL_ACTION_BUTTON (object);

	switch (prop_id) {
	case PROP_ACTION_TYPE:
		panel_action_button_set_type (button,
					      g_value_get_enum (value));
		break;
	case PROP_DND_ENABLED:
		panel_action_button_set_dnd_enabled (button,
						     g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
panel_action_button_drag_data_get (GtkWidget          *widget,
				   GdkDragContext     *context,
				   GtkSelectionData   *selection_data,
				   guint               info,
				   guint               time)
{
	PanelActionButton *button;
	char              *drag_data;

	g_return_if_fail (PANEL_IS_ACTION_BUTTON (widget));

	button = PANEL_ACTION_BUTTON (widget);

	drag_data = g_strdup_printf ("ACTION:%s:%d",
				     get_action_type_name (button->priv->type),
				     panel_find_applet_index (widget));

	gtk_selection_data_set (
		selection_data, gtk_selection_data_get_target (selection_data),
		8, (guchar *) drag_data, strlen (drag_data));

	g_free (drag_data);
}

static void
panel_action_button_clicked (GtkButton *gtk_button)
{
	PanelActionButton *button;

	g_return_if_fail (PANEL_IS_ACTION_BUTTON (gtk_button));

	button = PANEL_ACTION_BUTTON (gtk_button);

	g_return_if_fail (button->priv->type > PANEL_ACTION_NONE);
	g_return_if_fail (button->priv->type < PANEL_ACTION_LAST);

	if (panel_global_config_get_drawer_auto_close ()) {
		PanelToplevel *toplevel;

		toplevel = PANEL_WIDGET (gtk_widget_get_parent (GTK_WIDGET (button)))->toplevel;

		if (panel_toplevel_get_is_attached (toplevel))
			panel_toplevel_hide (toplevel, FALSE, -1);
	}

	if (actions [button->priv->type].invoke)
		actions [button->priv->type].invoke (GTK_WIDGET (button));
}

static void
panel_action_button_class_init (PanelActionButtonClass *klass)
{
	GObjectClass   *gobject_class = (GObjectClass *) klass;
	GtkWidgetClass *widget_class  = (GtkWidgetClass *) klass;
	GtkButtonClass *button_class  = (GtkButtonClass *) klass;

	gobject_class->finalize     = panel_action_button_finalize;
	gobject_class->get_property = panel_action_button_get_property;
	gobject_class->set_property = panel_action_button_set_property;

	widget_class->drag_data_get = panel_action_button_drag_data_get;

	button_class->clicked       = panel_action_button_clicked;

	g_object_class_install_property (
			gobject_class,
			PROP_ACTION_TYPE,
			g_param_spec_enum ("action-type",
					   "Action Type",
					   "The type of action this button implements",
					   PANEL_TYPE_ACTION_BUTTON_TYPE,
					   PANEL_ORIENTATION_TOP,
					   G_PARAM_READWRITE));

	g_object_class_install_property (
			gobject_class,
			PROP_DND_ENABLED,
			g_param_spec_boolean ("dnd-enabled",
					      "Drag and drop enabled",
					      "Whether or not drag and drop is enabled on the widget",
					      TRUE,
					      G_PARAM_READWRITE));
}

static void
panel_action_button_init (PanelActionButton *button)
{
	button->priv = panel_action_button_get_instance_private (button);

	button->priv->type = PANEL_ACTION_NONE;
	button->priv->info = NULL;

	button->priv->dnd_enabled  = FALSE;
}

void
panel_action_button_set_type (PanelActionButton     *button,
			      PanelActionButtonType  type)
{
	if (!type)
		return;

	g_return_if_fail (type > PANEL_ACTION_NONE && type < PANEL_ACTION_LAST);

	if (type == button->priv->type)
		return;

	button->priv->type = type;

	if (actions [type].icon_name != NULL)
		button_widget_set_icon_name (BUTTON_WIDGET (button), actions [type].icon_name);

	panel_util_set_tooltip_text (GTK_WIDGET (button),
				     _(actions [type].tooltip));
	panel_a11y_set_atk_name_desc (GTK_WIDGET (button), _(actions [type].tooltip), NULL);

	panel_action_button_update_sensitivity (button);
}

static void
panel_action_button_type_changed (GSettings       *settings,
								  gchar           *key,
								  PanelActionButton *button)
{
	int         type;
	g_return_if_fail (PANEL_IS_ACTION_BUTTON (button));
	type = g_settings_get_enum (settings, key);
	panel_action_button_set_type (button, type);
}

static void
panel_action_button_connect_to_gsettings (PanelActionButton *button)
{
	gchar *settings_path;
	gchar *signal_name;

	settings_path = g_strdup_printf (PANEL_OBJECT_PATH "%s/", button->priv->info->id);
	button->priv->settings = g_settings_new_with_path (PANEL_OBJECT_SCHEMA, settings_path);

	signal_name = g_strdup_printf ("changed::%s", PANEL_OBJECT_ACTION_TYPE_KEY);
	g_signal_connect (button->priv->settings,
					  signal_name,
					  G_CALLBACK (panel_action_button_type_changed),
					  button);

	g_free (signal_name);
	g_free (settings_path);

	panel_lockdown_notify_add (G_CALLBACK (panel_action_button_update_sensitivity),
				   button);
}

static void
panel_action_button_style_updated (PanelActionButton *button)
{
	if (actions [button->priv->type].icon_name != NULL)
		button_widget_set_icon_name (BUTTON_WIDGET (button), actions [button->priv->type].icon_name);
}

static void
panel_action_button_load (PanelActionButtonType  type,
			  PanelWidget           *panel,
			  gboolean               locked,
			  int                    position,
			  gboolean               exactpos,
			  const char            *id)
{
	PanelActionButton *button;

	g_return_if_fail (panel != NULL);

	button = g_object_new (PANEL_TYPE_ACTION_BUTTON, "action-type", type, NULL);

	button->priv->info = mate_panel_applet_register (GTK_WIDGET (button),
						    NULL, NULL,
						    panel, locked, position,
						    exactpos, PANEL_OBJECT_ACTION, id);
	if (!button->priv->info) {
		gtk_widget_destroy (GTK_WIDGET (button));
		return;
	}

	mate_panel_applet_add_callback (button->priv->info,
				   "help",
				   "help-browser",
				   _("_Help"),
				   NULL);

	panel_widget_set_applet_expandable (panel, GTK_WIDGET (button), FALSE, TRUE);
	panel_widget_set_applet_size_constrained (panel, GTK_WIDGET (button), TRUE);

	if (actions [button->priv->type].setup_menu)
		actions [button->priv->type].setup_menu (button);

	panel_action_button_connect_to_gsettings (button);

	g_signal_connect (button, "style-updated",
			  G_CALLBACK (panel_action_button_style_updated), NULL);
}

void
panel_action_button_create (PanelToplevel         *toplevel,
			    int                    position,
			    PanelActionButtonType  type)
{
	GSettings   *settings;
	char        *id;
	char        *path;

	id = panel_profile_prepare_object (PANEL_OBJECT_ACTION, toplevel, position, FALSE);

	path = g_strdup_printf (PANEL_OBJECT_PATH "%s/", id);
	settings = g_settings_new_with_path (PANEL_OBJECT_SCHEMA, path);

	g_settings_set_enum (settings,
						 PANEL_OBJECT_ACTION_TYPE_KEY,
						 type);

	panel_profile_add_to_list (PANEL_GSETTINGS_OBJECTS, id);

	g_free (id);
	g_free (path);
	g_object_unref (settings);
}

void
panel_action_button_load_from_gsettings (PanelWidget *panel,
				     gboolean     locked,
				     int          position,
				     gboolean     exactpos,
				     const char  *id)
{
	GSettings             *settings;
	PanelActionButtonType  type;
	char                  *path;

	path = g_strdup_printf (PANEL_OBJECT_PATH "%s/", id);
	settings = g_settings_new_with_path (PANEL_OBJECT_SCHEMA, path);

	type = g_settings_get_enum (settings, PANEL_OBJECT_ACTION_TYPE_KEY);

	g_free (path);
	g_object_unref (settings);

	panel_action_button_load (type, panel, locked,
					  position, exactpos, id);
}

void
panel_action_button_invoke_menu (PanelActionButton *button,
				 const char        *callback_name)
{
	g_return_if_fail (PANEL_IS_ACTION_BUTTON (button));
	g_return_if_fail (callback_name != NULL);
	g_return_if_fail (button->priv->type > PANEL_ACTION_NONE &&
			  button->priv->type < PANEL_ACTION_LAST);

	if (!strcmp (callback_name, "help")) {
		GdkScreen *screen;

		if (!actions [button->priv->type].help_index)
			return;

		screen = gtk_widget_get_screen (GTK_WIDGET (button));

		panel_show_help (screen, "mate-user-guide",
				 actions [button->priv->type].help_index, NULL);

		return;
	}

	if (actions [button->priv->type].invoke_menu)
		actions [button->priv->type].invoke_menu (button, callback_name);
}

gboolean
panel_action_button_load_from_drag (PanelToplevel *toplevel,
				    int            position,
				    const char    *drag_string,
				    int           *old_applet_idx)
{
	PanelActionButtonType   type = PANEL_ACTION_NONE;
	gboolean                retval = FALSE;
	char                  **elements;

	if (strncmp (drag_string, "ACTION:", strlen ("ACTION:")))
		return retval;

	elements = g_strsplit (drag_string, ":", 0);

	g_assert (elements != NULL);

	if (!elements [1] || !elements [2]) {
		g_strfreev (elements);
		return retval;
	}

	if (!get_action_type_id (elements [1], (gpointer) &type)) {
		g_strfreev (elements);
		return retval;
	}

	g_return_val_if_fail (type > PANEL_ACTION_NONE && type < PANEL_ACTION_LAST, FALSE);

	if (strcmp (elements [2], "NEW")) {
		*old_applet_idx = strtol (elements [2], NULL, 10);
		retval = TRUE; /* Remove the old applet */
	}

	g_strfreev (elements);

	panel_action_button_create (toplevel, position, type);

	return retval;
}

void
panel_action_button_set_dnd_enabled (PanelActionButton *button,
				     gboolean           enabled)
{
	g_return_if_fail (PANEL_IS_ACTION_BUTTON (button));

	if (!button->priv->type)
		return; /* wait until we know what type it is */

	enabled = enabled != FALSE;

	if (button->priv->dnd_enabled == enabled)
		return;

	if (enabled) {
		static GtkTargetEntry dnd_targets [] = {
			{ "application/x-mate-panel-applet-internal", 0, 0 }
		};

		gtk_widget_set_has_window (GTK_WIDGET (button), TRUE);
		gtk_drag_source_set (GTK_WIDGET (button), GDK_BUTTON1_MASK,
				     dnd_targets, 1,
				     GDK_ACTION_COPY | GDK_ACTION_MOVE);
		if (actions [button->priv->type].icon_name != NULL)
			gtk_drag_source_set_icon_name (GTK_WIDGET (button),
						       actions [button->priv->type].icon_name);
		gtk_widget_set_has_window (GTK_WIDGET (button), FALSE);
	} else
		gtk_drag_source_unset (GTK_WIDGET (button));

	button->priv->dnd_enabled = (enabled != FALSE);

	g_object_notify (G_OBJECT (button), "dnd-enabled");
}
