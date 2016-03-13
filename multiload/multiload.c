/* GNOME multiload panel applet
 * (C) 1997 The Free Software Foundation
 *
 * Authors: Tim P. Gerla
 *          Martin Baulig
 *          Todd Kulesza
 *
 * With code from wmload.c, v0.9.2, apparently by Ryan Land, rland@bc1.com.
 *
 */

#include <config.h>
#include <glibtop.h>
#include "multiload.h"

/* update the tooltip to the graph's current "used" percentage */
void
multiload_tooltip_update(LoadGraph *g)
{
	gchar *text;
	gchar *tooltip_markup;
	const gchar *name;

	g_assert(g);

	/* label the tooltip intuitively */
	if ( g->id >= 0 && g->id < NGRAPHS )
		name = graph_types[g->id].noninteractive_label;
	else
		g_assert_not_reached();

	switch (g->id) {
		case GRAPH_CPULOAD: {
			guint total_used = g->data[0][0] + g->data[0][1] + g->data[0][2] + g->data[0][3];

			guint percent = 100.0f * total_used / g->draw_height;
			percent = MIN(percent, 100);

			text = g_strdup_printf(_("%u%% in use"), percent);
		}	break;

		case GRAPH_MEMLOAD: {
			guint mem_user, mem_cache, user_percent, cache_percent;
			mem_user  = g->data[0][0];
			mem_cache = g->data[0][1] + g->data[0][2] + g->data[0][3];
			user_percent = 100.0f * mem_user / g->draw_height;
			cache_percent = 100.0f * mem_cache / g->draw_height;
			user_percent = MIN(user_percent, 100);
			cache_percent = MIN(cache_percent, 100);

			// xgettext: use and cache are > 1 most of the time, assume that they always are.
			text = g_strdup_printf(_(	"%u%% in use by programs\n"
										"%u%% in use as cache"),
										user_percent, cache_percent);
		}	break;

		case GRAPH_NETLOAD: {
			gchar *tx_in = netspeed_get(g->netspeed_in);
			gchar *tx_out = netspeed_get(g->netspeed_out);

			text = g_strdup_printf(_(	"Receiving %s\n"
										"Sending %s"),
										tx_in, tx_out);
			g_free(tx_in);
			g_free(tx_out);
		}	break;

		case GRAPH_SWAPLOAD: {
			guint percent = 100.0f * g->data[0][0] / g->draw_height;
			percent = MIN(percent, 100);

			text = g_strdup_printf(_("%u%% in use"), percent);
		}	break;

		case GRAPH_LOADAVG: {
			text = g_strdup_printf(_(	"Last minute: %0.02f\n"
										"Last 5 minutes: %0.02f\n"
										"Last 10 minutes: %0.02f"),
										g->loadavg[0], g->loadavg[1], g->loadavg[2]);
		}	break;

		case GRAPH_DISKLOAD: {
			gchar *disk_read = format_rate_for_display(g->diskread);
			gchar *disk_write = format_rate_for_display(g->diskwrite);

			text = g_strdup_printf(_(	"Read %s\n"
										"Write %s"),
										disk_read, disk_write);

			g_free(disk_read);
			g_free(disk_write);
		}	break;

		case GRAPH_TEMPERATURE: {
			text = g_strdup_printf(_("%.1f °C"), (g->temperature/1000.0));
		}	break;

		default: {
			guint i;
			guint percent;
			guint total_used = 0;

			for (i = 0; i < graph_types[g->id].num_colors-EXTRA_COLORS; i++)
				total_used += g->data[0][i];

			percent = 100.0f * total_used / g->draw_height;
			percent = MIN(percent, 100);

			text = g_strdup_printf(_("%u%% in use"), percent);
		}	break;
	}

	tooltip_markup = g_strdup_printf("<span underline='single' weight='bold' size='larger'>%s</span>\n%s", name, text);

	gtk_widget_set_tooltip_markup(g->disp, tooltip_markup);
	g_free(text);
	g_free(tooltip_markup);
}

static void
multiload_create_graphs(MultiloadPlugin *ma)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (graph_types); i++) {
		g_assert (graph_types[i].num_colors <= MAX_COLORS);
		ma->graphs[i] = load_graph_new (ma, i);
	}
}

/* get current orientation */
GtkOrientation
multiload_get_orientation(MultiloadPlugin *ma) {
	if (ma->orientation_policy == MULTILOAD_ORIENTATION_HORIZONTAL)
		return GTK_ORIENTATION_HORIZONTAL;
	else if (ma->orientation_policy == MULTILOAD_ORIENTATION_VERTICAL)
		return GTK_ORIENTATION_VERTICAL;
	else // if (ma->orientation_policy == MULTILOAD_ORIENTATION_AUTO)
		return ma->panel_orientation;
}

