#include "stext.h"

#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/wireless.h>
#include <sys/ioctl.h>

#define AC "/sys/class/power_supply/AC/"
#define BAT0 "/sys/class/power_supply/BAT0/"

#define SYSBUFSIZ (4096)

#define ADWAITA_THEME_DIR "/usr/share/icons/Adwaita/symbolic"
#define SVG_SURFACE_WIDTH (64)
#define SVG_SURFACE_HEIGHT (64)

#define DEG_TO_RAD(x) ((x) * (G_PI / 180.0))

#define PANEL_PADDING (4)
#define PANEL_SPACE (8)

static void formatdate(struct time_info *date) {
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	if (tm == NULL) return;

	// ISO Date format, 12 hours
	// YYYY-MM-DD HH:MM AM/PM
	strftime(date->date, DATE_STR_MAX, "%F %I:%M %p", tm);
}

static int sysread(char *_buffer, const char *_file) {
	int fd = open(_file, O_RDONLY);
	struct stat stat;
	if (fd == -1) {
		return 1;
	}

	if (fstat(fd, &stat) == -1) {
		goto error;
	}

	if (stat.st_size != SYSBUFSIZ) {
		goto error;
	}
	
	if (read(fd, _buffer, stat.st_size) == 0) {
		goto error;
	}

	close(fd);
	return 0;

error:
	close(fd);
	return 1;
}

static void formatbat(struct battery_info *info) {
	char buffer[SYSBUFSIZ];
	int capacity;
	int plugged_in;
	char status[4] = "UNK";

	if (sysread(buffer, AC "online")) {
		return;
	}
	plugged_in = (int)strtol(buffer, NULL, 0);
	info->plugged_in = plugged_in;

	if (sysread(buffer, BAT0 "capacity")) {
		return;
	}
	capacity = (int)strtol(buffer, NULL, 0);
	info->capacity = capacity;
	
	if (sysread(buffer, BAT0 "status")) {
		return;
	}

	// Assuming implementation of /sys/class/power_supply/BAT0/status
	// prints shorthand of "Discharging", "Charging", "Full", and "Not charging" (Inhibited)
	switch (buffer[0]) {
	case 'D':
		snprintf(status, 4, "DIS");
		info->status = Discharging;
		break;
	case 'C':
		snprintf(status, 4, "CHR");
		info->status = Charging;
		break;
	case 'F':
		snprintf(status, 4, "FUL");
		info->status = Full;
		break;
	case 'N':
		snprintf(status, 4, "INH");
		info->status = Inhibited;
		break;
	}
}

static void formattemp(struct temp_info *temp) {
	char buffer[SYSBUFSIZ];
	int celsius;
	
	if (sysread(buffer, "/sys/class/thermal/thermal_zone0/temp")) {
		return;
	}

	celsius = (int)(strtod(buffer, NULL) / 1000.0);

	snprintf(temp->celsius, TEMP_STR_MAX, "%d\U000000B0C", celsius);
}

static int cmp(const char *_haystack, const char *_needle) {
	return (strncmp(_haystack, _needle, strlen(_needle)) == 0);
}

static void formatram(struct memory_info *info) {
	long double memtotal = 0;
	long double memfree = 0;
	long double buffers = 0;
	long double cached = 0;
	FILE *fp;
	char line[128];
	long double gb = 1 << 20; // I hope your system is little endian
	long double memused; 

	if (!(fp = fopen("/proc/meminfo", "rb"))) return;

	while (fgets(line, 128, fp)) {
		if (cmp(line, "MemTotal:")) {
			memtotal = strtold(line + strlen("MemTotal:"), NULL);
		} else if (cmp(line, "MemFree:")) {
			memfree = strtold(line + strlen("MemFree:"), NULL);
		} else if (cmp(line, "Buffers:")) {
			buffers = strtold(line + strlen("Buffers:"), NULL);
		} else if (cmp(line, "Cached:")) {
			cached = strtold(line + strlen("Cached:"), NULL);
		}

		// early exit
		if (!memtotal && !memfree && !buffers && !cached) break;
	}

	fclose(fp);

	memused = (memtotal - memfree - buffers - cached) / gb;

	snprintf(info->usage_ratio, MEMORY_STR_MAX, "%.1LfGb/%.1LfGb", memused, memtotal / gb);
}

