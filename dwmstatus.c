/*
 * Copy me if you can.
 * by 20h
 */

#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/statvfs.h>

#include <X11/Xlib.h>

char *tzrome = "Europe/Rome";

static Display *dpy;

char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL)
		return smprintf("");

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		return smprintf("");
	}

	return smprintf("%s", buf);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0)
		return smprintf("");

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

char *
readfile(char *base, char *file)
{
	char *path, line[513];
	FILE *fd;

	memset(line, 0, sizeof(line));

	path = smprintf("%s/%s", base, file);
	fd = fopen(path, "r");
	free(path);
	if (fd == NULL)
		return NULL;

	if (fgets(line, sizeof(line)-1, fd) == NULL)
		return NULL;
	fclose(fd);

	return smprintf("%s", line);
}

char *
getbattery(char *base)
{
	char *co, status;
	int descap, remcap;

	descap = -1;
	remcap = -1;

	co = readfile(base, "present");
	if (co == NULL)
		return smprintf("");
	if (co[0] != '1') {
		free(co);
		return smprintf("not present");
	}
	free(co);

	co = readfile(base, "charge_full_design");
	if (co == NULL) {
		co = readfile(base, "energy_full_design");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &descap);
	free(co);

	co = readfile(base, "charge_now");
	if (co == NULL) {
		co = readfile(base, "energy_now");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &remcap);
	free(co);

	co = readfile(base, "status");
	if (!strncmp(co, "Discharging", 11)) {
		status = '-';
	} else if(!strncmp(co, "Charging", 8)) {
		status = '+';
	} else {
		status = '?';
	}

	if (remcap < 0 || descap < 0)
		return smprintf("invalid");

	return smprintf("%.0f%%%c", ((float)remcap / (float)descap) * 100, status);
}

float 
gettemperature(char *base, char *sensor)
{
	char *co;

	co = readfile(base, sensor);
	if (co == NULL)
		return 0.0;

	return atof(co) / 1000;
}

int
parse_proc_net_dev(unsigned long long int *receivedabs, unsigned long long int *sentabs)
{
	char buf[255];
	char *datastart;
	static int bufsize;
	int rval;
	FILE *devfd;
	unsigned long long int receivedacc, sentacc;

	*receivedabs = 0;
	*sentabs = 0;

	bufsize = 255;
	devfd = fopen("/proc/net/dev", "r");
	rval = 1;

	// Ignore the first two lines of the file
	fgets(buf, bufsize, devfd);
	fgets(buf, bufsize, devfd);

	while (fgets(buf, bufsize, devfd)) {
			if ((datastart = strstr(buf, "lo:")) == NULL) {
		datastart = strstr(buf, ":");

		// With thanks to the conky project at http://conky.sourceforge.net/
		sscanf(datastart + 1, "%llu	 %*d		 %*d	%*d	 %*d	%*d		%*d				 %*d			 %llu",\
					 &receivedacc, &sentacc);
		*receivedabs += receivedacc;
		*sentabs += sentacc;
		rval = 0;
			}
	}

	fclose(devfd);
	return rval;
}

int
parse_proc_stat(unsigned long long int *proctime, unsigned long long int *idletime)
{
	char buf[255];
	FILE *devfd;
	unsigned long long int times[9];
	int i;

	devfd = fopen("/proc/stat", "r");

	if (fgets(buf, sizeof(buf), devfd) == NULL) {
		return 1;
	}

	sscanf(buf, "cpu	%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
					&times[0], &times[1], &times[2], idletime, &times[3], &times[4], &times[5], &times[6], &times[7], &times[8]);

	for (i = 0, *proctime = 0; i < 9; i++) {
		*proctime += times[i];
	}


	fclose(devfd);
	return 0;
}

int
parse_proc_meminfo(unsigned long long int *totalmem, unsigned long long int *usedmem)
{
	char buf[255];
	FILE *devfd;
	unsigned long long int availmem;

	devfd = fopen("/proc/meminfo", "r");

	if (fgets(buf, sizeof(buf), devfd) == NULL) {
		return 1;
	}

	sscanf(buf, "MemTotal:       %llu kB", totalmem);

	if (fgets(buf, sizeof(buf), devfd) == NULL) {
		return 1;
	}
  
	if (fgets(buf, sizeof(buf), devfd) == NULL) {
		return 1;
	}

	sscanf(buf, "MemAvailable:   %llu kB", &availmem);

  *usedmem = *totalmem - availmem;

	fclose(devfd);
	return 0;
}

void
get_freespace(unsigned long long int *totaldisk, unsigned long long int *useddisk) {
    struct statvfs data;

    if ( (statvfs("/", &data)) < 0) {
		  fprintf(stderr, "can't get info on disk.\n");
		  return;
    }

    *totaldisk = data.f_blocks * data.f_frsize;
    *useddisk = (data.f_blocks - data.f_bfree) * data.f_frsize;
}

int
main(void)
{
	char *status;
	//char *avgs;
	char *time;
	unsigned int counter;
	unsigned long long int received;
	unsigned long long int sent;
	unsigned long long int proctime1;
	unsigned long long int proctime2;
	unsigned long long int idletime1;
	unsigned long long int idletime2;
	unsigned long long int totalmem;
	unsigned long long int usedmem;
	unsigned long long int totaldisk;
	unsigned long long int useddisk;
	double cpu;
  float cputemp;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	proctime1 = 0;
	idletime1 = 0;

	for (counter = 0;; sleep(1), counter++) {
		time = mktimes(" %a %d %b %Y  %H:%M ", tzrome);
		parse_proc_net_dev(&received, &sent);

		if ((counter % 5) == 0) {
			parse_proc_stat(&proctime2, &idletime2);
			cpu = ((double)(proctime2 - proctime1)/(double)(idletime2 - idletime1)) * 100;
			proctime1 = proctime2;
			idletime1 = idletime2;

      parse_proc_meminfo(&totalmem, &usedmem);

      cputemp = gettemperature("/sys/class/hwmon/hwmon0", "temp1_input");
		}

		if ((counter % 600) == 0) {
      get_freespace(&totaldisk, &useddisk);
		}

		status = smprintf("^b#1a1b26^^c#24283b^\ue0b2^c#73daca^^b#24283b^  %.2fMB  %.2fMB ^c#1a1b26^\ue0b2^c#7aa2f7^^b#1a1b26^  %.2f% ^c#24283b^\ue0b2^c#bb9af7^^b#24283b^  %02.2f°C ^c#1a1b26^\ue0b2^c#9ece6a^^b#1a1b26^  %.2fGB/%.2fGB ^c#24283b^\ue0b2^c#e0af78^^b#24283b^  %.2fGB/%.2fGB ^c#1a1b26^\ue0b2^c#2ac3ce^^b#1a1b26^ %s", received / 1048576.0 , sent / 1048576.0, cpu, cputemp, usedmem / 1048576.0, totalmem / 1048576.0, useddisk / 1073741824.0, totaldisk / 1073741824.0, time);
		setstatus(status);

		free(time);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}

