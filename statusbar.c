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

#define DATEOFFSET (24)
#define BATOFFSET (24)
#define TEMPOFFSET (16)
#define RAMOFFSET (16)
#define NETOFFSET (48)

#define BAT0 "/sys/class/power_supply/BAT0/"

#define SYSBUFSIZ (4096)

#define ADWAITA_THEME_DIR "/usr/share/icons/Adwaita/symbolic"
#define SVG_SURFACE_WIDTH (64)
#define SVG_SURFACE_HEIGHT (64)
#define SVG_SURFACE_SCALE (2.0)

#define DEG_TO_RADS(x) ((x) * (G_PI / 180.0))

static void formatdate(char **s) {
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	if (tm == NULL) return;

	// ISO Date format, 12 hours
	// YYYY-MM-DD HH:MM AM/PM
	strftime(*s, DATEOFFSET, " | %F %I:%M %p", tm);
	*s += strlen(*s);
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

static void formatbat(char **s) {
	char buffer[SYSBUFSIZ];
	int capacity;
	char status[4] = "UNK";

	if (sysread(buffer, BAT0 "capacity")) {
		return;
	}
	capacity = (int)strtol(buffer, NULL, 0);
	
	if (sysread(buffer, BAT0 "status")) {
		return;
	}

	// Assuming implementation of /sys/class/power_supply/BAT0/status
	// prints shorthand of "Discharging", "Charging", "Full", and "Not charging" (Inhibited)
	switch (buffer[0]) {
	case 'D':
		snprintf(status, 4, "DIS");
		break;
	case 'C':
		snprintf(status, 4, "CHR");
		break;
	case 'F':
		snprintf(status, 4, "FUL");
		break;
	case 'N':
		snprintf(status, 4, "INH");
		break;
	}

	snprintf(*s, BATOFFSET, " | BAT0 %d%% %s", capacity, status);
	*s += strlen(*s);
}

static void formattemp(char **s) {
	char buffer[SYSBUFSIZ];
	int temp;
	
	if (sysread(buffer, "/sys/class/thermal/thermal_zone0/temp")) {
		return;
	}

	temp = (int)(strtod(buffer, NULL) / 1000.0);

	snprintf(*s, TEMPOFFSET, " | %d\U000000B0C", temp);
	*s += strlen(*s);
}

static int cmp(const char *_haystack, const char *_needle) {
	return (strncmp(_haystack, _needle, strlen(_needle)) == 0);
}

static void formatram(char **s) {
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

	snprintf(*s, RAMOFFSET, "%.1LfGb/%.1LfGb", memused, memtotal / gb);
	*s += strlen(*s);
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

static void formatnetwork(struct network_info *info, char **c) {
	struct iwreq rq;
	int fd;
	struct sockaddr_in *in;
	char addr[INET_ADDRSTRLEN];
	char essid[48] = { 0 }; // null terminated aligned buffer
	struct iw_statistics stats;
	struct iw_range range;
	int quality = 0;
	volatile size_t size = NETOFFSET; // suppress compiler truncation warning
	
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

	// have network print the seperator.
	// I prefer the status to end without a seperator.
	snprintf(*c, size, "%s %d%% %s %s | ", essid, quality, rq.ifr_name, addr);
	*c += strlen(*c);

	// lets just default to wireless for now (this is bad lol)
	info->type = Wireless;

	memcpy(info->name, essid, strlen(essid));
	info->quality = quality;
}

void formatstatusbar(struct system_info *info, char *stext) {
	char *ptr = stext;

	formatnetwork(&info->network, &ptr);

	formatram(&ptr);

	formattemp(&ptr);

	formatbat(&ptr);

	formatdate(&ptr);
}

static void set_color(cairo_t *cr, uint32_t hex) {
	double r = ((hex >> 24) & 0xFF) / 255.0;
	double g = ((hex >> 16) & 0xFF) / 255.0;
	double b = ((hex >> 8) & 0xFF) / 255.0;
	double a = (hex & 0xFF) / 255.0;

	cairo_set_source_rgba(cr, r, g, b, a);
}

static void draw_wireless_icon(struct Drwl *drwl, struct network_info *info, int x, int y, int w, int h) {
	struct icon *icon = NULL;
	if (info->quality <= 25) {
		icon = &drwl->wireless.none;
	} else if (info->quality <= 50) {
		icon = &drwl->wireless.weak;
	} else if (info->quality <= 75) {
		icon = &drwl->wireless.okay;
	} else if (info->quality <= 100) {
		icon = &drwl->wireless.good;
	}

	render_icon(drwl, icon, x, y, w, h);
}

static void draw_network_info(struct Drwl *drwl, struct network_info *info, int x, int y, int w, int h) {
	int icon_x;
	int text_width;
	int text_x;
	switch (info->type) {
		case Disconnected:
			render_icon(drwl, &drwl->wireless.disconnected, x, y, w, h);
			break;
		case Wireless:
			icon_x = x - w + (int)(SVG_SURFACE_SCALE / 2.0);
			text_width = drwl_font_getwidth(drwl, info->name);
			text_x = x - w - text_width;

			set_color(drwl->context, drwl->scheme[ColFg]);
			drwl_rounded_rect(drwl, text_x, y, w + text_width, h, 4);
			draw_wireless_icon(drwl, info, icon_x, y, w, h);
			drwl_text(drwl, text_x, y, text_width, 0, 2, info->name, 1);
			break;
		default:
			return;
	}
}

void draw_system_info(struct Drwl *drwl, struct system_info *info, int x, int y) {
	draw_network_info(drwl, &info->network, x, y, 16, 16);
}

static void load_icon(const char *file, struct icon *icon) {	
	GError *error = NULL;
	icon->handle = rsvg_handle_new_from_file(file, &error);
	if (error) {
		fprintf(stderr, "Error loading icon: %s\n", error->message);
		return;
	}

	icon->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, SVG_SURFACE_WIDTH, SVG_SURFACE_HEIGHT);
	icon->context = cairo_create(icon->surface);
	icon->viewport.x = 0.0;
	icon->viewport.y = 0.0;
}

struct Drwl *drwl_create(const char *font) {
	struct Drwl *drwl;
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

	// Create a pango context from default cairo font map
	font_map = pango_cairo_font_map_get_default();
	drwl->pango_context = pango_font_map_create_context(font_map);

	drwl->pango_description = pango_font_description_from_string(font);
	// Get font metrics and use the metrics to get the font height
	metrics = pango_context_get_metrics(drwl->pango_context, drwl->pango_description, NULL);
	font_height = (float)pango_font_metrics_get_height(metrics) / (float)PANGO_SCALE;
	drwl->font_height = (unsigned int)font_height;

	// load all the icons necessary for wireless networks
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-disabled-symbolic.svg", &drwl->wireless.disconnected);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-good-symbolic.svg", &drwl->wireless.good);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-ok-symbolic.svg", &drwl->wireless.okay);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-weak-symbolic.svg", &drwl->wireless.weak);
	load_icon(ADWAITA_THEME_DIR "/status/network-wireless-signal-none-symbolic.svg", &drwl->wireless.none);

	pango_font_metrics_unref(metrics);

	return drwl;
}

