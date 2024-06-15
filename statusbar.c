#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// sound
#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#define SECOND_US 1000000

#define free(p) ({assert(p); free(p);})

useconds_t m_clock(void)
{
	return clock() / (CLOCKS_PER_SEC / SECOND_US);
}

void err(const char *format, ...)
{
	fflush(stdout);
	va_list vargs;
	va_start(vargs, format);
	fprintf(stderr, "Error: ");
	vfprintf(stderr, format, vargs);
	fprintf(stderr, "\n");
	fflush(stderr);
	va_end(vargs);
	exit(-1);
}

char pathbuf[PATH_MAX];

struct cpu_info
{
	int user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
	int total_idle, total_active;
};

void get_cpu_info(FILE *cpu_file, struct cpu_info *info)
{
	char cpu[] = "cpu";
	struct cpu_info i;
	fscanf(cpu_file, "%s %i %i %i %i %i %i %i %i %i %i", cpu, &i.user, &i.nice, &i.system, &i.idle, &i.iowait, &i.irq, &i.softirq, &i.steal, &i.guest, &i.guest_nice);
	i.total_idle = i.idle + i.iowait;
	i.total_active = i.user + i.nice + i.system + i.irq + i.softirq + i.steal + i.guest + i.guest_nice;
	*info = i;
	// printf("%i %i %i %i %i %i %i %i %i %i\n", i.user, i.nice, i.system, i.idle, i.iowait, i.irq, i.softirq, i.steal, i.guest, i.guest_nice);
	rewind(cpu_file);
}

