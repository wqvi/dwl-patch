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

#ifndef BAT0
#define BAT0 "/sys/class/power_supply/BAT0/"
#endif

#ifndef DIS
#define DIS "Discharging\n"
#endif

#ifndef CHR
#define CHR "Charging\n"
#endif

#ifndef FUL
#define FUL "Full\n"
#endif

#ifndef NOT_CHR
#define NOT_CHR "Not charging\n"
#endif

#define SYSBUFSIZ (4096)

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

	if (sysread(buffer, BAT0"capacity")) {
		return;
	}
	capacity = (int)strtol(buffer, NULL, 0);
	
	if (sysread(buffer, BAT0"status")) {
		return;
	}

	// Assuming implementation of /sys/class/power_supply/BAT0/status
	// prints "Discharging", "Charging", "Full", and "Not charging"
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

static void formatnetwork(char **c) {
	struct iwreq rq;
	int fd;
	struct sockaddr_in *in;
	char addr[INET_ADDRSTRLEN];
	char essid[48] = { 0 }; // null terminated aligned buffer
	struct iw_statistics stats;
	struct iw_range range;
	int quality = 0;
	volatile size_t size = NETOFFSET; // suppress compiler truncation warning

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
}

static void formatstatusbar(void) {
	char *ptr = stext;

	formatnetwork(&ptr);

	formatram(&ptr);

	formattemp(&ptr);

	formatbat(&ptr);

	formatdate(&ptr);
}

static void statusbar(void) {
	static time_t start = 0;
	if (time(NULL) - start > 60) {
		formatstatusbar();
		start = time(NULL);
	}
}