void drwl_prepare_drawing(struct Drwl *drwl, int w, int h, int stride, unsigned char *data) {
	// create all the necessary information to write to the wayland buffer
	drwl->surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, w, h, stride);
	drwl->context = cairo_create(drwl->surface);
	drwl->pango_layout = pango_layout_new(drwl->pango_context);
	pango_layout_set_font_description(drwl->pango_layout, drwl->pango_description);
}

void drwl_rect(struct Drwl *drwl,
		int x, int y, unsigned int w, unsigned int h,
		int filled, int invert) {
	uint32_t clr = drwl->scheme[invert ? ColBg : ColFg];
	set_color(drwl->context, clr);
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

void drwl_rounded_rect(struct Drwl *drwl,
		double x, double y, unsigned int w, unsigned int h,
		double radius) {
	cairo_t *cr = drwl->context;

	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - radius, y + radius, radius, DEG_TO_RADS(-90), 0);
	cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, DEG_TO_RADS(90));
	cairo_arc(cr, x + radius, y + h - radius, radius, DEG_TO_RADS(90), DEG_TO_RADS(180));
	cairo_arc(cr, x + radius, y + radius, radius, DEG_TO_RADS(180), DEG_TO_RADS(270));
	cairo_close_path(cr);

	cairo_fill(cr);
}

