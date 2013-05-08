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

/* NOTE:
 * Ensure pidgin.desktop has X-MessagingMenu-UsesChatSection=true
 */

#include "internal.h"
#include "debug.h"
#include "version.h"
#include "account.h"
#include "savedstatuses.h"

#include "gtkplugin.h"
#include "gtkconv.h"

#include <unity.h>
#include <messaging-menu.h>

static MessagingMenuApp *mmapp;
UnityLauncherEntry *launcher = NULL;
static GSList *unity_ids = NULL;

static int attach_signals(PurpleConversation *conv);
static void detach_signals(PurpleConversation *conv);

static void
update_launcher(PidginWindow *purplewin)
{
	guint count = 0;
	GList *convs = NULL, *l;

	for (convs = purplewin->gtkconvs; convs != NULL; convs = convs->next) {
		PidginConversation *conv = convs->data;
		for (l = conv->convs; l != NULL; l = l->next) {
			count += GPOINTER_TO_INT(purple_conversation_get_data(l->data,
			                         "unity-message-count"));
		}
	}

	if (launcher != NULL) {
		if (count > 0) {
			unity_launcher_entry_set_count(launcher, count);
			unity_launcher_entry_set_count_visible(launcher, TRUE);
		} else {
			unity_launcher_entry_set_count(launcher, count);
			unity_launcher_entry_set_count_visible(launcher, FALSE);
		}
	}
}

static gchar *
conversation_id(PurpleConversation *conv)
{
	return g_strconcat ("todo", NULL); // TODO: concat all strings
}


static void
messaging_menu_add_source(const gchar *source_id, gint count)
{
	// TODO: add source
}

static void
messaging_menu_remove_source(const gchar *source_id)
{
	// TODO: remove source
}

static int
notify(PurpleConversation *conv)
{
	gint count;
	gboolean has_focus;
	PidginWindow *purplewin = NULL;

	if (conv == NULL || PIDGIN_CONVERSATION(conv) == NULL)
		return 0;

	purplewin = PIDGIN_CONVERSATION(conv)->win;
	g_object_get(G_OBJECT(purplewin->window), "has-toplevel-focus", &has_focus,
	             NULL);

	if (!has_focus) {
		count = GPOINTER_TO_INT(purple_conversation_get_data(conv,
		                        "unity-message-count"));
		count++;
		purple_conversation_set_data(conv, "unity-message-count",
		                             GINT_TO_POINTER(count));
		update_launcher(purplewin);
		messaging_menu_add_source(conversation_id(conv), count);
	}

	return 0;
}

static int
unnotify_cb(GtkWidget *widget, gpointer data, PurpleConversation *conv)
{
	PidginWindow *purplewin = NULL;

	if (GPOINTER_TO_INT(purple_conversation_get_data(conv, "unity-message-count")) != 0) {
		purplewin = PIDGIN_CONVERSATION(conv)->win;
		purple_conversation_set_data(conv, "unity-message-count",
		                             GINT_TO_POINTER(0));
		update_launcher(purplewin);
		messaging_menu_remove_source(conversation_id(conv));
	}

	return 0;
}

static gboolean
message_displayed_cb(PurpleAccount *account, const char *who, char *message,
                     PurpleConversation *conv, PurpleMessageFlags flags)
{
	if ((flags & PURPLE_MESSAGE_RECV) && !(flags & PURPLE_MESSAGE_DELAYED))
		notify(conv);

	return FALSE;
}

static void
im_sent_im(PurpleAccount *account, const char *receiver, const char *message)
{
	PurpleConversation *conv = NULL;
	PidginWindow *purplewin = PIDGIN_CONVERSATION(conv)->win;
	conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, receiver,
	                                             account);
	purple_conversation_set_data(conv, "unity-message-count",
	                             GINT_TO_POINTER(0));
	update_launcher(purplewin);
	messaging_menu_remove_source(conversation_id(conv));
}

static void
chat_sent_im(PurpleAccount *account, const char *message, int id)
{
	PurpleConversation *conv = NULL;
	PidginWindow *purplewin = PIDGIN_CONVERSATION(conv)->win;
	conv = purple_find_chat(purple_account_get_connection(account), id);
	purple_conversation_set_data(conv, "unity-message-count",
	                             GINT_TO_POINTER(0));
	update_launcher(purplewin);
	messaging_menu_remove_source(conversation_id(conv));
}

static void
conv_created(PurpleConversation *conv)
{
	purple_conversation_set_data(conv, "unity-message-count",
	                             GINT_TO_POINTER(0));
	attach_signals(conv);
}

static void
deleting_conv(PurpleConversation *conv)
{
	PidginWindow *purplewin = PIDGIN_CONVERSATION(conv)->win;
	detach_signals(conv);
	purple_conversation_set_data(conv, "unity-message-count",
	                             GINT_TO_POINTER(0));
	update_launcher(purplewin);
	messaging_menu_remove_source(conversation_id(conv));
}

static void
message_source_activated(MessagingMenuApp *app, const gchar *id,
                         gpointer user_data)
{
	// TODO: handle source
}

static PurpleSavedStatus *
create_transient_status(PurpleStatusPrimitive primitive, PurpleStatusType *status_type)
{
	PurpleSavedStatus *saved_status = purple_savedstatus_new(NULL, primitive);

	if(status_type != NULL) {
		GList *tmp, *active_accts = purple_accounts_get_all_active();
		for (tmp = active_accts; tmp != NULL; tmp = tmp->next) {
			purple_savedstatus_set_substatus(saved_status,
				(PurpleAccount*) tmp->data, status_type, NULL);
		}
		g_list_free(active_accts);
	}

	return saved_status;
}

