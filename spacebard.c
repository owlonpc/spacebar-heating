#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#define unreachable __builtin_unreachable
#define unused      __attribute((unused))

static volatile bool spaceheld = false;

static void *
worker(unused void *arg)
{
	for (;;)
		if (!spaceheld)
			usleep(1 * 1000);

	return NULL;
}

int
main(void)
{
	int fd = open("/dev/input/event2", O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	size_t ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	pthread_t threads[ncpus];

	for (size_t i = 0; i < ncpus; i++) {
		pthread_create(&threads[i], NULL, worker, NULL);

		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(i, &cpuset);

		pthread_setaffinity_np(threads[i], sizeof cpuset, &cpuset);
	}

	struct input_event ev;

	for (;;) {
		read(fd, &ev, sizeof ev);

		if (ev.type == EV_KEY && ev.code == KEY_SPACE)
			spaceheld = ev.value;
	}

	unreachable();
}
