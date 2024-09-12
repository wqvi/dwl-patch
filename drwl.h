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
#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>
#include <librsvg/rsvg.h>
// suppress macro redefinition
// we don't need glib min & max
#undef MAX
#undef MIN

enum { ColFg, ColBg, ColBorder }; /* colorscheme index */

typedef struct fcft_font Fnt;

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

	cairo_surface_t *surface;
	cairo_t *context;

	// this could be anything from
	// "Monospace" to "Sans"
	char *font_name;
	cairo_font_face_t *font_face;
	unsigned int font_size;
	unsigned int font_height;

	pixman_image_t *pix;
	Fnt *font;
	uint32_t *scheme;
} Drwl;

#define ADWAITA_THEME_DIR "/usr/share/icons/Adwaita/symbolic"

static void
drwl_init(void)
{
	FcInit();
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
	size_t font_str_size;
	cairo_surface_t *dummy_surface;
	cairo_t *dummy_context;
	FcPattern *pattern;
	FcResult result;
	FcPattern *match;
	cairo_font_extents_t extent;
	
	drwl = calloc(1, sizeof(Drwl));
	if (!drwl) {
		fprintf(stderr, "Failed to allocate memory for status bar\n");
		return NULL;
	}

	font_str_size = strlen(font_name);
	drwl->font_name = calloc(font_str_size, sizeof(char));
	if (!drwl->font_name) {
		fprintf(stderr, "Failed to allocate memory for font name string\n");
		free(drwl);
		return NULL;
	}
	memcpy(drwl->font_name, font_name, font_str_size);

	// create dummy surfaces to get font information
	// this is meant for dwl not for the status bar
	dummy_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	dummy_context = cairo_create(dummy_surface);

	// Get the font, for some reason it's an unsigned char despite needing a string
	// gonna be honest I don't entirely understand what this does I just got it from an example.
	// it works, so who am I to complain!
	pattern = FcNameParse((const FcChar8 *)font_name);
	FcConfigSubstitute(NULL, pattern, FcMatchPattern);
	FcDefaultSubstitute(pattern);

	match = FcFontMatch(NULL, pattern, &result);
	if (!match) {
		fprintf(stderr, "Font not found\n");
		return NULL;
	}

	drwl->font_face = cairo_ft_font_face_create_for_pattern(match);
	cairo_set_font_face(dummy_context, drwl->font_face);
	drwl->font_size = font_size;
	cairo_set_font_size(dummy_context, drwl->font_size);

	// Get font height and assign it to status bar.
	// Uses the dummy context which gets freed after the status bar
	// is finished up with it's creation
	cairo_font_extents(dummy_context, &extent);
	drwl->font_height = (unsigned int)extent.height;

	// load all the icons necessary for wireless networks
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-disabled-symbolic.svg", &drwl->wifi.disabled);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-good-symbolic.svg", &drwl->wifi.good);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-ok-symbolic.svg", &drwl->wifi.okay);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-weak-symbolic.svg", &drwl->wifi.weak);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-none-symbolic.svg", &drwl->wifi.none);

	// these aren't needed as they are just temporarily used
	// for getting the font height
	//
	// What a terrible hack I know, but that's how cairo works.
	cairo_destroy(dummy_context);
	cairo_surface_destroy(dummy_surface);

	FcPatternDestroy(pattern);
	FcPatternDestroy(match);

	return drwl;
}

static void
drwl_prepare_drawing(Drwl *drwl, int w, int h, int stride, unsigned char *data)
{
	drwl->surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, w, h, stride);
	drwl->context = cairo_create(drwl->surface);

	cairo_set_font_face(drwl->context, drwl->font_face);
	cairo_set_font_size(drwl->context, drwl->font_size);
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
	cairo_text_extents_t extent;
	float surface_height;
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
	cairo_text_extents(drwl->context, text, &extent);
	surface_height = (float)cairo_image_surface_get_height(drwl->surface);
	text_y = (surface_height - (float)extent.height) / 2.0f - (float)extent.y_bearing;

	cairo_move_to(drwl->context, x, text_y);
	cairo_show_text(drwl->context, text);

	return x + (render ? w : 0);
}

static unsigned int
drwl_font_getwidth(Drwl *drwl, const char *text)
{
	cairo_text_extents_t extents;
	cairo_text_extents(drwl->context, text, &extents);

	return (unsigned int)extents.width;
}

static unsigned int
drwl_font_getheight(Drwl *drwl)
{
	cairo_font_extents_t extents;
	cairo_font_extents(drwl->context, &extents);

	return (unsigned int)extents.height;
}

static void
drwl_finish_drawing(Drwl *drwl)
{
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
	cairo_font_face_destroy(drwl->font_face);

	destroy_icon(&drwl->wifi.disabled);
	destroy_icon(&drwl->wifi.good);
	destroy_icon(&drwl->wifi.okay);
	destroy_icon(&drwl->wifi.weak);
	destroy_icon(&drwl->wifi.none);

	free(drwl->font_name);
	free(drwl);
}

static void drwl_fini(void) {
}
