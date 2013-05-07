/*
 * ColorNicks Logger - Store unique colored nicks in HTML logs
 * Based on html_logger in libpurple/log.c
 * Copyright (C) 2013 Ankit Vani <a@nevitus.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1301, USA.
 */

#include "internal.h"
#include "debug.h"
#include "plugin.h"
#include "version.h"
#include "gtkconv.h"

#define LUMINANCE(c) (float)((0.3*(c.red))+(0.59*(c.green))+(0.11*(c.blue)))

static gsize colornicks_logger_write(PurpleLog *log, PurpleMessageFlags type,
							  const char *from, time_t time, const char *message);
static void colornicks_logger_finalize(PurpleLog *log);
static GList *colornicks_logger_list(PurpleLogType type, const char *sn, PurpleAccount *account);
static GList *colornicks_logger_list_syslog(PurpleAccount *account);
static char *colornicks_logger_read(PurpleLog *log, PurpleLogReadFlags *flags);
static int colornicks_logger_total_size(PurpleLogType type, const char *name, PurpleAccount *account);

static PurpleLogLogger *colornicks_logger;

static char *
get_nick_color(PidginConversation *gtkconv, const char *name)
{
	static GdkColor col;
	GtkStyle *style;
	float scale;

	g_return_val_if_fail(name != NULL && gtkconv != NULL && gtkconv->nick_colors != NULL, NULL);

	style = gtk_widget_get_style(gtkconv->webview);
	col = g_array_index(gtkconv->nick_colors, GdkColor, g_str_hash(name) % gtkconv->nick_colors->len);
	scale = ((1-(LUMINANCE(style->base[GTK_STATE_NORMAL]) / LUMINANCE(style->white))) *
		       (LUMINANCE(style->white)/MAX(MAX(col.red, col.blue), col.green)));

	/* The colors are chosen to look fine on white; we should never have to darken */
	if (scale > 1) {
		col.red   *= scale;
		col.green *= scale;
		col.blue  *= scale;
	}

	return g_strdup_printf("#%02x%02x%02x", (col.red >> 8), (col.green >> 8), (col.blue >> 8));
}

/* NOTE: This can return msg (which you may or may not want to g_free())
 * NOTE: or a newly allocated string which you MUST g_free(). */
static char *
convert_image_tags(const PurpleLog *log, const char *msg)
{
	const char *tmp;
	const char *start;
	const char *end;
	GData *attributes;
	GString *newmsg = NULL;

	tmp = msg;

	while (purple_markup_find_tag("img", tmp, &start, &end, &attributes)) {
		int imgid = 0;
		char *idstr = NULL;

		if (newmsg == NULL)
			newmsg = g_string_new("");

		/* copy any text before the img tag */
		if (tmp < start)
			g_string_append_len(newmsg, tmp, start - tmp);

		if ((idstr = g_datalist_get_data(&attributes, "id")) != NULL)
			imgid = atoi(idstr);

		if (imgid != 0)
		{
			FILE *image_file;
			char *dir;
			PurpleStoredImage *image;
			gconstpointer image_data;
			char *new_filename = NULL;
			char *path = NULL;
			size_t image_byte_count;

			image = purple_imgstore_find_by_id(imgid);
			if (image == NULL)
			{
				/* This should never happen. */
				/* This *does* happen for failed Direct-IMs -DAA */
				g_string_free(newmsg, TRUE);
				g_return_val_if_reached((char *)msg);
			}

			image_data       = purple_imgstore_get_data(image);
			image_byte_count = purple_imgstore_get_size(image);
			dir              = purple_log_get_log_dir(log->type, log->name, log->account);
			new_filename     = purple_util_get_image_filename(image_data, image_byte_count);

			path = g_build_filename(dir, new_filename, NULL);

			/* Only save unique files. */
			if (!g_file_test(path, G_FILE_TEST_EXISTS))
			{
				if ((image_file = g_fopen(path, "wb")) != NULL)
				{
					if (!fwrite(image_data, image_byte_count, 1, image_file))
					{
						purple_debug_error("log", "Error writing %s: %s\n",
						                   path, g_strerror(errno));
						fclose(image_file);

						/* Attempt to not leave half-written files around. */
						if (g_unlink(path)) {
							purple_debug_error("log", "Error deleting partial "
									"file %s: %s\n", path, g_strerror(errno));
						}
					}
					else
					{
						purple_debug_info("log", "Wrote image file: %s\n", path);
						fclose(image_file);
					}
				}
				else
				{
					purple_debug_error("log", "Unable to create file %s: %s\n",
					                   path, g_strerror(errno));
				}
			}

			/* Write the new image tag */
			g_string_append_printf(newmsg, "<IMG SRC=\"%s\">", new_filename);
			g_free(new_filename);
			g_free(path);
		}

		/* Continue from the end of the tag */
		tmp = end + 1;
	}

	if (newmsg == NULL)
	{
		/* No images were found to change. */
		return (char *)msg;
	}

	/* Append any remaining message data */
	g_string_append(newmsg, tmp);

	return g_string_free(newmsg, FALSE);
}

