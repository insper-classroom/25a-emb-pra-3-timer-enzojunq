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

#define TRIGGER_PIN             28    // Ajuste conforme sua montagem
#define ECHO_PIN                27    // Ajuste conforme sua montagem
#define MEASUREMENT_TIMEOUT_US  30000 // Timeout em microsegundos para a medição
#define CMD_BUFFER_SIZE         64

// Estados da medição
typedef enum {
    STATE_IDLE = 0,
    STATE_WAITING_FOR_ECHO,
    STATE_MEASUREMENT_COMPLETE,
    STATE_MEASUREMENT_ERROR
} measurement_state_t;

volatile measurement_state_t current_state = STATE_IDLE;
volatile absolute_time_t echo_start_time, echo_end_time;
volatile alarm_id_t measurement_alarm_id = 0;
volatile bool system_running = false;

// Buffer para comandos via terminal
char cmd_buffer[CMD_BUFFER_SIZE];
int cmd_index = 0;

// Callback do alarme: se o tempo máximo for ultrapassado, define erro
int64_t timeout_callback(alarm_id_t id, void *user_data) {
    if (current_state == STATE_WAITING_FOR_ECHO) {
        current_state = STATE_MEASUREMENT_ERROR;
    }
    return 0; // Não reagendar o alarme
}

// Callback de interrupção para o pino do ECHO (captura bordas de subida e descida)
void echo_callback(uint gpio, uint32_t events) {
    if (gpio == ECHO_PIN) {
        if (events & GPIO_IRQ_EDGE_RISE) {
            echo_start_time = get_absolute_time();
        }
        if (events & GPIO_IRQ_EDGE_FALL) {
            echo_end_time = get_absolute_time();
            if (current_state == STATE_WAITING_FOR_ECHO) {
                cancel_alarm(measurement_alarm_id);
                current_state = STATE_MEASUREMENT_COMPLETE;
            }
        }
    }
}

// Gera um pulso de trigger de 10 µs
void send_trigger_pulse() {
    gpio_put(TRIGGER_PIN, 1);
    sleep_us(10);
    gpio_put(TRIGGER_PIN, 0);
}

int main() {
    stdio_init_all();

    // Inicializa pinos
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_put(TRIGGER_PIN, 0);

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    // Não habilitar pull-up, pois o sensor é ativo
    gpio_disable_pulls(ECHO_PIN);

    // Configura interrupção para o pino do ECHO (borda de subida e descida)
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &echo_callback);

    // Inicializa o RTC
    rtc_init();
    // Configure a data/hora inicial – ajuste os valores conforme necessário.
    // Exemplo: 17/03/2025, 12:00:00 (dotw pode ser ajustado; 1 = segunda-feira, 2 = terça, etc.)
    datetime_t t = {
        .year = 2025,
        .month = 3,
        .day = 19,
        .dotw = 3,
        .hour = 10,
        .min = 0,
        .sec = 0
    };
    rtc_set_datetime(&t);

    printf("Sistema iniciado.\n");
    printf("Digite 'start' para iniciar as medições e 'stop' para parar.\n");

    while (true) {
        // Leitura não bloqueante do terminal
        int c = getchar_timeout_us(1000);
    
        if (c != PICO_ERROR_TIMEOUT) {
            // Eco do caractere no terminal (opcional)
            putchar(c);
    
            // Verifica se é fim de linha (\r ou \n)
            if (c == '\r' || c == '\n') {
                // Se a linha estiver vazia, não processa comando
                if (cmd_index == 0) {
                    continue;
                }
                // Final do comando – processa o que foi digitado
                cmd_buffer[cmd_index] = '\0';
                if (strcmp(cmd_buffer, "start") == 0) {
                    system_running = true;
                    printf("\nMedições iniciadas.\n");
                } else if (strcmp(cmd_buffer, "stop") == 0) {
                    system_running = false;
                    printf("\nMedições paradas.\n");
                } else {
                    printf("\nComando desconhecido: %s\n", cmd_buffer);
                }
                cmd_index = 0;
                memset(cmd_buffer, 0, sizeof(cmd_buffer));
            } else if (cmd_index < CMD_BUFFER_SIZE - 1) {
                cmd_buffer[cmd_index++] = (char)c;
            }
        }
    
        // Se o sistema estiver em modo "running" e não houver medição em andamento, inicia nova medição
        if (system_running && current_state == STATE_IDLE) {
            send_trigger_pulse();
            current_state = STATE_WAITING_FOR_ECHO;
            // Agenda alarme para detectar timeout na medição
            measurement_alarm_id = add_alarm_in_us(MEASUREMENT_TIMEOUT_US, timeout_callback, NULL, true);
        }
    
        // Se a medição foi concluída ou ocorreu erro, processa e exibe o resultado
        if (current_state == STATE_MEASUREMENT_COMPLETE || current_state == STATE_MEASUREMENT_ERROR) {
            datetime_t current_time;
            rtc_get_datetime(&current_time);
    
            char time_str[16];
            sprintf(time_str, "%02d:%02d:%02d", current_time.hour, current_time.min, current_time.sec);
    
            if (current_state == STATE_MEASUREMENT_COMPLETE) {
                int64_t duration_us = absolute_time_diff_us(echo_start_time, echo_end_time);
                float distance = (duration_us * 0.0343f) / 2.0f;
                printf("\n%s - %.0f cm\n", time_str, distance);
            } else {
                printf("\n%s - Falha\n", time_str);
            }
            current_state = STATE_IDLE;
    
            // Se estiver em modo contínuo, aguarda 1 segundo entre as medições
            if (system_running) {
                sleep_ms(1000);
            }
        }
        sleep_ms(10);
    }
    
    return 0;
}