void render_icon(struct Drwl *drwl, struct icon *icon, double x, double y, int w, int h) {
	GError *error = NULL;

	icon->viewport.width = w - SVG_SURFACE_SCALE;
	icon->viewport.height = h - SVG_SURFACE_SCALE;

	if (!rsvg_handle_render_document(icon->handle, icon->context, &icon->viewport, &error)) {
		fprintf(stderr, "Could not render svg: %s\n", error->message);
		return;
	}

	// render surface to target context
	cairo_set_source_surface(drwl->context, icon->surface, x, y + SVG_SURFACE_SCALE / 2);
	cairo_paint(drwl->context);
}

int drwl_text(struct Drwl *drwl,
		int x, int y, int w, int h,
		unsigned int lpad, const char *text, int invert) {
	PangoRectangle rect;
	float surface_height;
	float height;
	float text_y;
	int render = x || y || w || h;
	uint32_t clr = drwl->scheme[ColFg];

	if (!render) {
		w = invert ? invert : ~invert;
	} else {
		clr = drwl->scheme[invert ? ColBg : ColFg];
		set_color(drwl->context, clr);

		drwl_rect(drwl, x, y, w, h, 1, !invert);

		x += lpad;
		w -= lpad;
	}

	// set current color, in this case for the font
	// this is to emulate the pixman_image_composite32 operations
	// as that's how the previous implementation colored
	// the text
	set_color(drwl->context, clr);

	// calculate the position to center the text
	pango_layout_get_extents(drwl->pango_layout, NULL, &rect);
	pango_layout_set_text(drwl->pango_layout, text, -1);
	surface_height = (float)cairo_image_surface_get_height(drwl->surface);
	// wow pango library is annoying since I have
	// to divide every value from the library by the
	// PANGO_SCALE constant
	height = rect.height / PANGO_SCALE;

	// actually calculate the center of the y axis
	text_y = (surface_height - height) / 2.0f;

	// render the text
	cairo_move_to(drwl->context, x, text_y);
	pango_cairo_show_layout(drwl->context, drwl->pango_layout);

	return x + (render ? w : 0);
}

unsigned int drwl_font_getwidth(struct Drwl *drwl, const char *text) {
	PangoRectangle extent;
	pango_layout_set_text(drwl->pango_layout, text, -1);
	pango_layout_get_extents(drwl->pango_layout, NULL, &extent);
	return (unsigned int)extent.width / PANGO_SCALE;
}

void drwl_finish_drawing(struct Drwl *drwl) {
	g_object_unref(drwl->pango_layout);
	cairo_destroy(drwl->context);
	cairo_surface_destroy(drwl->surface);
}

void destroy_icon(struct icon *icon) {
	cairo_destroy(icon->context);
	cairo_surface_destroy(icon->surface);
	g_object_unref(icon->handle);
}

void drwl_destroy(struct Drwl *drwl) {
	pango_font_description_free(drwl->pango_description);

	g_object_unref(drwl->pango_context);

	destroy_icon(&drwl->wireless.disconnected);
	destroy_icon(&drwl->wireless.good);
	destroy_icon(&drwl->wireless.okay);
	destroy_icon(&drwl->wireless.weak);
	destroy_icon(&drwl->wireless.none);

	free(drwl);
}