/* remove the old graphs and rebuild them */
void
multiload_refresh(MultiloadPlugin *ma)
{
	gint i;

	// stop and free the old graphs
	for (i = 0; i < NGRAPHS; i++) {
		if (!ma->graphs[i])
			continue;

		load_graph_stop(ma->graphs[i]);
		gtk_widget_destroy(ma->graphs[i]->main_widget);

		load_graph_unalloc(ma->graphs[i]);
		g_free(ma->graphs[i]);
	}

	if (ma->box)
		gtk_widget_destroy(ma->box);

	ma->box = gtk_vbox_new (FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(ma->box), ma->padding);

	// Switch between GtkVBox and GtkHBox depending of orientation
	gtk_orientable_set_orientation(GTK_ORIENTABLE(ma->box), multiload_get_orientation(ma));

	gtk_widget_show (ma->box);
	gtk_container_add (ma->container, ma->box);

	// Children (graphs) are individually shown/hidden to control visibility
	gtk_widget_set_no_show_all (ma->box, TRUE);

	// Create the NGRAPHS graphs, with user properties from ma->graph_config
	multiload_create_graphs (ma);

	// Only start and display the graphs the user has turned on
	for (i = 0; i < NGRAPHS; i++) {
		gtk_box_pack_start(GTK_BOX(ma->box), 
						   ma->graphs[i]->main_widget,
						   TRUE, TRUE, ma->spacing);
		if (ma->graph_config[i].visible) {
			gtk_widget_show_all (ma->graphs[i]->main_widget);
			load_graph_start(ma->graphs[i]);
		}
	}

	return;
}

void
multiload_init()
{
	static int initialized = 0;
	if ( initialized )
		return;

	glibtop *glt = glibtop_init();
	g_assert(glt != NULL);

	/* Prepare graph types */
	GraphType temp[] = {
		/*	prefs_label			tooltip_label		name			get_data */
		{	_("_Processor"),	_("Processor"),		"cpuload",		GetLoad,
			6, { // hue: 196
				{ _("_User"),			_("User"),			"#FF036F96" },
				{ _("_System"),			_("System"),		"#FF42ACD1" },
				{ _("N_ice"),			_("Nice"),			"#FFBEEEFF" },
				{ _("I_OWait"),			_("IOWait"),		"#FF002633" },
				{ _("Bor_der"),			_("Border"),		"#FF005D80" },
				{ _("_Background"),		_("Background"),	"#FF000000" }
			}
		},

		{	_("_Memory"),		_("Memory"),		"memload",		GetMemory,
			6, { // hue: 151
				{ _("_User"),			_("User"),			"#FF03964F" },
				{ _("_Shared"),			_("Shared"),		"#FF43D18D" },
				{ _("_Buffers"),		_("Buffers"),		"#FFBFFFE0" },
				{ _("Cach_ed"),			_("Cached"),		"#FF00331A" },
				{ _("Bor_der"),			_("Border"),		"#FF008042" },
				{ _("_Background"),		_("Background"),	"#FF000000" }
			}
		},

		{	_("_Network"),		_("Network"),		"netload",		GetNet,
			5, { // hue: 53
				{ _("_In"),				_("In"),			"#FFE2CC05" },
				{ _("O_ut"),			_("Out"),			"#FF696018" },
				{ _("L_ocal"),			_("Local"),			"#FFFFF7B1" },
				{ _("Bor_der"),			_("Border"),		"#FF807100" },
				{ _("_Background"),		_("Background"),	"#FF000000" }
			}
		},

		{	_("S_wap Space"),	_("Swap Space"),	"swapload",		GetSwap,
			3, { // hue: 278
				{ _("_Used"),			_("Used"),			"#FF9C43D1" },
				{ _("Bor_der"),			_("Border"),		"#FF510080" },
				{ _("_Background"),		_("Background"),	"#FF000000" }
			}
		},

		{	_("_Load"),			_("Load Average"),	"loadavg",		GetLoadAvg,
			3, { // hue: 0
				{ _("A_verage"),		_("Average"),		"#FFD14343" },
				{ _("Bor_der"),			_("Border"),		"#FF800000" },
				{ _("_Background"),		_("Background"),	"#FF000000" }
			}
		},

		{	_("_Disk"),			_("Disk"),			"diskload",		GetDiskLoad,
			4, { // hue: 31
				{ _("_Read"),			_("Read"),			"#FFED7A00" },
				{ _("Wr_ite"),			_("Write"),			"#FFFF6700" },
				{ _("Bor_der"),			_("Border"),		"#FF804200" },
				{ _("_Background"),		_("Background"),	"#FF000000" }
			}
		},

		{	_("_Temperature"),	_("Temperature"),	"temperature",	GetTemperature,
			3, { // hue: 310
				{ _("_Value"),			_("Value"),			"#FFF049D5" },
				{ _("Bor_der"),			_("Border"),		"#FF80006B" },
				{ _("_Background"),		_("Background"),	"#FF000000" }
			}
		}
	};
	memcpy(&graph_types, &temp, sizeof(graph_types));
}

