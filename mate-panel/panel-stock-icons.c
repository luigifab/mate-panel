/*
 * panel-stock-icons.c panel stock icons registration
 *
 * Copyright (C) 2002 Sun Microsystems, Inc.
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

#include "panel-stock-icons.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "panel-icon-names.h"
#include "panel-schemas.h"

static GtkIconSize panel_menu_icon_size = 0;
static GtkIconSize panel_menu_bar_icon_size = 0;
static GtkIconSize panel_add_to_icon_size = 0;

GtkIconSize
panel_menu_icon_get_size (void)
{
	return panel_menu_icon_size;
}

GtkIconSize
panel_menu_bar_icon_get_size (void)
{
	return panel_menu_bar_icon_size;
}

GtkIconSize panel_add_to_icon_get_size(void)
{
	return panel_add_to_icon_size;
}


void
panel_init_stock_icons_and_items (void)
{
	GSettings      *settings;
	gint		icon_size;

	settings = g_settings_new (PANEL_MENU_BAR_SCHEMA);

	icon_size = g_settings_get_enum (settings, "item-icon-size");
	if (icon_size <= 0) {
		panel_menu_icon_size = gtk_icon_size_register ("panel-menu",
							       PANEL_DEFAULT_MENU_ICON_SIZE,
							       PANEL_DEFAULT_MENU_ICON_SIZE);
	} else {
		/* underscores to prevent themes from altering these settings */
		panel_menu_icon_size = gtk_icon_size_register ("__panel-menu",
							       icon_size,
							       icon_size);
	}

	icon_size = g_settings_get_enum (settings, "icon-size");
	if (icon_size <= 0) {
		panel_menu_bar_icon_size = gtk_icon_size_register ("panel-foobar",
								   PANEL_DEFAULT_MENU_BAR_ICON_SIZE,
								   PANEL_DEFAULT_MENU_BAR_ICON_SIZE);
	} else {
		panel_menu_bar_icon_size = gtk_icon_size_register ("__panel-foobar",
								   icon_size,
								   icon_size);
	}

	panel_add_to_icon_size = gtk_icon_size_register ("panel-add-to",
							 PANEL_ADD_TO_DEFAULT_ICON_SIZE,
							 PANEL_ADD_TO_DEFAULT_ICON_SIZE);


	g_object_unref (settings);
}
