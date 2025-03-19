/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/rtc.h"
#include <string.h>

#define TRIGGER_PIN 28
#define ECHO_PIN 27
#define MEASUREMENT_TIMEOUT_US 30000
#define CMD_BUFFER_SIZE 64

// Definições para as flags IRQ
#define GPIO_IRQ_EDGE_RISE 0x8
#define GPIO_IRQ_EDGE_FALL 0x4

typedef enum
{
    STATE_IDLE = 0,
    STATE_WAITING_FOR_ECHO,
    STATE_MEASUREMENT_COMPLETE,
    STATE_MEASUREMENT_ERROR
} measurement_state_t;

// Estrutura para armazenar o estado do sistema
typedef struct
{
    measurement_state_t current_state;
    absolute_time_t echo_start_time;
    absolute_time_t echo_end_time;
    alarm_id_t measurement_alarm_id;
    bool system_running;

    // Variáveis para processamento de comandos
    char cmd_buffer[CMD_BUFFER_SIZE];
    int cmd_index;
} system_state_t;

// Instância global da estrutura de estado (necessária para callbacks e IRQs)
system_state_t g_state = {
    .current_state = STATE_IDLE,
    .system_running = false,
    .measurement_alarm_id = 0,
    .cmd_index = 0};

// Estrutura para datetime do RTC
typedef struct
{
    int16_t year;
    int8_t month;
    int8_t day;
    int8_t dotw;
    int8_t hour;
    int8_t min;
    int8_t sec;
} datetime_t;

int64_t timeout_callback(alarm_id_t id, void *user_data)
{
    system_state_t *state = (system_state_t *)user_data;
    if (state->current_state == STATE_WAITING_FOR_ECHO)
    {
        state->current_state = STATE_MEASUREMENT_ERROR;
    }
    return 0;
}

void echo_callback(uint gpio, uint32_t events)
{
    if (gpio == ECHO_PIN)
    {
        if (events & GPIO_IRQ_EDGE_RISE)
        {
            g_state.echo_start_time = get_absolute_time();
        }
        if (events & GPIO_IRQ_EDGE_FALL)
        {
            g_state.echo_end_time = get_absolute_time();
            if (g_state.current_state == STATE_WAITING_FOR_ECHO)
            {
                cancel_alarm(g_state.measurement_alarm_id);
                g_state.current_state = STATE_MEASUREMENT_COMPLETE;
            }
        }
    }
}

void send_trigger_pulse()
{
    gpio_put(TRIGGER_PIN, 1);
    sleep_us(10);
    gpio_put(TRIGGER_PIN, 0);
}

int main()
{
    stdio_init_all();

    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_put(TRIGGER_PIN, 0);

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);

    gpio_disable_pulls(ECHO_PIN);

    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &echo_callback);

    rtc_init();

    datetime_t t = {
        .year = 2025,
        .month = 3,
        .day = 19,
        .dotw = 3,
        .hour = 10,
        .min = 0,
        .sec = 0};
    rtc_set_datetime(&t);

    printf("Sistema iniciado.\n");
    printf("Digite 'start' para iniciar as medições e 'stop' para parar.\n");

    while (true)
    {
        int c = getchar_timeout_us(1000);

        if (c != PICO_ERROR_TIMEOUT)
        {
            putchar(c);

            if (c == '\r' || c == '\n')
            {
                if (g_state.cmd_index == 0)
                {
                    continue;
                }

                g_state.cmd_buffer[g_state.cmd_index] = '\0';
                if (strcmp(g_state.cmd_buffer, "start") == 0)
                {
                    g_state.system_running = true;
                    printf("\nMedições iniciadas.\n");
                }
                else if (strcmp(g_state.cmd_buffer, "stop") == 0)
                {
                    g_state.system_running = false;
                    printf("\nMedições paradas.\n");
                }
                else
                {
                    printf("\nComando desconhecido: %s\n", g_state.cmd_buffer);
                }
                g_state.cmd_index = 0;
                memset(g_state.cmd_buffer, 0, sizeof(g_state.cmd_buffer));
            }
            else if (g_state.cmd_index < CMD_BUFFER_SIZE - 1)
            {
                g_state.cmd_buffer[g_state.cmd_index++] = (char)c;
            }
        }

        if (g_state.system_running && g_state.current_state == STATE_IDLE)
        {
            send_trigger_pulse();
            g_state.current_state = STATE_WAITING_FOR_ECHO;

            g_state.measurement_alarm_id = add_alarm_in_us(MEASUREMENT_TIMEOUT_US, timeout_callback, &g_state, true);
        }

        if (g_state.current_state == STATE_MEASUREMENT_COMPLETE || g_state.current_state == STATE_MEASUREMENT_ERROR)
        {
            datetime_t current_time;
            rtc_get_datetime(&current_time);

            char time_str[16];
            sprintf(time_str, "%02d:%02d:%02d", current_time.hour, current_time.min, current_time.sec);

            if (g_state.current_state == STATE_MEASUREMENT_COMPLETE)
            {
                int64_t duration_us = absolute_time_diff_us(g_state.echo_start_time, g_state.echo_end_time);
                float distance = (duration_us * 0.0343f) / 2.0f;
                printf("\n%s - %.0f cm\n", time_str, distance);
            }
            else
            {
                printf("\n%s - Falha\n", time_str);
            }
            g_state.current_state = STATE_IDLE;

            if (g_state.system_running)
            {
                sleep_ms(1000);
            }
        }
        sleep_ms(10);
    }

    return 0;
}