void
multiload_destroy(MultiloadPlugin *ma)
{
	gint i;

	/* Stop the graphs */
	for (i = 0; i < NGRAPHS; i++) {
		load_graph_stop(ma->graphs[i]);
		gtk_widget_destroy(ma->graphs[i]->main_widget);

		load_graph_unalloc(ma->graphs[i]);
		g_free(ma->graphs[i]);
	}

	return;
}

/* Convert a GdkColor into a string of the form "#aarrggbb"
   Output string must have size at least 10.
 */
gboolean
multiload_gdk_color_stringify(GdkColor* color, guint alpha, gchar *color_string)
{
	int rc = snprintf(color_string, 10, "#%02X%02X%02X%02X",
					alpha / 256,
					color->red / 256, color->green / 256, color->blue / 256);
	gboolean retval = (rc == 9);
	g_assert(retval);
	return retval;
}

/* Convert a graph configuration into a string
   of the form "#aarrggbb,#aarrggbb,..."
   Output string must have size at least 10*MAX_COLORS.
 */
void
multiload_colorconfig_stringify(MultiloadPlugin *ma, guint i, char *list)
{
	guint ncolors = graph_types[i].num_colors, j;
	GdkColor *colors = ma->graph_config[i].colors;
	guint16 *alphas = ma->graph_config[i].alpha;
	char *listpos = list;

	if ( G_UNLIKELY (!list) )
		return;

	/* Create color list */
	for ( j = 0; j < ncolors; j++ ) {
		multiload_gdk_color_stringify(&colors[j], alphas[j], listpos);
		if ( j == ncolors-1 )
			listpos[9] = 0;
		else
			listpos[9] = ',';
		listpos += 10;
	}
	g_assert (strlen(list) == 10*ncolors-1);
}

/* Wrapper for gdk_color_parse with alpha */
static gboolean
gdk_color_parse_alpha(const gchar *gspec, GdkColor *color, guint16 *alpha) {
	gchar buf[8];
	guint i;
	if (strlen(gspec) == 7)
		return gdk_color_parse(gspec, color);
	else if (G_LIKELY (strlen(gspec) == 9) ) {
		// alpha part
		buf[0] = gspec[1];
		buf[1] = gspec[2];
		buf[2] = 0;
		errno = 0;
		*alpha = (guint16)strtol(buf, NULL, 16);
		if (errno) {
			// error in strtol, set alpha=max
			*alpha = 0xFFFF;
		} else {
			/* alpha is in the form '0x00jk'. Transform it in the form
			  '0xjkjk', so the conversion of 8 to 16 bits is proportional. */
			*alpha |= (*alpha << 8);
		}

		// color part
		buf[0] = '#';
		for (i=0; i<6; i++)
			buf[1+i] = gspec[3+i];
		buf[7] = 0;
		return gdk_color_parse(buf, color);
	} else
		return FALSE;
}

/* Set the colors for graph i to the default values */
void
multiload_colorconfig_default(MultiloadPlugin *ma, guint i)
{
	guint j;
	for ( j = 0; j < graph_types[i].num_colors; j++ ) {
		gdk_color_parse_alpha(graph_types[i].colors[j].default_value,
						&ma->graph_config[i].colors[j],
						&ma->graph_config[i].alpha[j]);
	}
}

/* Set the colors for a graph from a string, as produced by
   multiload_colorconfig_stringify
 */
void
multiload_colorconfig_unstringify(MultiloadPlugin *ma, guint i,
								  const char *list)
{
	guint ncolors = graph_types[i].num_colors, j;
	GdkColor *colors = ma->graph_config[i].colors;
	guint16 *alphas = ma->graph_config[i].alpha;
	const char *listpos = list;

	if ( G_UNLIKELY (!listpos) )
		return multiload_colorconfig_default(ma, i);

	for ( j = 0; j < ncolors; j++ ) {
		/* Check the length of the list item. */
		int pos = 0;
		if ( j == ncolors-1 )
			pos = strlen(listpos);
		else
			pos = (int)(strchr(listpos, ',')-listpos);

		/* Try to parse the color */
		if ( G_UNLIKELY (pos != 9) )
			return multiload_colorconfig_default(ma, i);

		/* Extract the color into a null-terminated buffer */
		char buf[10];
		strncpy(buf, listpos, 9);
		buf[9] = 0;
		if ( G_UNLIKELY (gdk_color_parse_alpha(buf, &colors[j], &alphas[j]) != TRUE) )
			return multiload_colorconfig_default(ma, i);

		listpos += 10;
	}

	//ignore alpha value of last color (background)
	alphas[ncolors-1] = 0xFFFF;
}

int
multiload_find_graph_by_name(const char *str, const char **suffix)
{
	guint i;
	for ( i = 0; i < NGRAPHS; i++ ) {
		int n = strlen(graph_types[i].name);
		if ( strncasecmp(str, graph_types[i].name, n) == 0 ) {
			if ( suffix )
				*suffix = str+n;
			return i;
		}
	}
	return -1;
}