int main(void)
{
	sleep(1); // wait for OS stuff to load first

	char *cwd = getcwd(pathbuf, sizeof(pathbuf));  
	int error;

	// volume
	snd_mixer_t *mixer;
	snd_mixer_elem_t *mixer_elem;
	long min_playback_volume, max_playback_volume;
	{
		snd_mixer_selem_id_t *mixer_selem_id;
		static int mixer_index = 0;
		static const char mixer_name[] = "Master";
		static const char card_name[] = "default";
		snd_mixer_selem_id_alloca(&mixer_selem_id);
		snd_mixer_selem_id_set_index(mixer_selem_id, mixer_index);
		snd_mixer_selem_id_set_name(mixer_selem_id, mixer_name);
		if ((error = snd_mixer_open(&mixer, 0)) < 0)                     { err("%s", snd_strerror(error)); }
		if ((error = snd_mixer_attach(mixer, card_name)) < 0)            { err("%s", snd_strerror(error)); }
		if ((error = snd_mixer_selem_register(mixer, 0, 0)) < 0)         { err("%s", error); }
		if ((error = snd_mixer_load(mixer)) < 0)                         { err("%s", error); }
		if (!(mixer_elem = snd_mixer_find_selem(mixer, mixer_selem_id))) { err("Failed to find mixer simple element.");	}
		(void)snd_mixer_selem_get_playback_volume_range(mixer_elem, &min_playback_volume, &max_playback_volume);
	}

	// ram, swap
#define SwapFree_LINE_NUMBER 16
#define MemAvailable_LINE_NUMBER 3
	int total_mem, total_swap;
	FILE *memory_file;
	{
		memory_file = fopen("/proc/meminfo", "rm");
		setbuf(memory_file, 0);
		char buf[64];
		fscanf(memory_file, "%s %i", buf, &total_mem);
		for (int i = 0; i < SwapFree_LINE_NUMBER - 2; ++i)
		{
			char *line = 0;
			size_t n;
			getline(&line, &n, memory_file);
			free(line);
		}
		fscanf(memory_file, "%s %i", buf, &total_swap);
		rewind(memory_file);
	}

	// temp
	FILE *temp_file;
	{
		static const char dirpath[] = "/sys/class/thermal"; 
		DIR *dir = opendir(dirpath);
		char pathbuf[PATH_MAX];
		int pathlen;
		int found = 0;
		for (;;)
		{
			struct dirent *ent = readdir(dir);
			if (!ent)
				break;
			if (strstr(ent->d_name, "thermal_zone"))
			{
				pathlen = sprintf(pathbuf, "%s/%s/type", dirpath, ent->d_name);
				FILE *f = fopen(pathbuf, "rm");
				char *line = 0;
				size_t n;
				getline(&line, &n, f);
				fclose(f);
				*strchr(line, '\n') = 0;
				found = strcmp(line, "x86_pkg_temp") == 0;
				free(line);
				if (found)
				{
					memcpy(&pathbuf[pathlen - sizeof("type") + 1], "temp", 4);
					temp_file = fopen(pathbuf, "rm");
					setbuf(temp_file, 0);
					break;
				}
			}
		}
		closedir(dir);
		if (!found)
		{
			fprintf(stderr, "Failed to find good thermal_zone.");
			exit(-1);
		}
	}

	// power
	int max_power;
	FILE *charge_now_file;
	FILE *status_file;
	{
		chdir("/sys/class/power_supply/BAT0");
		FILE *charge_full_file = fopen("charge_full", "r");
		char *line = 0;
		size_t n;
		(void)getline(&line, &n, charge_full_file);
		fclose(charge_full_file);
		max_power = atoi(line);
		free(line);
		charge_now_file = fopen("charge_now", "rm");
		status_file = fopen("status", "rm");
		setbuf(charge_now_file, 0);
		setbuf(status_file, 0);
		chdir(cwd);
	}

	// cpu
	FILE *cpu_file;
	{
		cpu_file = fopen("/proc/stat", "rm");
		setbuf(cpu_file, 0);
	}

	// brightness
	int max_brightness;
	FILE *brightness_file;
	{
		chdir("/sys/class/backlight/intel_backlight");
		FILE *max_brightness_file = fopen("max_brightness", "r");
		char *line = 0;
		size_t n;
		(void)getline(&line, &n, max_brightness_file);
		fclose(max_brightness_file);
		max_brightness = atoi(line);
		free(line);
		brightness_file = fopen("brightness", "rm");
		setbuf(brightness_file, 0);
		chdir(cwd);
	}

	// main loop
	useconds_t starting_time, ending_time, elapsed_time;
	const useconds_t second_us = 1000000;
	const int ticks_per_sec = 12;
	const useconds_t tick_rate = second_us / ticks_per_sec;
	for (;;)
	{
		int second_passed = 0;

		// timing
		ending_time = m_clock();
		elapsed_time = ending_time - starting_time;
		starting_time = ending_time;
		if (elapsed_time > tick_rate)
			elapsed_time = tick_rate;
		useconds_t sleeping_time = tick_rate - elapsed_time;
		usleep(sleeping_time); // save power

		int updated = 0;

		// date
		char date[64];
		{
			static long prior_second = 0;
			time_t now = time(0);
			struct tm *tm = localtime(&now);
			second_passed = tm->tm_sec != prior_second;
			updated |= second_passed;
			if (updated)
			{
				date[strftime(date, sizeof(date), "%x %X", tm)] = 0;
				prior_second = tm->tm_sec;
			}
		}

		// volume
		char sound[8];
		{
			static long prior_playback_volume = 0;
			static long prior_muted = 0;
			snd_mixer_handle_events(mixer);
			long playback_volume;
			if ((error = snd_mixer_selem_get_playback_volume(mixer_elem, 0, &playback_volume)) < 0)
				err("%s", snd_strerror(error));
			prior_playback_volume = playback_volume;
			int volumeval = round((float)(playback_volume - min_playback_volume) / (max_playback_volume - min_playback_volume) * 100);
			int muted;
			snd_mixer_selem_get_playback_switch(mixer_elem, SND_MIXER_SCHN_MONO, &muted);
			prior_muted = muted;
			if (!muted)
				strcpy(sound, "mute");
			else
				sprintf(sound, "%i%%", volumeval);
			updated |= (muted != prior_muted) || (playback_volume != prior_playback_volume);
		}

		// power
		int power;
		char *power_status;
		{
			static int prior_power = -1;
			static char *prior_status = 0;

			char *line = 0;
			size_t n;
			(void)getline(&line, &n, charge_now_file);
			rewind(charge_now_file);
			power = atoi(line);
			free(line);			  
			power = round((float)power / max_power) * 100;

			line = 0;
			(void)getline(&line, &n, status_file);
			rewind(status_file);
			char type = line[0];
			free(line);
			switch (type)
			{
			case 'U':
				power_status = "unknown";
				break;
			case 'C':
				power_status = "charging";
				break;
			case 'D':
				power_status = "discharging";
				break;
			case 'N':
			case 'F':
				power_status = "charged";
				break;
			}
			updated |= prior_power != power;
			updated |= prior_status != power_status;
			prior_power = power;
			prior_status = power_status;
		}

		// temp
		int temp;
		{
			static int prior_temp = 0;
			fscanf(temp_file, "%i", &temp);
			rewind(temp_file);
			temp /= 1000;
			updated |= prior_temp != temp;
			prior_temp = temp;
		}

		// cpu
		int cpu;
		{
			static struct cpu_info prior_info = {0};
			struct cpu_info info;
			if (second_passed)
			{
				get_cpu_info(cpu_file, &info);
				int active = info.total_active - prior_info.total_active;
				int idle = info.total_idle - prior_info.total_idle;
				int total = active + idle;
				cpu = round((float)active / total * 100);
				prior_info = info;
			}
		}

		// memory and swap
		int mem_perc, swap_perc;
		{
			char buf[64];
			for (int i = 0; i < MemAvailable_LINE_NUMBER - 1; ++i)
			{
				char *line = 0;
				size_t n;
				getline(&line, &n, memory_file);
				free(line);
			}
			int i;
			fscanf(memory_file, "%s %i", buf, &i);
			mem_perc = round((float)(total_mem - i) / total_mem * 100);
			
			for (int i = 0; i < (SwapFree_LINE_NUMBER - MemAvailable_LINE_NUMBER) - 1; ++i)
			{
				char *line = 0;
				size_t n;
				getline(&line, &n, memory_file);
				free(line);
			}
			fscanf(memory_file, "%s %i", buf, &i);
			swap_perc = round((float)(total_swap - i) / total_swap * 100);
			rewind(memory_file);
		}

		// brightness
		int brightness;
		{
			static int prior_brightness = -1;
			char *line = 0;
			size_t n;
			(void)getline(&line, &n, brightness_file);
			rewind(brightness_file);
			brightness = atoi(line);
			free(line);
			brightness = round((float)brightness / max_brightness * 100);
			updated |= prior_brightness != brightness;
			prior_brightness = brightness;
		}

		// print
		if (updated)
		{
			printf("sound: %s | power: %i%% %s | cpu: %i%% | temp: %iC | ram: %i%% | swap: %i%% | brightness: %i%% | date: %s\n", sound, power, power_status, cpu, temp, mem_perc, swap_perc, brightness, date);
			fflush(stdout);
		}
	}
	return 0;
}