static void
status_changed_cb(PurpleSavedStatus *saved_status)
{
	MessagingMenuStatus status = MESSAGING_MENU_STATUS_AVAILABLE;

	switch (purple_savedstatus_get_type(saved_status)) {
	case PURPLE_STATUS_AVAILABLE:
	case PURPLE_STATUS_MOOD:
	case PURPLE_STATUS_TUNE:
	case PURPLE_STATUS_UNSET:
		status = MESSAGING_MENU_STATUS_AVAILABLE;
		break;

	case PURPLE_STATUS_AWAY:
	case PURPLE_STATUS_EXTENDED_AWAY:
		status = MESSAGING_MENU_STATUS_AWAY;
		break;

	case PURPLE_STATUS_INVISIBLE:
		status = MESSAGING_MENU_STATUS_INVISIBLE;
		break;

	case PURPLE_STATUS_MOBILE:
	case PURPLE_STATUS_OFFLINE:
		status = MESSAGING_MENU_STATUS_OFFLINE;
		break;

	case PURPLE_STATUS_UNAVAILABLE:
		status = MESSAGING_MENU_STATUS_BUSY;
		break;

	default:
		g_assert_not_reached ();
	}
	messaging_menu_app_set_status(mmapp, status);
}

static void
messaging_menu_status_changed(MessagingMenuApp *mmapp,
                              MessagingMenuStatus mm_status, gpointer user_data)
{
	PurpleSavedStatus *saved_status;
	PurpleStatusPrimitive primitive = PURPLE_STATUS_UNSET;

	switch (mm_status) {
	case MESSAGING_MENU_STATUS_AVAILABLE:
		primitive = PURPLE_STATUS_AVAILABLE;
		break;

	case MESSAGING_MENU_STATUS_AWAY:
		primitive = PURPLE_STATUS_AWAY;
		break;

	case MESSAGING_MENU_STATUS_BUSY:
		primitive = PURPLE_STATUS_UNAVAILABLE;
		break;

	case MESSAGING_MENU_STATUS_INVISIBLE:
		primitive = PURPLE_STATUS_INVISIBLE;
		break;

	case MESSAGING_MENU_STATUS_OFFLINE:
		primitive = PURPLE_STATUS_OFFLINE;
		break;

	default:
		g_assert_not_reached ();
	}

	saved_status = purple_savedstatus_find_transient_by_type_and_message(primitive, NULL);
	if (saved_status == NULL)
		saved_status = create_transient_status(primitive, NULL);
	purple_savedstatus_activate(saved_status);
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

	purple_conversation_set_data(conv, "unity-webview-signals", webview_ids);
	purple_conversation_set_data(conv, "unity-entry-signals", entry_ids);

	return 0;
}

static void
detach_signals(PurpleConversation *conv)
{
	PidginConversation *gtkconv = NULL;
	GSList *ids = NULL, *l;
	gtkconv = PIDGIN_CONVERSATION(conv);

	ids = purple_conversation_get_data(conv, "unity-webview-signals");
	for (l = ids; l != NULL; l = l->next)
		g_signal_handler_disconnect(gtkconv->webview, GPOINTER_TO_INT(l->data));
	g_slist_free(ids);

	ids = purple_conversation_get_data(conv, "unity-entry-signals");
	for (l = ids; l != NULL; l = l->next)
		g_signal_handler_disconnect(gtkconv->entry, GPOINTER_TO_INT(l->data));
	g_slist_free(ids);

	purple_conversation_set_data(conv, "unity-message-count",
	                             GINT_TO_POINTER(0));

	purple_conversation_set_data(conv, "unity-webview-signals", NULL);
	purple_conversation_set_data(conv, "unity-entry-signals", NULL);
}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	guint id;
	GList *convs = purple_get_conversations();
	PurpleSavedStatus *saved_status;
	void *conv_handle = purple_conversations_get_handle();
	void *gtk_conv_handle = pidgin_conversations_get_handle();
	void *savedstat_handle = purple_savedstatuses_get_handle();

	mmapp = messaging_menu_app_new("pidgin.desktop");
	messaging_menu_app_register(mmapp);

	id = g_signal_connect(mmapp, "activate-source",
	                       G_CALLBACK (message_source_activated), NULL);
	unity_ids = g_slist_append(unity_ids, GUINT_TO_POINTER(id));
	id = g_signal_connect(mmapp, "status-changed",
	                       G_CALLBACK (messaging_menu_status_changed), NULL);
	unity_ids = g_slist_append(unity_ids, GUINT_TO_POINTER(id));

	saved_status = purple_savedstatus_get_current();
	status_changed_cb(saved_status);

	purple_signal_connect(savedstat_handle, "savedstatus-changed", plugin,
	                    PURPLE_CALLBACK(status_changed_cb), NULL);

	launcher = unity_launcher_entry_get_for_desktop_id("pidgin.desktop");

	purple_signal_connect(gtk_conv_handle, "displayed-im-msg", plugin,
	                    PURPLE_CALLBACK(message_displayed_cb), NULL);
	purple_signal_connect(gtk_conv_handle, "displayed-chat-msg", plugin,
	                    PURPLE_CALLBACK(message_displayed_cb), NULL);
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
	GSList *l;
	GList *convs = purple_get_conversations();
	while (convs) {
		PurpleConversation *conv = (PurpleConversation *)convs->data;
		detach_signals(conv);
		convs = convs->next;
	}

	for (l = unity_ids; l != NULL; l = l->next)
		g_signal_handler_disconnect(mmapp, GPOINTER_TO_INT(l->data));
	g_slist_free(unity_ids);

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

	"ankitkv-unityinteg",                             /**< id             */
	"Unity Integration",                              /**< name           */
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

PURPLE_INIT_PLUGIN(unityinteg, init_plugin, info)