static char *log_get_timestamp(PurpleLog *log, time_t when)
{
	gboolean show_date;
	char *date;
	struct tm tm;

	show_date = (log->type == PURPLE_LOG_SYSTEM) || (time(NULL) > when + 20*60);

	date = purple_signal_emit_return_1(purple_log_get_handle(),
	                          "log-timestamp",
	                          log, when, show_date);
	if (date != NULL)
		return date;

	tm = *(localtime(&when));
	if (show_date)
		return g_strdup(purple_date_format_long(&tm));
	else
		return g_strdup(purple_time_format(&tm));
}

static gsize colornicks_logger_write(PurpleLog *log, PurpleMessageFlags type,
							  const char *from, time_t time, const char *message)
{
	char *msg_fixed;
	char *image_corrected_msg;
	char *date;
	char *header;
	char *escaped_from;
	char *nick_color;
	PurplePlugin *plugin = purple_find_prpl(purple_account_get_protocol_id(log->account));
	PurpleLogCommonLoggerData *data = log->logger_data;
	gsize written = 0;

	if(!data) {
		const char *prpl =
			PURPLE_PLUGIN_PROTOCOL_INFO(plugin)->list_icon(log->account, NULL);
		const char *date;
		purple_log_common_writer(log, ".html");

		data = log->logger_data;

		/* if we can't write to the file, give up before we hurt ourselves */
		if(!data->file)
			return 0;

		date = purple_date_format_full(localtime(&log->time));

		written += fprintf(data->file, "<html><head>");
		written += fprintf(data->file, "<meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">");
		written += fprintf(data->file, "<title>");
		if (log->type == PURPLE_LOG_SYSTEM)
			header = g_strdup_printf("System log for account %s (%s) connected at %s",
					purple_account_get_username(log->account), prpl, date);
		else
			header = g_strdup_printf("Conversation with %s at %s on %s (%s)",
					log->name, date, purple_account_get_username(log->account), prpl);

		written += fprintf(data->file, "%s", header);
		written += fprintf(data->file, "</title></head><body>");
		written += fprintf(data->file, "<h3>%s</h3>\n", header);
		g_free(header);
	}

	/* if we can't write to the file, give up before we hurt ourselves */
	if(!data->file)
		return 0;

	escaped_from = g_markup_escape_text(from, -1);
	nick_color = get_nick_color(PIDGIN_CONVERSATION(log->conv), escaped_from);

	image_corrected_msg = convert_image_tags(log, message);
	purple_markup_html_to_xhtml(image_corrected_msg, &msg_fixed, NULL);

	/* Yes, this breaks encapsulation.  But it's a static function and
	 * this saves a needless strdup(). */
	if (image_corrected_msg != message)
		g_free(image_corrected_msg);

	date = log_get_timestamp(log, time);

	if(log->type == PURPLE_LOG_SYSTEM){
		written += fprintf(data->file, "---- %s @ %s ----<br/>\n", msg_fixed, date);
	} else {
		if (type & PURPLE_MESSAGE_SYSTEM)
			written += fprintf(data->file, "<font size=\"2\">(%s)</font><b> %s</b><br/>\n", date, msg_fixed);
		else if (type & PURPLE_MESSAGE_RAW)
			written += fprintf(data->file, "<font size=\"2\">(%s)</font> %s<br/>\n", date, msg_fixed);
		else if (type & PURPLE_MESSAGE_ERROR)
			written += fprintf(data->file, "<font color=\"#FF0000\"><font size=\"2\">(%s)</font><b> %s</b></font><br/>\n", date, msg_fixed);
		else if (type & PURPLE_MESSAGE_WHISPER) {
			if (type & PURPLE_MESSAGE_SEND)
				written += fprintf(data->file, "<font color=\"#6C2585\"><font size=\"2\">(%s)</font><b> %s &lt;whisper&gt;:</b></font> %s<br/>\n",
						date, escaped_from, msg_fixed);
			else
				written += fprintf(data->file, "<font color=\"%s\"><font size=\"2\">(%s)</font><b> %s &lt;whisper&gt;:</b></font> %s<br/>\n",
						(nick_color ? nick_color : "#6C2585"), date, escaped_from, msg_fixed);
		} else if (type & PURPLE_MESSAGE_AUTO_RESP) {
			if (type & PURPLE_MESSAGE_SEND)
				written += fprintf(data->file, _("<font color=\"#16569E\"><font size=\"2\">(%s)</font> <b>%s &lt;AUTO-REPLY&gt;:</b></font> %s<br/>\n"),
						date, escaped_from, msg_fixed);
			else if (type & PURPLE_MESSAGE_RECV)
				written += fprintf(data->file, _("<font color=\"%s\"><font size=\"2\">(%s)</font> <b>%s &lt;AUTO-REPLY&gt;:</b></font> %s<br/>\n"),
						(nick_color ? nick_color : "#A82F2F"), date, escaped_from, msg_fixed);
		} else if (type & PURPLE_MESSAGE_RECV) {
			if(purple_message_meify(msg_fixed, -1))
				written += fprintf(data->file, "<font color=\"%s\"><font size=\"2\">(%s)</font> <b>***%s</b></font> %s<br/>\n",
						(nick_color ? nick_color : "#062585"), date, escaped_from, msg_fixed);
			else
				written += fprintf(data->file, "<font color=\"%s\"><font size=\"2\">(%s)</font> <b>%s:</b></font> %s<br/>\n",
						(nick_color ? nick_color : "#A82F2F"), date, escaped_from, msg_fixed);
		} else if (type & PURPLE_MESSAGE_SEND) {
			if(purple_message_meify(msg_fixed, -1))
				written += fprintf(data->file, "<font color=\"#062585\"><font size=\"2\">(%s)</font> <b>***%s</b></font> %s<br/>\n",
						date, escaped_from, msg_fixed);
			else
				written += fprintf(data->file, "<font color=\"#16569E\"><font size=\"2\">(%s)</font> <b>%s:</b></font> %s<br/>\n",
						date, escaped_from, msg_fixed);
		} else {
			purple_debug_error("log", "Unhandled message type.\n");
			written += fprintf(data->file, "<font size=\"2\">(%s)</font><b> %s:</b></font> %s<br/>\n",
						date, escaped_from, msg_fixed);
		}
	}
	g_free(date);
	g_free(msg_fixed);
	g_free(escaped_from);
	g_free(nick_color);
	fflush(data->file);

	return written;
}

static void colornicks_logger_finalize(PurpleLog *log)
{
	PurpleLogCommonLoggerData *data = log->logger_data;
	if (data) {
		if(data->file) {
			fprintf(data->file, "</body></html>\n");
			fclose(data->file);
		}
		g_free(data->path);

		g_slice_free(PurpleLogCommonLoggerData, data);
	}
}

static GList *colornicks_logger_list(PurpleLogType type, const char *sn, PurpleAccount *account)
{
	return purple_log_common_lister(type, sn, account, ".html", colornicks_logger);
}

static GList *colornicks_logger_list_syslog(PurpleAccount *account)
{
	return purple_log_common_lister(PURPLE_LOG_SYSTEM, ".system", account, ".html", colornicks_logger);
}

static char *colornicks_logger_read(PurpleLog *log, PurpleLogReadFlags *flags)
{
	char *read;
	PurpleLogCommonLoggerData *data = log->logger_data;
	*flags = PURPLE_LOG_READ_NO_NEWLINE;
	if (!data || !data->path)
		return g_strdup(_("<font color=\"red\"><b>Unable to find log path!</b></font>"));
	if (g_file_get_contents(data->path, &read, NULL, NULL)) {
		char *minus_header = strchr(read, '\n');

		if (!minus_header)
			return read;

		minus_header = g_strdup(minus_header + 1);
		g_free(read);

		return minus_header;
	}
	return g_strdup_printf(_("<font color=\"red\"><b>Could not read file: %s</b></font>"), data->path);
}

static int colornicks_logger_total_size(PurpleLogType type, const char *name, PurpleAccount *account)
{
	return purple_log_common_total_sizer(type, name, account, ".html");
}


static gboolean
plugin_load(PurplePlugin *plugin)
{
//	purple_prefs_set_string("/purple/logging/format", "colornicks");

	colornicks_logger = purple_log_logger_new("colornicks", "Colored nicks", 11,
									  NULL,
									  colornicks_logger_write,
									  colornicks_logger_finalize,
									  colornicks_logger_list,
									  colornicks_logger_read,
									  purple_log_common_sizer,
									  colornicks_logger_total_size,
									  colornicks_logger_list_syslog,
									  NULL,
									  purple_log_common_deleter,
									  purple_log_common_is_deletable);
	purple_log_logger_add(colornicks_logger);

	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	purple_log_logger_remove(colornicks_logger);
	purple_log_logger_free(colornicks_logger);
	return TRUE;
}

static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,                           /**< type           */
	NULL,                                             /**< ui_requirement */
	0,                                                /**< flags          */
	NULL,                                             /**< dependencies   */
	PURPLE_PRIORITY_DEFAULT,                          /**< priority       */

	"ankitkv-colornicks_logger",                      /**< id             */
	"ColorNicks Logger",                              /**< name           */
	"0.1",                                            /**< version        */
	                                                  /**  summary        */
	"Store unique colored nicks in HTML logs.",
	                                                  /**  description    */
	"This plugin adds the log format 'Colored nicks', "
	"which can store unique colored nicks in HTML "
	"logs.",
	"Ankit Vani <a@nevitus.org>",                     /**< author         */
	"http://nevitus.com",                             /**< homepage       */

	plugin_load,                                      /**< load           */
	plugin_unload,                                    /**< unload         */
	NULL,                                             /**< destroy        */

	NULL,                                             /**< ui_info        */
	NULL,                                             /**< extra_info     */
	NULL,
	NULL,
	/* Padding */
	NULL,
	NULL,
	NULL,
	NULL
};

static void
init_plugin(PurplePlugin *plugin)
{
}

PURPLE_INIT_PLUGIN(colornicks_logger, init_plugin, info)
