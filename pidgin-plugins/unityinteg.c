/*
 * Unity Integration - Integration with Unity's messaging menu and launcher
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
#include "gtkconv.h"

#include <unity.h>
#include <messaging-menu.h>

static MessagingMenuApp *mmapp;

static int
unnotify_cb(GtkWidget *widget, gpointer data, PurpleConversation *conv)
{
	return 0;
}

static gboolean
message_displayed_cb(PurpleAccount *account, const char *who, char *message,
                     PurpleConversation *conv, PurpleMessageFlags flags)
{
	return FALSE;
}

static void
conv_switched(PurpleConversation *conv)
{
}

static void
im_sent_im(PurpleAccount *account, const char *receiver, const char *message)
{
}

static void
chat_sent_im(PurpleAccount *account, const char *message, int id)
{
}

static void
conv_created(PurpleConversation *conv)
{
}

static void
deleting_conv(PurpleConversation *conv)
{
}

static int
attach_signals(PurpleConversation *conv)
{
	PidginConversation *gtkconv = NULL;
	GSList *webview_ids = NULL, *entry_ids = NULL;
	guint id;

	gtkconv = PIDGIN_CONVERSATION(conv);

	id = g_signal_connect(G_OBJECT(gtkconv->entry), "focus-in-event",
	                      G_CALLBACK(unnotify_cb), conv);
	entry_ids = g_slist_append(entry_ids, GUINT_TO_POINTER(id));

	id = g_signal_connect(G_OBJECT(gtkconv->webview), "focus-in-event",
	                      G_CALLBACK(unnotify_cb), conv);
	webview_ids = g_slist_append(webview_ids, GUINT_TO_POINTER(id));

	id = g_signal_connect(G_OBJECT(gtkconv->entry), "button-press-event",
	                      G_CALLBACK(unnotify_cb), conv);
	entry_ids = g_slist_append(entry_ids, GUINT_TO_POINTER(id));

	id = g_signal_connect(G_OBJECT(gtkconv->webview), "button-press-event",
	                      G_CALLBACK(unnotify_cb), conv);
	webview_ids = g_slist_append(webview_ids, GUINT_TO_POINTER(id));

	id = g_signal_connect(G_OBJECT(gtkconv->entry), "key-press-event",
	                      G_CALLBACK(unnotify_cb), conv);
	entry_ids = g_slist_append(entry_ids, GUINT_TO_POINTER(id));

	purple_conversation_set_data(conv, "messagingmenu-webview-signals", webview_ids);
	purple_conversation_set_data(conv, "messagingmenu-entry-signals", entry_ids);

	return 0;
}

static void
detach_signals(PurpleConversation *conv)
{
	PidginConversation *gtkconv = NULL;
	GSList *ids = NULL, *l;

	gtkconv = PIDGIN_CONVERSATION(conv);
	if (!gtkconv)
		return;

	ids = purple_conversation_get_data(conv, "messagingmenu-webview-signals");
	for (l = ids; l != NULL; l = l->next)
		g_signal_handler_disconnect(gtkconv->webview, GPOINTER_TO_INT(l->data));
	g_slist_free(ids);

	ids = purple_conversation_get_data(conv, "messagingmenu-entry-signals");
	for (l = ids; l != NULL; l = l->next)
		g_signal_handler_disconnect(gtkconv->entry, GPOINTER_TO_INT(l->data));
	g_slist_free(ids);

	purple_conversation_set_data(conv, "messagingmenu-message-count", GINT_TO_POINTER(0));

	purple_conversation_set_data(conv, "messagingmenu-webview-signals", NULL);
	purple_conversation_set_data(conv, "messagingmenu-entry-signals", NULL);
}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	GList *convs = purple_get_conversations();
	void *conv_handle = purple_conversations_get_handle();
	void *gtk_conv_handle = pidgin_conversations_get_handle();

	mmapp = messaging_menu_app_new("pidgin.desktop");
	messaging_menu_app_register(mmapp);

	purple_signal_connect(gtk_conv_handle, "displayed-im-msg", plugin,
	                    PURPLE_CALLBACK(message_displayed_cb), NULL);
	purple_signal_connect(gtk_conv_handle, "displayed-chat-msg", plugin,
	                    PURPLE_CALLBACK(message_displayed_cb), NULL);
	purple_signal_connect(gtk_conv_handle, "conversation-switched", plugin,
	                    PURPLE_CALLBACK(conv_switched), NULL);
	purple_signal_connect(conv_handle, "sent-im-msg", plugin,
	                    PURPLE_CALLBACK(im_sent_im), NULL);
	purple_signal_connect(conv_handle, "sent-chat-msg", plugin,
	                    PURPLE_CALLBACK(chat_sent_im), NULL);
	purple_signal_connect(conv_handle, "conversation-created", plugin,
	                    PURPLE_CALLBACK(conv_created), NULL);
	purple_signal_connect(conv_handle, "deleting-conversation", plugin,
	                    PURPLE_CALLBACK(deleting_conv), NULL);

	while (convs) {
		PurpleConversation *conv = (PurpleConversation *)convs->data;
		attach_signals(conv);
		convs = convs->next;
	}

	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	GList *convs = purple_get_conversations();

	while (convs) {
		PurpleConversation *conv = (PurpleConversation *)convs->data;
		detach_signals(conv);
		convs = convs->next;
	}
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

	"ankitkv-unityinteg",                          /**< id             */
	"Unity Integration",                     /**< name           */
	"0.1",                                            /**< version        */
	                                                  /**  summary        */
	"Provides integration with Unity.",
	                                                  /**  description    */
	"Provides integration with Unity's messaging menu "
	"and launcher.",
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