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
#include <linux/wireless.h>

// undefine max & min.
// suppresses redefinition warning
// dwl has it's own implementation
#undef MAX
#undef MIN

#define DATE_STR_MAX (32)
#define MEMORY_STR_MAX (16)
#define TEMP_STR_MAX (16)

// color scheme index enumeration
// see colors variable in config.def.h
enum {
	ColFg,
	ColBg,
	ColBorder
};

enum network_type {
	Disconnected,
	Wired,
	Wireless,

	// TODO definitely on the farthest back of the backburners.
	// Adwaita supports cellular icons but this will take some
	// work, plus a machine with cellular capabilities.
	Cellular
};

enum battery_status {
	Discharging,
	Charging,
	Full,
	// shorthand word for "not charging"
	Inhibited
};

struct icon {
	RsvgHandle *handle;
	cairo_surface_t *surface;
	cairo_t *context;
	RsvgRectangle viewport;
};

struct wireless_icons {
	struct icon disconnected;
	struct icon good;
	struct icon okay;
	struct icon weak;
	struct icon none;
};

// not a typical naming scheme.
// the underscore is just so it compiles
struct charging_icons {
	struct icon _10;
	struct icon _20;
	struct icon _30;
	struct icon _40;
	struct icon _50;
	struct icon _60;
	struct icon _70;
	struct icon _80;
	struct icon _90;
	struct icon _100;
};

struct discharging_icons {
	struct icon _0;
	struct icon _10;
	struct icon _20;
	struct icon _30;
	struct icon _40;
	struct icon _50;
	struct icon _60;
	struct icon _70;
	struct icon _80;
	struct icon _90;
	struct icon _100;
};

struct battery_icons {
	struct charging_icons charging;
	struct discharging_icons discharging;

};

struct network_info {
	enum network_type type;
	char name[IW_ESSID_MAX_SIZE];
	int quality;
};

// these structs are probably only going to be
// filled with a string. However it makes the
// organization seem slightly more logical.
// not to mention I can edit them easily
// if need be!

struct memory_info {
	char usage_ratio[MEMORY_STR_MAX];
};

struct temp_info {
	char celsius[TEMP_STR_MAX];
};

struct battery_info {
	int plugged_in;
	int capacity;
	enum battery_status status;
};

struct time_info {
	char date[DATE_STR_MAX];
};

struct system_info {
	struct network_info network;
	struct memory_info memory;
	struct temp_info temp;
	struct battery_info charge;
	struct time_info date;
};

struct font_conf {
	PangoContext *context;
	PangoFontDescription *desc;
	unsigned int height;
	PangoLayout *layout;
};

struct statusbar {
	// used for updating system info struct every 45 seconds
	struct wl_event_source *timed_event_source;

	struct system_info system_info;

	// all statusbar panels get written to here.
	cairo_surface_t *surface;
	cairo_t *context;
};

// TODO rename Drwl to something like statusbar?
// Plus make it not a typedef. That is confusing.
struct Drwl {
	struct wireless_icons wireless;
	struct battery_icons battery;

	struct font_conf *font;

	cairo_surface_t *surface;
	cairo_t *context;

	uint32_t *scheme;
};

void formatstatusbar(struct system_info *info);

void set_color(cairo_t *cr, uint32_t hex);

int draw_system_info(struct Drwl *drwl, struct system_info *info, int x, int y);

struct Drwl *drwl_create(const char *font);

void drwl_prepare_drawing(struct Drwl *drwl, int w, int h, int stride, unsigned char *data);

void delineate_rect(cairo_t *cr, int x, int y, int w, int h);

void filled_rect(cairo_t *cr, int x, int y, int w, int h);

void filled_rounded_rect(cairo_t *cr, int x, int y, int w, int h, double radius);

void render_icon(cairo_t *cr, struct icon *icon, double x, double y);

void render_text(cairo_t *cr, struct font_conf *font, int x, int y, const char *text);

int text_width(struct font_conf *font, const char *text);

void drwl_finish_drawing(struct Drwl *drwl);

void destroy_icon(struct icon *icon);

void drwl_destroy(struct Drwl *drwl);
