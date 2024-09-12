/*
 * drwl - https://codeberg.org/sewn/drwl
 *
 * Copyright (c) 2023-2024 sewn <sewn@disroot.org>
 * Copyright (c) 2024 notchoc <notchoc@disroot.org>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The UTF-8 Decoder included is from Bjoern Hoehrmann:
 * Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
 * See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
 */
#pragma once

#include <stdlib.h>
#include <fcft/fcft.h>
#include <pixman-1/pixman.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <librsvg/rsvg.h>

// undefine max & min.
// suppresses redefinition warning
// dwl has it's own implementation
#undef MAX
#undef MIN

#define ADWAITA_THEME_DIR "/usr/share/icons/Adwaita/symbolic"

enum { ColFg, ColBg, ColBorder }; /* colorscheme index */

struct icon {
	RsvgHandle *handle;
	// 16px by 16px for now because I am programming this
	// for a 1920x1200 screen :)
	cairo_surface_t *surface;
	cairo_t *context;
};

struct wifi_icons {
	struct icon disabled;
	struct icon good;
	struct icon okay;
	struct icon weak;
	struct icon none;
};

typedef struct {
	struct wifi_icons wifi;

	// font context. used for getting font height
	// prior to any surface creation
	PangoContext *pango_context;

	// Font description, so the name of the font
	// and the size of the font
	PangoFontDescription *pango_description;

	// Display metrics for the font, so size in pixels
	// used to populate the font_height variable
	unsigned int font_height;

	cairo_surface_t *surface;
	cairo_t *context;
	// used for rendering text to the surface
	PangoLayout *pango_layout;

	uint32_t *scheme;
} Drwl;

static void
drwl_init(void)
{
}

static void load_icon(const char *file, struct icon *icon) {	
	GError *error = NULL;
	icon->handle = rsvg_handle_new_from_file(file, &error);
	if (error) {
		fprintf(stderr, "Error loading icon: %s\n", error->message);
		return;
	}

	icon->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
	icon->context = cairo_create(icon->surface);
}

static Drwl *
drwl_create(const char *font_name, unsigned int font_size)
{
	Drwl *drwl;
	// this variable is for getting the font height
	// this is for calculating parts of the bar prior
	// to any rendering
	PangoFontMap *font_map;
	PangoFontMetrics *metrics;
	int ascent;
	int descent;
	float font_height;
	
	drwl = calloc(1, sizeof(Drwl));
	if (!drwl) {
		fprintf(stderr, "Failed to allocate memory for status bar\n");
		return NULL;
	}

	// Create a pango context from default cairo font map
	font_map = pango_cairo_font_map_get_default();
	drwl->pango_context = pango_font_map_create_context(font_map);

	// TODO replace the hardcoded string with one from config.def.h
	drwl->pango_description = pango_font_description_from_string("LiberationMono 12");
	// Get font metrics and use the metrics to get the font height
	metrics = pango_context_get_metrics(drwl->pango_context, drwl->pango_description, NULL);
	ascent = pango_font_metrics_get_ascent(metrics);
	descent = pango_font_metrics_get_descent(metrics);
	font_height = (float)(ascent + descent) / (float)PANGO_SCALE;
	drwl->font_height = (unsigned int)font_height;

	// load all the icons necessary for wireless networks
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-disabled-symbolic.svg", &drwl->wifi.disabled);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-good-symbolic.svg", &drwl->wifi.good);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-ok-symbolic.svg", &drwl->wifi.okay);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-weak-symbolic.svg", &drwl->wifi.weak);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-none-symbolic.svg", &drwl->wifi.none);

	pango_font_metrics_unref(metrics);

	return drwl;
}

static void
drwl_prepare_drawing(Drwl *drwl, int w, int h, int stride, unsigned char *data)
{
	// create all the necessary information to write to the wayland buffer
	drwl->surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, w, h, stride);
	drwl->context = cairo_create(drwl->surface);
	drwl->pango_layout = pango_layout_new(drwl->pango_context);
	pango_layout_set_font_description(drwl->pango_layout, drwl->pango_description);
}