static int resolve_ifname(struct iwreq *_rq) {
	struct ifaddrs *head;
	struct ifaddrs *list;
	if (getifaddrs(&head)) {
		return 1;
	}

	list = head;
	
	for (; list != NULL; list = list->ifa_next) {
		// skip loopback device
		if (strcmp(list->ifa_name, "lo") == 0) {
			continue;
		}
		
		// TODO add this to the wifi widget
		// skip wireguard device
		if (strncmp(list->ifa_name, "wg0", 2) == 0) {
			continue;
		}

		// skip any empty device names
		if (!strlen(list->ifa_name)) {
			continue;
		}

		// skip any non IPv4 devices
		if (list->ifa_addr->sa_family != AF_INET) {
			continue;
		}

		strncpy(_rq->ifr_name, list->ifa_name, IFNAMSIZ);
		_rq->u.addr.sa_family = AF_INET;
		break;
	}

	freeifaddrs(head);
	
	return 0;
}

static void formatnetwork(struct network_info *info) {
	struct iwreq rq;
	int fd;
	struct sockaddr_in *in;
	char addr[INET_ADDRSTRLEN];
	char essid[48] = { 0 }; // null terminated aligned buffer
	struct iw_statistics stats;
	struct iw_range range;
	int quality = 0;
	
	// default to disconnected just in case network is off
	info->type = Disconnected;

	if (resolve_ifname(&rq)) {
		return;
	}
	
	fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd == -1) {
		return;
	}

	// Converts numerical ip address to string address
	if (ioctl(fd, SIOCGIFADDR, &rq) < 0) {
		close(fd);
		return;
	}
	in = (struct sockaddr_in *)&rq.u.ap_addr;
	inet_ntop(AF_INET, &in->sin_addr, addr, INET_ADDRSTRLEN);

	// Get network essid
	rq.u.essid.pointer = essid;
	rq.u.essid.length = IW_ESSID_MAX_SIZE;
	rq.u.essid.flags = 0;
	if (ioctl(fd, SIOCGIWESSID, &rq) < 0) {
		close(fd);
		return;
	}

	// Get network statistics
	rq.u.data.pointer = &stats;
	rq.u.data.length = sizeof(struct iw_statistics);
	rq.u.data.flags = 0;
	if (ioctl(fd, SIOCGIWSTATS, &rq) < 0) {
		close(fd);
		return;
	}

	// Get network range
	rq.u.data.pointer = &range;
	rq.u.data.length = sizeof(struct iw_range);
	rq.u.data.flags = 0;
	if (ioctl(fd, SIOCGIWRANGE, &rq) < 0) {
		close(fd);
		return;
	}

	close(fd);

	if (stats.qual.qual != 0) {
		quality = (stats.qual.qual * 100) / range.max_qual.qual;
	}

	// lets just default to wireless for now (this is bad lol)
	info->type = Wireless;

	memcpy(info->name, essid, strlen(essid));
	info->quality = quality;
}

void formatstatusbar(struct system_info *info) {
	formatnetwork(&info->network);

	formatram(&info->memory);

	formattemp(&info->temp);

	formatbat(&info->charge);

	formatdate(&info->date);
}

void set_color(cairo_t *cr, uint32_t hex) {
	double r = ((hex >> 24) & 0xFF) / 255.0;
	double g = ((hex >> 16) & 0xFF) / 255.0;
	double b = ((hex >> 8) & 0xFF) / 255.0;
	double a = (hex & 0xFF) / 255.0;

	cairo_set_source_rgba(cr, r, g, b, a);
}

static struct icon *get_wireless_icon(struct wireless_icons *wireless, struct network_info *info) {
	struct icon *icon = NULL;
	if (info->quality <= 25) {
		icon = &wireless->none;
	} else if (info->quality <= 50) {
		icon = &wireless->weak;
	} else if (info->quality <= 75) {
		icon = &wireless->okay;
	} else {
		icon = &wireless->good;
	}

	return icon;
}

static struct icon *get_discharging_icon(struct discharging_icons *icons, struct battery_info *info) {
	struct icon *icon = NULL;
	if (info->capacity < 5) {
		icon = &icons->_0;
	} else if (info->capacity < 15) {
		icon = &icons->_10;
	} else if (info->capacity < 25) {
		icon = &icons->_20;
	} else if (info->capacity < 35) {
		icon = &icons->_30;
	} else if (info->capacity < 45) {
		icon = &icons->_40;
	} else if (info->capacity < 55) {
		icon = &icons->_50;
	} else if (info->capacity < 65) {
		icon = &icons->_60;
	} else if (info->capacity < 75) {
		icon = &icons->_70;
	} else if (info->capacity < 85) {
		icon = &icons->_80;
	} else if (info->capacity < 95) {
		icon = &icons->_90;
	} else {
		icon = &icons->_100;
	}

