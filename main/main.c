/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/rtc.h"
#include "pico/time.h"

// Definição dos pinos
#define TRIG_PIN 15
#define ECHO_PIN 14

// Estrutura para armazenar o estado do sensor
typedef struct
{
    alarm_id_t alarm_id;
    volatile int flag_f_trigger;
    volatile bool timer_fired;
    volatile bool action_completed;
    volatile absolute_time_t t_descida;
    volatile absolute_time_t t_subida;
} sensor_state_t;

// Estado global do sensor (necessário para callbacks de IRQ)
static sensor_state_t sensor_state = {
    .alarm_id = 0,
    .flag_f_trigger = 0,
    .timer_fired = false,
    .action_completed = false};

int64_t alarm_callback(alarm_id_t id, void *user_data)
{
    sensor_state_t *state = (sensor_state_t *)user_data;
    state->timer_fired = true;
    return 0;
}

void trigger_callback(uint gpio, uint32_t events)
{
    if (gpio == ECHO_PIN && events == 0x8)
    { // rise edge
        sensor_state.t_subida = get_absolute_time();
    }

    if (gpio == ECHO_PIN && events == 0x4 && sensor_state.flag_f_trigger)
    { // fall edge
        sensor_state.t_descida = get_absolute_time();
        sensor_state.flag_f_trigger = 0;
        if (!sensor_state.timer_fired && sensor_state.alarm_id)
        {
            cancel_alarm(sensor_state.alarm_id);
        }
        sensor_state.timer_fired = false;
        sensor_state.action_completed = true;
    }
}

void print_datetime(void)
{
    datetime_t now;
    rtc_get_datetime(&now);
    printf("%02d:%02d:%02d - ", now.hour, now.min, now.sec);
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);

    rtc_init();

    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);
    gpio_put(TRIG_PIN, 0);

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);

    gpio_set_irq_enabled_with_callback(TRIG_PIN, GPIO_IRQ_EDGE_FALL, true, trigger_callback);
    gpio_set_irq_enabled(ECHO_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);

    bool reading_active = false;
    char command[20];
    int cmd_index = 0;
    memset(command, 0, sizeof(command));

    printf("Digite 'start' para iniciar a leitura e 'stop' para parar:\n");

    const uint32_t measurement_interval_ms = 1000;
    absolute_time_t last_measurement = get_absolute_time();

    while (true)
    {
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT)
        {
            if (ch == '\n' || ch == '\r')
            {
                if (cmd_index > 0)
                {
                    command[cmd_index] = '\0';
                    if (strcmp(command, "start") == 0)
                    {
                        reading_active = true;
                        printf("Leitura iniciada!\n");
                    }
                    else if (strcmp(command, "stop") == 0)
                    {
                        reading_active = false;
                        printf("Leitura parada!\n");
                    }
                    else
                    {
                        printf("Comando desconhecido. Use 'start' ou 'stop'.\n");
                    }
                    cmd_index = 0;
                    memset(command, 0, sizeof(command));
                }
            }
            else
            {
                if (cmd_index < (int)(sizeof(command) - 1))
                {
                    command[cmd_index++] = (char)ch;
                }
            }
        }

        if (reading_active && absolute_time_diff_us(last_measurement, get_absolute_time()) >= measurement_interval_ms * 1000)
        {
            sensor_state.flag_f_trigger = true;
            sensor_state.timer_fired = false;
            sensor_state.action_completed = false;

            gpio_put(TRIG_PIN, 1);
            sleep_us(10);
            gpio_put(TRIG_PIN, 0);
            sensor_state.alarm_id = add_alarm_in_ms(500, alarm_callback, &sensor_state, false);

            absolute_time_t measure_start = get_absolute_time();
            while (!sensor_state.action_completed && !sensor_state.timer_fired)
            {
                sleep_ms(1);
                if (absolute_time_diff_us(measure_start, get_absolute_time()) > 1000000)
                    break;
            }
            cancel_alarm(sensor_state.alarm_id);

            print_datetime();
            if (sensor_state.action_completed)
            {
                int64_t pulse_duration = absolute_time_diff_us(sensor_state.t_subida, sensor_state.t_descida);
                float distance = (pulse_duration * 0.0343f) / 2.0f;
                printf("%.2f cm\n", distance);
            }
            else if (sensor_state.timer_fired)
            {
                printf("Falha\n");
            }
            else
            {
                printf("Leitura não concluída\n");
            }
            last_measurement = get_absolute_time();
        }

        sleep_ms(10);
    }

    return 0;
}