/*
 * Messaging Menu Integration - Integration with Unity's messaging menu
 * Copyright (C) 2013 Ankit Vani <a@nevitus.org>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 *
 */

#include "internal.h"
#include "debug.h"
#include "gtkplugin.h"
#include "version.h"

#include <messaging-menu.h>

static MessagingMenuApp *mmapp;

static gboolean
plugin_load(PurplePlugin *plugin)
{
	mmapp = messaging_menu_app_new("pidgin.desktop");
	messaging_menu_app_register(mmapp);
	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	messaging_menu_app_unregister(mmapp);
	g_object_unref(mmapp);
	return TRUE;
}

static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,                           /**< type           */
	PIDGIN_PLUGIN_TYPE,                               /**< ui_requirement */
	0,                                                /**< flags          */
	NULL,                                             /**< dependencies   */
	PURPLE_PRIORITY_DEFAULT,                          /**< priority       */

	"ankitkv-messagingmenu",                          /**< id             */
	"Messaging Menu Integration",                     /**< name           */
	"0.1",                                            /**< version        */
	                                                  /**  summary        */
	"Provides integration with Unity's messaging menu.",
	                                                  /**  description    */
	"Provides integration with Unity's messaging menu.",
	                                                  /**< author         */
	"Ankit Vani <a@nevitus.org>",
	"http://nevitus.com",                             /**< homepage       */

	plugin_load,                                      /**< load           */
	plugin_unload,                                    /**< unload         */
	NULL,                                             /**< destroy        */

	NULL,                                             /**< ui_info        */
	NULL,                                             /**< extra_info     */
	NULL,
	NULL,

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};

static void
init_plugin(PurplePlugin *plugin)
{
}

PURPLE_INIT_PLUGIN(notify, init_plugin, info)