	return icon;
}

static struct icon *get_charging_icon(struct charging_icons *icons, struct battery_info *info) {
	struct icon *icon = NULL;
	if (info->capacity < 5) {
		icon = &icons->_100;
	} else if (info->capacity < 15) {
		icon = &icons->_10;
	} else if (info->capacity < 25) {
		icon = &icons->_20;
	} else if (info->capacity < 35) {
		icon = &icons->_30;
	} else if (info->capacity < 45) {
		icon = &icons->_40;
	} else if (info->capacity < 55) {
		icon = &icons->_50;
	} else if (info->capacity < 65) {
		icon = &icons->_60;
	} else if (info->capacity < 75) {
		icon = &icons->_70;
	} else if (info->capacity < 85) {
		icon = &icons->_80;
	} else if (info->capacity < 95) {
		icon = &icons->_90;
	} else {
		icon = &icons->_100;
	}

	return icon;
}

static struct icon *get_battery_icon(struct battery_icons *icons, struct battery_info *info) {
	switch (info->status) {
		case Discharging:
			return get_discharging_icon(&icons->discharging, info);
		case Charging:
			return get_charging_icon(&icons->charging, info);
		case Full:
			if (info->plugged_in) {
				return &icons->charging._100;
			}
			return &icons->discharging._100;
		case Inhibited:
			return &icons->charging._100;
		default:
			return NULL;
	}
}

static int panel_icon_width(struct font_conf *font, struct icon *icon, const char *text) {
	if (text) {
		return text_width(font, text) + (int)icon->viewport.width + PANEL_PADDING * 2;
	}

	return (int)icon->viewport.width + PANEL_PADDING * 2;
}

static int panel_text_width(struct font_conf *font, const char *text) {
	return text_width(font, text) + PANEL_PADDING;
}

static int draw_panel_icon(cairo_t *cr, uint32_t *scheme, struct font_conf *font, struct icon *icon, const char *text, int x, int y) {
	int rect_width;
	int rect_x;
	int text_x;
	int icon_x;

	if (icon == NULL) {
		return x;
	}

	rect_width = panel_icon_width(font, icon, text);
	rect_x = x - rect_width;
	text_x = rect_x + PANEL_PADDING / 2;
	icon_x = x - ((int)icon->viewport.width + PANEL_PADDING);

	set_color(cr, scheme[ColFg]);
	filled_rounded_rect(cr, rect_x, y, rect_width, font->height, 4);

	set_color(cr, scheme[ColBg]);
	if (text) {
		render_text(cr, font, text_x, y, text);
	}
	render_icon(cr, icon, icon_x, y);

	return rect_x - PANEL_SPACE;
}

static int draw_panel_text(cairo_t *cr, uint32_t *scheme, struct font_conf *font, char *text, int x, int y) {
	int rect_width = panel_text_width(font, text);
	// rectangle origin is the top left. Therefore
	// you must move it to the left of the width of the rectangle
	// to not have it render off the side of the screen
	int rect_x = x - rect_width;
	// inch forward by half the padding (to center it)
	int text_x = rect_x + PANEL_PADDING / 2;

	set_color(cr, scheme[ColFg]);
	// add padding to take into account the offset text (which is half of padding)
	filled_rounded_rect(cr, rect_x, y, rect_width, font->height, 4);

	// don't draw text background, thus don't provide width & height
	// this is leftover logic from sewn's drwl statusbar
	set_color(cr, scheme[ColBg]);
	render_text(cr, font, text_x, y, text);

	// move left to next panel x position
	return rect_x - PANEL_SPACE;
}

int draw_system_info(struct Drwl *drwl, struct system_info *info, int x, int y) {
	int panel_x = x;
	struct icon *icon;

	// starts from left to right
	panel_x = draw_panel_text(drwl->context, drwl->scheme, drwl->font, info->date.date, panel_x, y);

	icon = get_battery_icon(&drwl->battery, &info->charge);
	panel_x = draw_panel_icon(drwl->context, drwl->scheme, drwl->font, icon, NULL, panel_x, y);

	panel_x = draw_panel_text(drwl->context, drwl->scheme, drwl->font, info->temp.celsius, panel_x, y);

	panel_x = draw_panel_text(drwl->context, drwl->scheme, drwl->font, info->memory.usage_ratio, panel_x, y);

	// this is incorrect. it should be get_network_icon
	// and inside get_network_icon there should be a check if
	// it's a wireless or wired connection
	icon = get_wireless_icon(&drwl->wireless, &info->network);
	panel_x = draw_panel_icon(drwl->context, drwl->scheme, drwl->font, icon, info->network.name, panel_x, y);

	// undo the last panel's spacing
	return panel_x + PANEL_SPACE;
}

