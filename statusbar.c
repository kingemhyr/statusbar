#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <limits.h>

// sound
#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#define SECOND_US 1000000

useconds_t m_clock(void) {
    return clock() / (CLOCKS_PER_SEC / SECOND_US);
}

void err(const char *format, ...) {
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

int main(void) {
    char *cwd = getcwd(pathbuf, sizeof(pathbuf));  
    int error;

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
        if ((error = snd_mixer_open(&mixer, 0)) < 0) err("%s", snd_strerror(error));
        if ((error = snd_mixer_attach(mixer, card_name)) < 0) err("%s", snd_strerror(error));
        if ((error = snd_mixer_selem_register(mixer, 0, 0)) < 0) err("%s", error);
        if ((error = snd_mixer_load(mixer)) < 0) err("%s", error);
        if (!(mixer_elem = snd_mixer_find_selem(mixer, mixer_selem_id))) err("Failed to find mixer simple element.");     
        (void)snd_mixer_selem_get_playback_volume_range(mixer_elem, &min_playback_volume, &max_playback_volume);
    }

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

    useconds_t starting_time, ending_time, elapsed_time;
    const useconds_t second_us = 1000000;
    const useconds_t tick_rate = second_us / 24;
    for (;;) {
        ending_time = m_clock();
        elapsed_time = ending_time - starting_time;
        starting_time = ending_time;
        if (elapsed_time > tick_rate)
            elapsed_time = tick_rate;
        useconds_t sleeping_time = tick_rate - elapsed_time;
        usleep(sleeping_time);

        int updated = 0;

        int volume;
        {
            updated |= snd_mixer_handle_events(mixer) > 0;
            long playback_volume;
            if ((error = snd_mixer_selem_get_playback_volume(mixer_elem, 0, &playback_volume)) < 0)
                err("%s", snd_strerror(error));
            volume = round((float)(playback_volume - min_playback_volume) / (max_playback_volume - min_playback_volume) * 100);
        }

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
            switch (type) {
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

        char date[64];
        {
            static long prior_second = 0;
            time_t now = time(0);
            struct tm *tm = localtime(&now);
            updated |= tm->tm_sec != prior_second;
            if (updated) {
                date[strftime(date, sizeof(date), "%x %X", tm)] = 0;
                prior_second = tm->tm_sec;
            }
        }

        if (updated) {
            printf("volume: %i%% | power: %i%% %s | brightness: %i%% | date: %s\n", volume, power, power_status, brightness, date);
            fflush(stdout);
        }
    }
    return 0;
}
