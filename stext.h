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
// TODO rename this file
#pragma once

#include <pango/pangocairo.h>
#include <librsvg/rsvg.h>

// undefine max & min.
// suppresses redefinition warning
// dwl has it's own implementation
#undef MAX
#undef MIN

// color scheme index enumeration
// see colors variable in config.def.h
enum {
	ColFg,
	ColBg,
	ColBorder
};

struct icon {
	RsvgHandle *handle;
	cairo_surface_t *surface;
	cairo_t *context;
	RsvgRectangle viewport;
};

struct wifi_icons {
	struct icon disabled;
	struct icon good;
	struct icon okay;
	struct icon weak;
	struct icon none;
};

struct wifi_info {
	char *network_name;
	int quality;
};

// TODO rename Drwl to something like statusbar?
// Plus make it not a typedef. That is confusing.
struct Drwl {
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
};

void formatstatusbar(char *stext);

void statusbar(char *stext);

struct Drwl *drwl_create(const char *font);

void drwl_prepare_drawing(struct Drwl *drwl, int w, int h, int stride, unsigned char *data);

void drwl_rect(struct Drwl *drwl,
		int x, int y, unsigned int w, unsigned int h,
		int filled, int invert);

void render_icon(struct Drwl *drwl, struct icon *icon, double x, double y, int w, int h);

int drwl_text(struct Drwl *drwl,
		int x, int y, int w, int h,
		unsigned int lpad, const char *text, int invert);

unsigned int drwl_font_getwidth(struct Drwl *drwl, const char *text);

void drwl_finish_drawing(struct Drwl *drwl);

void destroy_icon(struct icon *icon);

void drwl_destroy(struct Drwl *drwl);