static void load_icon(const char *file, struct icon *icon) {	
	GError *error = NULL;
	double svg_width;
	double svg_height;

	icon->handle = rsvg_handle_new_from_file(file, &error);
	if (error) {
		fprintf(stderr, "Error loading icon: %s\n", error->message);
		return;
	}

	icon->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, SVG_SURFACE_WIDTH, SVG_SURFACE_HEIGHT);
	icon->context = cairo_create(icon->surface);

	rsvg_handle_get_intrinsic_size_in_pixels(icon->handle, &svg_width, &svg_height);

	icon->viewport.x = 0;
	icon->viewport.y = 0;
	icon->viewport.width = svg_width;
	icon->viewport.height = svg_height;
}

struct Drwl *drwl_create(const char *font) {
	struct Drwl *drwl;
	struct font_conf *font_conf;
	// this variable is for getting the font height
	// this is for calculating parts of the bar prior
	// to any rendering
	PangoFontMap *font_map;
	PangoFontMetrics *metrics;
	float font_height;
	
	drwl = calloc(1, sizeof(struct Drwl));
	if (!drwl) {
		fprintf(stderr, "Failed to allocate memory for status bar\n");
		return NULL;
	}

	font_conf = calloc(1, sizeof(struct font_conf));
	if (!font_conf) {
		fprintf(stderr, "Failed to allocate memory for font_conf\n");
		free(drwl);
		return NULL;
	}

	// Create a pango context from default cairo font map
	font_map = pango_cairo_font_map_get_default();
	font_conf->context = pango_font_map_create_context(font_map);

	font_conf->desc = pango_font_description_from_string(font);
	// Get font metrics and use the metrics to get the font height
	metrics = pango_context_get_metrics(font_conf->context, font_conf->desc, NULL);
	font_height = (float)pango_font_metrics_get_height(metrics) / (float)PANGO_SCALE;
	font_conf->height = (unsigned int)font_height;

	// load all the icons necessary for wireless networks
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-disabled-symbolic.svg", &drwl->wireless.disconnected);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-good-symbolic.svg", &drwl->wireless.good);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-ok-symbolic.svg", &drwl->wireless.okay);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-weak-symbolic.svg", &drwl->wireless.weak);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-none-symbolic.svg", &drwl->wireless.none);

	// load all the icons necessary for the battery
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-10-charging-symbolic.svg", &drwl->battery.charging._10);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-20-charging-symbolic.svg", &drwl->battery.charging._20);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-30-charging-symbolic.svg", &drwl->battery.charging._30);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-40-charging-symbolic.svg", &drwl->battery.charging._40);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-50-charging-symbolic.svg", &drwl->battery.charging._50);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-60-charging-symbolic.svg", &drwl->battery.charging._60);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-70-charging-symbolic.svg", &drwl->battery.charging._70);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-80-charging-symbolic.svg", &drwl->battery.charging._80);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-90-charging-symbolic.svg", &drwl->battery.charging._90);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-100-charged-symbolic.svg", &drwl->battery.charging._100);

	load_icon(ADWAITA_THEME_DIR "/status/battery-level-0-symbolic.svg", &drwl->battery.discharging._0);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-10-symbolic.svg", &drwl->battery.discharging._10);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-20-symbolic.svg", &drwl->battery.discharging._20);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-30-symbolic.svg", &drwl->battery.discharging._30);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-40-symbolic.svg", &drwl->battery.discharging._40);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-50-symbolic.svg", &drwl->battery.discharging._50);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-60-symbolic.svg", &drwl->battery.discharging._60);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-70-symbolic.svg", &drwl->battery.discharging._70);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-80-symbolic.svg", &drwl->battery.discharging._80);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-90-symbolic.svg", &drwl->battery.discharging._90);
	load_icon(ADWAITA_THEME_DIR "/status/battery-level-100-symbolic.svg", &drwl->battery.discharging._100);

	pango_font_metrics_unref(metrics);

	drwl->font = font_conf;

	return drwl;
}

void drwl_prepare_drawing(struct Drwl *drwl, int w, int h, int stride, unsigned char *data) {
	// create all the necessary information to write to the wayland buffer
	drwl->surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, w, h, stride);
	drwl->context = cairo_create(drwl->surface);

	drwl->font->layout = pango_layout_new(drwl->font->context);
	pango_layout_set_font_description(drwl->font->layout, drwl->font->desc);
}

