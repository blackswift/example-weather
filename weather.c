/*
 * weather.c
 *
 *  Created on: May 27, 2015
 *      Author: openwrt
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

//#define SIG_TIMER_IRQ	(SIGRTMIN+11)	// SIGRTMIN is different in Kernel and User modes
#define SIG_TIMER_IRQ	43				// So we have to hardcode this value
#define SIG_DHT_IRQ		44

#define TIMER_TIMER 1
#define DHT_TIMER 2
#define DHT_GPIO 23

////////////////////////////////////////////////////////////////////////////////

static unsigned int counter = 0;
static int failcounter = 0;
static char buf[255];

float minute[2][60];
float hour[2][24*4]; // every 15 minutes
float week[2][24*7]; // every hour

#define MINUTE_FILE "/www/light/data/minute.dat"
#define HOUR_FILE "/www/light/data/hour.dat"
#define WEEK_FILE "/www/light/data/week.dat"

#define MINUTE_IMG "/www/light/img/last_60m.png"
#define HOUR_IMG "/www/light/img/last_24h.png"
#define WEEK_IMG "/www/light/img/last_7d.png"

#define GNUPLOT_SETTINGS "/www/light/data/gnuplot.cfg"

#define EXT_01M_CMD "/root/weather.sh"
#define EXT_15M_CMD "/usr/bin/php-cgi /root/narodmon.php"
// #define EXT_60M_CMD ""

//////////////////////////////////////////////////////////////////////////

void parse_data_files(char* filename, int num, float (*data)[num])
{
	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	FILE *fd;
	int count = 0;
	int index = 0;

	fd = fopen(filename, "r");
	if (fd != NULL)
	{
		while ((read = getline(&line, &len, fd)) != -1)
		{
			if(isdigit(line[0]))
			{
				if (count < num)
				{
					data[index][count] = atof(line);
				}
				count++;
			}
			else
			{
				if(strncmp(line, "e\n", 2) == 0)
				{
					index = 1;
					count = 0;
				}
			}
		}
		free(line);
		fclose(fd);
	}
}

//////////////////////////////////////////////////////////////////////////

void load_data_files()
{
	parse_data_files(MINUTE_FILE, 60, minute);
	parse_data_files(HOUR_FILE, 24*4, hour);
	parse_data_files(WEEK_FILE, 24*7, week);
}

//////////////////////////////////////////////////////////////////////////

void output_data(char* filename, char* imgname, int num, float (*data)[num])
{
	int fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT);

	sprintf(buf, "set terminal png medium size 600,300\n");
	write(fd, buf, strlen(buf));
	sprintf(buf, "set format x \"\"\n");
	write(fd, buf, strlen(buf));
	sprintf(buf, "set key below\n");
	write(fd, buf, strlen(buf));
	sprintf(buf, "set xrange [%d:0]\n", num);
	write(fd, buf, strlen(buf));
	sprintf(buf, "set yrange [0:100]\n");
	write(fd, buf, strlen(buf));
	sprintf(buf, "set grid ytics lc rgb \"#bbbbbb\" lw 1 lt 0\n");
	write(fd, buf, strlen(buf));
	sprintf(buf, "set grid xtics lc rgb \"#bbbbbb\" lw 1 lt 0\n");
	write(fd, buf, strlen(buf));
	sprintf(buf, "set output \"%s\"\n", imgname);
	write(fd, buf, strlen(buf));
	sprintf(buf, "plot \"-\" using 0:1 title \"Temperature\" with lines, \"-\" using 0:1 title \"Humidity\" with lines\n");
	write(fd, buf, strlen(buf));


	int i, k = 0;
	for (k=0; k<2; k++)
	{
		for (i=0; i<num; i++)
		{
			if ((data[0][i] != 0) && (data[1][i] != 0))
			{
				sprintf(buf, "%g\n", data[k][i]); // temperature - humidity
				write(fd, buf, strlen(buf));
			}
		}
		sprintf(buf, "e\n");
		write(fd, buf, strlen(buf));
	}
	close(fd);

	if (imgname != NULL)
	{
		sprintf(buf, "/usr/bin/gnuplot %s &", filename);
		system(buf);
	}
}

//////////////////////////////////////////////////////////////////////////

void dht_request_data()
{
	int fd=open("/sys/kernel/debug/irq-dht", O_WRONLY);
	//sprintf(buf, "%d %d %u", DHT_TIMER, DHT_GPIO, getpid()); // gpio-irq-dht kernel module
	sprintf(buf, "%d %u", DHT_GPIO, getpid()); // gpio-dht-handler kernel module
	write(fd, buf, strlen(buf) + 1);
	close(fd);
}

//////////////////////////////////////////////////////////////////////////

void dht_handler(int n, siginfo_t *info, void *unused)
{
	if (info->si_int == 0) // DHT protocol CRC error
	{
		if (++failcounter > 10)
		{
			printf("Error retrieving data from DHT sensor\n");
			failcounter = 0;
			return;
		}
		usleep(3000000); // wait 3 seconds between attempts
		dht_request_data();
		return;
	}

	float humidity = (float)((info->si_int) >> 16)/10.0; // 2xMSB
	float temperature = (float)((info->si_int) & 0xFFFF)/10.0; //2xLSB

	if ((humidity > 100) || (temperature > 80))
	{
		// Obviously incorrect data
		if (++failcounter > 10)
		{
			printf("Incorrect DHT data with correct CRC. Wow!\n");
			failcounter = 0;
			return;
		}
		usleep(3000); // wait 3 seconds between attempts
		dht_request_data();
		return;
	}

	time_t rawtime;
	struct tm * timeinfo;
	time (&rawtime);
	timeinfo = localtime (&rawtime);
	char stime[50];
	strftime (stime, 50,"%a %B %e %H:%M", timeinfo);

	#ifdef EXT_01M_CMD
		sprintf(buf, "%s %g %g \"%s\" %d &", EXT_01M_CMD, temperature, humidity, stime, counter);
		system(buf);
	#endif

	failcounter = 0;

	int k = 0;
	int i = 0;

	for (k=0; k<2; k++)
	{
		for (i=59; i>0; i--)
		{
			minute[k][i] = minute[k][i-1];
		}
	}
	minute[0][0] = temperature;
	minute[1][0] = humidity;

	output_data(MINUTE_FILE, MINUTE_IMG, 60, minute);

	if (counter%15 == 0) // 15 minutes
	{
		for (k=0; k<2; k++)
		{
			for (i=24*4-1; i>0; i--)
			{
				hour[k][i] = hour[k][i-1];
			}

			hour[k][0] = 0;
			for (i=0; i<15; i++) // 15 minutes average
			{
				hour[k][0] += minute[k][i];
			}
			hour[k][0] /= 15.0;
		}

		#ifdef EXT_15M_CMD
			sprintf(buf, "%s %g %g \"%s\" %d &", EXT_15M_CMD, temperature, humidity, stime, counter);
			system(buf);
		#endif

		output_data(HOUR_FILE, HOUR_IMG, 24*4, hour);
	}

	if (counter%60 == 0) // 1 hour
	{
		for (k=0; k<2; k++)
		{
			for (i=24*7-1; i>0; i--)
			{
				week[k][i] = week[k][i-1];
			}

			week[k][0] = 0;
			for (i=0; i<60; i++) // 60 minutes average
			{
				week[k][0] += minute[k][i];
			}
			week[k][0] /= 60.0;
		}

		#ifdef EXT_60M_CMD
			sprintf(buf, "%s %g %g \"%s\" %d &", EXT_60M_CMD, temperature, humidity, stime, counter);
			system(buf);
		#endif

		counter = 0;
		output_data(WEEK_FILE, WEEK_IMG, 24*7, week);
	}
}

//////////////////////////////////////////////////////////////////////////

void irq_handler(int n, siginfo_t *info, void *unused)
{
	//every minute
	counter++;
	dht_request_data();
}

////////////////////////////////////////////////////////////////////////////////

bool init_handler(int timer, int tick, unsigned int timeout)
{
	int fd;

	struct sigaction sig;
	sig.sa_sigaction=irq_handler;
	sig.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigaction(SIG_TIMER_IRQ, &sig, NULL);

	fd = open("/sys/kernel/debug/timer-irq", O_WRONLY);
	if(fd < 0)
	{
		perror("open");
		return false;
	}

	sprintf(buf, "+ %d %u\n+ %d %u %u", timer, tick, timer, timeout, getpid());

	if(write(fd, buf, strlen(buf) + 1) < 0)
	{
		perror("write");
		close(fd);
		return false;
	}

	close(fd);
	return true;
}

////////////////////////////////////////////////////////////////////////////////

bool remove_handler(int timer)
{
	int fd;

	fd = open("/sys/kernel/debug/timer-irq", O_WRONLY);
	if(fd < 0)
	{
		perror("open");
		return false;
	}

	sprintf(buf, "- %d", timer);

	if(write(fd, buf, strlen(buf) + 1) < 0)
	{
		perror("write");
		close(fd);
		return false;
	}

	close(fd);
	return true;
}

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
	int timer = TIMER_TIMER;
	int tick = 1000*1000; // 1 sec
	unsigned int timeout = 1000*1000*60; // 60 sec

	if (!init_handler(timer, tick, timeout))
	{
		printf("Error initializing timer %d\n", timer);
		return -1;
	}

	struct sigaction sig;
	sig.sa_sigaction = dht_handler;
	sig.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigaction(SIG_DHT_IRQ, &sig, NULL);

	sigset_t sigset;
	siginfo_t siginfo;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);	//	Ctrl+C
	sigprocmask(SIG_BLOCK, &sigset, NULL);

	load_data_files();

	counter++;
	dht_request_data();

	while (1)
	{
		sigwaitinfo(&sigset, &siginfo);
		if(siginfo.si_signo == SIGINT)
		{
			break;
		}
	}

	remove_handler(timer);
	return 0;
}
