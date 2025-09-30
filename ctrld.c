#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define unreachable __builtin_unreachable
#define unused      __attribute((unused))

#define DEBUG

#define HWMON_PATH   "/sys/class/hwmon"
#define SPIKE_SPEED  10.0    // °C/s
#define DROP_SPEED   -5.0    // °C/s
#define POLL_MS      10
#define MAX_PATH_LEN (sizeof HWMON_PATH + sizeof((struct dirent *)0)->d_name + 5)

static int
findtempsensors(char paths[][MAX_PATH_LEN], size_t maxsensors)
{
	DIR *dir = opendir(HWMON_PATH);
	if (!dir)
		return -1;

	size_t sensorcount = 0;
	struct dirent *entry;

	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.')
			continue;

		char namepath[MAX_PATH_LEN];
		snprintf(namepath, sizeof namepath, "%s/%s/name", HWMON_PATH, entry->d_name);

		FILE *f = fopen(namepath, "r");
		if (!f)
			continue;

		char name[64];
		fgets(name, sizeof name, f);
		fclose(f);

		if (strstr(name, "coretemp") || strstr(name, "k10temp")) {
			char basepath[sizeof HWMON_PATH + sizeof entry->d_name];
			snprintf(basepath, sizeof basepath, "%s/%s", HWMON_PATH, entry->d_name);

			DIR *hwmondir = opendir(basepath);
			if (!hwmondir)
				continue;

			struct dirent *tempentry;
			while ((tempentry = readdir(hwmondir))) {
				if (strncmp(tempentry->d_name, "temp", 4) == 0 && strstr(tempentry->d_name, "_input")) {
					snprintf(paths[sensorcount++], sizeof HWMON_PATH + sizeof entry->d_name + sizeof entry->d_name,
					         "%s/%s", basepath, tempentry->d_name);
					if (sensorcount >= maxsensors)
						break;
				}
			}
			closedir(hwmondir);
		}
	}

	closedir(dir);
	return sensorcount;
}

static int
readtemp(int fd)
{
	char buf[16];
	lseek(fd, 0, SEEK_SET);
	int n = read(fd, buf, sizeof buf - 1);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	return atoi(buf);
}

static double
timediff(struct timespec *start, struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

static void
sendkey(int fd, int key, int value)
{
	struct input_event ie = { 0 };

	ie.type = EV_KEY;
	ie.code = key;
	ie.value = value;
	write(fd, &ie, sizeof ie);

	ie.type = EV_SYN;
	ie.code = SYN_REPORT;
	ie.value = 0;
	write(fd, &ie, sizeof ie);
}

int
main(void)
{
	char sensorpaths[32][MAX_PATH_LEN];
	size_t sensorcount = findtempsensors(sensorpaths, 32);

	if (sensorcount <= 0) {
		fprintf(stderr, "no cpu temp sensors found\n");
		return 1;
	}

	int sensorfds[32];
	for (size_t i = 0; i < sensorcount; i++) {
		sensorfds[i] = open(sensorpaths[i], O_RDONLY);
		if (sensorfds[i] < 0) {
			fprintf(stderr, "failed to open %s\n", sensorpaths[i]);
			return 1;
		}
	}

	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_KEYBIT, KEY_LEFTCTRL);

	struct uinput_setup setup = { 0 };
	setup.id.bustype = BUS_USB;
	setup.id.vendor = 0x1;
	setup.id.product = 0x1;
	snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "ctrld");

	ioctl(fd, UI_DEV_SETUP, &setup);
	ioctl(fd, UI_DEV_CREATE);

#ifdef DEBUG
	printf("found %zu sensors\n", sensorcount);
	for (size_t i = 0; i < sensorcount; i++)
		printf("  %s\n", sensorpaths[i]);
#endif

	int prevtemps[32];
	struct timespec prevtime;

	for (size_t i = 0; i < sensorcount; i++)
		prevtemps[i] = readtemp(sensorfds[i]);

	clock_gettime(CLOCK_MONOTONIC, &prevtime);

	bool inspikestate = false;

	for (;;) {
		usleep(POLL_MS * 1000);

		struct timespec curtime;
		clock_gettime(CLOCK_MONOTONIC, &curtime);
		double dt = timediff(&prevtime, &curtime);

		size_t spikecount = 0;
		size_t dropcount = 0;
		double maxspeed = 0.0;
		int curtemps[32];

		for (size_t i = 0; i < sensorcount; i++) {
			curtemps[i] = readtemp(sensorfds[i]);
			if (curtemps[i] < 0)
				continue;

			double delta = (curtemps[i] - prevtemps[i]) / 1000.0;
			double speed = delta / dt;

			if (speed >= SPIKE_SPEED)
				spikecount++;

			if (speed <= DROP_SPEED)
				dropcount++;

			if (speed > maxspeed)
				maxspeed = speed;
		}

		if (!inspikestate && spikecount == sensorcount) {
#ifdef DEBUG

			printf("spike detected: %.1f°C/s\n", maxspeed);
#endif
			sendkey(fd, KEY_LEFTCTRL, 1);
			inspikestate = true;
		}

		if (inspikestate && dropcount == sensorcount) {
#ifdef DEBUG
			printf("temperature dropped\n");
#endif
			sendkey(fd, KEY_LEFTCTRL, 0);
			inspikestate = false;
		}

		for (size_t i = 0; i < sensorcount; i++)
			prevtemps[i] = curtemps[i];

		prevtime = curtime;
	}

	unreachable();
}