void delineate_rect(cairo_t *cr, int x, int y, int w, int h) {
	cairo_antialias_t aa = cairo_get_antialias(cr);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	cairo_set_line_width(cr, 1.0);

	cairo_rectangle(cr, x + 1, y + 1, w - 1, h - 1);
	cairo_stroke(cr);

	cairo_set_antialias(cr, aa);
}

void filled_rect(cairo_t *cr, int x, int y, int w, int h) {
	cairo_rectangle(cr, x, y, w, h);
	cairo_fill(cr);
}

void filled_rounded_rect(cairo_t *cr, int x, int y, int w, int h,
		double radius) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - radius, y + radius, radius, DEG_TO_RAD(-90), 0);
	cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, DEG_TO_RAD(90));
	cairo_arc(cr, x + radius, y + h - radius, radius, DEG_TO_RAD(90), DEG_TO_RAD(180));
	cairo_arc(cr, x + radius, y + radius, radius, DEG_TO_RAD(180), DEG_TO_RAD(270));
	cairo_close_path(cr);

	cairo_fill(cr);
}

void render_icon(cairo_t *cr, struct icon *icon, double x, double y) {
	GError *error = NULL;

	if (!rsvg_handle_render_document(icon->handle, icon->context, &icon->viewport, &error)) {
		fprintf(stderr, "Could not render svg: %s\n", error->message);
		return;
	}

	// render surface to target context
	cairo_set_source_surface(cr, icon->surface, x, 0);
	cairo_paint(cr);
}

void render_text(cairo_t *cr, struct font_conf *font, int x, int y, const char *text) {
	pango_layout_set_text(font->layout, text, -1);

	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, font->layout);
}

unsigned int drwl_font_getwidth(struct Drwl *drwl, const char *text) {
	PangoRectangle extent;
	pango_layout_set_text(drwl->font->layout, text, -1);
	pango_layout_get_extents(drwl->font->layout, NULL, &extent);
	return (unsigned int)extent.width / PANGO_SCALE;
}

int text_width(struct font_conf *font, const char *text) {
	PangoRectangle extent;
	pango_layout_set_text(font->layout, text, -1);
	pango_layout_get_pixel_extents(font->layout, NULL, &extent);
	return extent.width;
}

void drwl_finish_drawing(struct Drwl *drwl) {
	g_object_unref(drwl->font->layout);
	cairo_destroy(drwl->context);
	cairo_surface_destroy(drwl->surface);
}

void destroy_icon(struct icon *icon) {
	cairo_destroy(icon->context);
	cairo_surface_destroy(icon->surface);
	g_object_unref(icon->handle);
}

void drwl_destroy(struct Drwl *drwl) {
	pango_font_description_free(drwl->font->desc);

	g_object_unref(drwl->font->context);

	// TODO
	// yknow I could probably do this more graciously
	// like making a list of icons.
	// but also I am lazy currently. I will do it later
	//
	// "Implement it first. Then fix the issues."
	destroy_icon(&drwl->wireless.disconnected);
	destroy_icon(&drwl->wireless.good);
	destroy_icon(&drwl->wireless.okay);
	destroy_icon(&drwl->wireless.weak);
	destroy_icon(&drwl->wireless.none);

	destroy_icon(&drwl->battery.charging._10);
	destroy_icon(&drwl->battery.charging._20);
	destroy_icon(&drwl->battery.charging._30);
	destroy_icon(&drwl->battery.charging._40);
	destroy_icon(&drwl->battery.charging._50);
	destroy_icon(&drwl->battery.charging._60);
	destroy_icon(&drwl->battery.charging._70);
	destroy_icon(&drwl->battery.charging._80);
	destroy_icon(&drwl->battery.charging._90);
	destroy_icon(&drwl->battery.charging._100);

	destroy_icon(&drwl->battery.discharging._0);
	destroy_icon(&drwl->battery.discharging._20);
	destroy_icon(&drwl->battery.discharging._30);
	destroy_icon(&drwl->battery.discharging._40);
	destroy_icon(&drwl->battery.discharging._50);
	destroy_icon(&drwl->battery.discharging._60);
	destroy_icon(&drwl->battery.discharging._70);
	destroy_icon(&drwl->battery.discharging._80);
	destroy_icon(&drwl->battery.discharging._90);
	destroy_icon(&drwl->battery.discharging._100);

	free(drwl->font);
	free(drwl);
}