static void
drwl_rect(Drwl *drwl,
		int x, int y, unsigned int w, unsigned int h,
		int filled, int invert)
{
	cairo_set_source_rgba(drwl->context, 1.0, 1.0, 1.0, 1.0);
	if (filled) {
		cairo_rectangle(drwl->context, x, y, w, h);
		cairo_fill(drwl->context);
	} else {
		// grab anti alias settings
		cairo_antialias_t antialias = cairo_get_antialias(drwl->context);
		// when anti aliasing is on
		// cairo will produce a blurry outline of a rectangle
		cairo_set_antialias(drwl->context, CAIRO_ANTIALIAS_NONE);

		cairo_set_line_width(drwl->context, 1.0);
		// offset the rectangle outline to be within the outline.
		// meaning it doesn't render the outline it renders the actual
		// edges of the rectangle
		cairo_rectangle(drwl->context, x + 1, y + 1, w - 1, h - 1);
		cairo_stroke(drwl->context);

		// set anti alias settings to what it was prior to drawing outline
		cairo_set_antialias(drwl->context, antialias);
	}
}

static int
drwl_text(Drwl *drwl,
		int x, int y, int w, int h,
		unsigned int lpad, const char *text, int invert)
{
	PangoRectangle bearing_rect;
	PangoRectangle logical_rect;
	float surface_height;
	float height;
	float y_bearing;
	float text_y;
	int render = x || y || w || h;

	if (!render) {
		w = invert ? invert : ~invert;
	} else {
		//clr = convert_color(drwl->scheme[invert ? ColBg : ColFg]);
		//fg_pix = pixman_image_create_solid_fill(&clr);

		//drwl_rect(drwl, x, y, w, h, 1, !invert);

		x += lpad;
		w -= lpad;
	}

	//drwl_rect(drwl, x, y, w, h, 1, !invert);

	// set current color, in this case for the font
	cairo_set_source_rgba(drwl->context, 1.0, 1.0, 1.0, 1.0);

	// calculate the position to center the text
	pango_layout_get_extents(drwl->pango_layout, &bearing_rect, &logical_rect);
	pango_layout_set_text(drwl->pango_layout, text, -1);
	surface_height = (float)cairo_image_surface_get_height(drwl->surface);
	// wow pango library is annoying since I have
	// to divide every value from the library by the
	// PANGO_SCALE constant
	height = logical_rect.height / PANGO_SCALE;
	y_bearing = bearing_rect.y / PANGO_SCALE;

	// actually calculate the center of the y axis
	text_y = (surface_height - height) / 2.0f - y_bearing;

	// render the text
	cairo_move_to(drwl->context, x, text_y);
	pango_cairo_show_layout(drwl->context, drwl->pango_layout);

	return x + (render ? w : 0);
}

static unsigned int
drwl_font_getwidth(Drwl *drwl, const char *text)
{
	PangoRectangle extent;
	pango_layout_set_text(drwl->pango_layout, text, -1);
	pango_layout_get_extents(drwl->pango_layout, NULL, &extent);
	return (unsigned int)extent.width / PANGO_SCALE;
}

static unsigned int
drwl_font_getheight(Drwl *drwl)
{
	return drwl->font_height;
}

static void
drwl_finish_drawing(Drwl *drwl)
{
	g_object_unref(drwl->pango_layout);
	cairo_destroy(drwl->context);
	cairo_surface_destroy(drwl->surface);
}

static void destroy_icon(struct icon *icon) {
	cairo_destroy(icon->context);
	cairo_surface_destroy(icon->surface);
	g_object_unref(icon->handle);
}

static void
drwl_destroy(Drwl *drwl)
{
	pango_font_description_free(drwl->pango_description);

	g_object_unref(drwl->pango_context);

	destroy_icon(&drwl->wifi.disabled);
	destroy_icon(&drwl->wifi.good);
	destroy_icon(&drwl->wifi.okay);
	destroy_icon(&drwl->wifi.weak);
	destroy_icon(&drwl->wifi.none);

	free(drwl);
}

static void drwl_fini(void) {
}
