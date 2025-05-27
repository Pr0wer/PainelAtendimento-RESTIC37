#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "lib/ssd1306.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>

/*------------------------- MACROS ----------------------------*/

// Quantidade máxima de atendimentos
#define MAX_ATENDIMENTOS 10

// Marcações para ativação dos LEDs RGB
#define LED_BLUE_TRIGGER 0
#define LED_GREEN_TRIGGER MAX_ATENDIMENTOS - 2
#define LED_YELLOW_TRIGGER MAX_ATENDIMENTOS - 1
#define LED_RED_TRIGGER MAX_ATENDIMENTOS

/*-------------------------- STRUCTS --------------------------*/

// Estrutura para fila do display
typedef struct displayData_t
{
    bool limite_excedido; // Indica se o limite de atendimentos foi excedido
    bool task_reset; // Indica se a atualização veio da tarefa de reset
} displayData_t;

/*------------------------- GLOBAIS ---------------------------*/

// Pinos
static const uint btn_joystick_pin = 22;

// Contador de atendimentos
static uint8_t atendimentos = 0;

// Variável para debounce do botão do joystick
static uint tempoAnteriorJS = 0;

// Estado do programa
volatile static bool em_reset = false;

// Filas
QueueHandle_t xDisplayQueue;
QueueHandle_t xBuzzerQueue;

// Mutexes
SemaphoreHandle_t xAtendimentosMutex;
SemaphoreHandle_t xDisplayMutex;

// Semáforos binários 
SemaphoreHandle_t xResetSem;
SemaphoreHandle_t xLedRgbSem;

// Semáforos de contagem
SemaphoreHandle_t xAtendimentosSem;

/*-------------------- TAREFAS (Protótipo) -------------------*/

void vTaskDisplay(void *params);
void vTaskLedRgb(void *params);
void vTaskBuzzer(void *params);
void vTaskEntrada(void *params);
void vTaskSaida(void *params);
void vTaskReset(void *params);

/*---------------- FUNÇÕES HELPER (Protótipo) ----------------*/

void irq_reset_handler(uint gpio, uint32_t events); // Callback para o botão do joystick
void inicializarLed(uint pino);

/*--------------------- FUNÇÃO PRINCIPAL ---------------------*/

int main()
{
    stdio_init_all();

    // Inicializa o botão do joystick para reset 
    gpio_init(btn_joystick_pin);
    gpio_set_dir(btn_joystick_pin, GPIO_IN);
    gpio_pull_up(btn_joystick_pin);
    gpio_set_irq_enabled_with_callback(btn_joystick_pin, GPIO_IRQ_EDGE_FALL, true, &irq_reset_handler);

    xDisplayQueue = xQueueCreate(1, sizeof(displayData_t));
    xBuzzerQueue = xQueueCreate(1, sizeof(uint));
    xAtendimentosMutex = xSemaphoreCreateMutex();
    xDisplayMutex = xSemaphoreCreateMutex();
    xLedRgbSem = xSemaphoreCreateBinary();
    xResetSem = xSemaphoreCreateBinary();
    xAtendimentosSem = xSemaphoreCreateCounting(MAX_ATENDIMENTOS, 0);

    xTaskCreate(vTaskDisplay, "Task do Display", configMINIMAL_STACK_SIZE,
         NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(vTaskLedRgb, "Task dos LEDs", configMINIMAL_STACK_SIZE,
         NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(vTaskBuzzer, "Task do Buzzer", configMINIMAL_STACK_SIZE,
         NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(vTaskEntrada, "Task de Entrada", configMINIMAL_STACK_SIZE,
         NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(vTaskSaida, "Task de Saída", configMINIMAL_STACK_SIZE,
         NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(vTaskReset, "Task de Reset", configMINIMAL_STACK_SIZE,
         NULL, tskIDLE_PRIORITY + 3, NULL);
         
    vTaskStartScheduler();
    panic_unsupported();
}

/*------------------------- TAREFAS --------------------------*/

void vTaskDisplay(void *params)
{   
    ssd1306_t ssd;

    // Inicializa display SSD1306
    ssd1306_i2c_init(&ssd);

    // Buffer para receber dados de atualização
    displayData_t info;

    // Buffer para escrita no display
    char str_disponiveis[16];
    char str_ocupados[16];

    // Escrita inicial do display
    ssd1306_rect(&ssd, 3, 3, 122, 60, true, false);
    ssd1306_line(&ssd, 3, 25, 123, 25, true);          
    ssd1306_line(&ssd, 3, 37, 123, 37, true);  
    ssd1306_draw_string(&ssd, "ATENDIMENTOS", 13, 12);

    sprintf(str_disponiveis, "Livres: %i", MAX_ATENDIMENTOS - atendimentos);
    ssd1306_draw_string(&ssd, str_disponiveis, 8, 41);

    sprintf(str_ocupados, "Ocupados: %i", atendimentos);
    ssd1306_draw_string(&ssd, str_ocupados, 8, 52);

    ssd1306_draw_string(&ssd, "Verifique!", 8, 28);

    ssd1306_send_data(&ssd);

    while (true)
    {
        if (xQueueReceive(xDisplayQueue, &info, portMAX_DELAY) == pdPASS)
        {    
            // Ignora atualizações de tarefas que não são do modo de execução atual
            if (info.task_reset != em_reset)
            {
                continue;
            }

            // Limpa o display
            ssd1306_fill(&ssd, false);

            ssd1306_rect(&ssd, 3, 3, 122, 60, true, false);
            ssd1306_line(&ssd, 3, 25, 123, 25, true);          
            ssd1306_line(&ssd, 3, 37, 123, 37, true);  
            ssd1306_draw_string(&ssd, "ATENDIMENTOS", 13, 12);

            sprintf(str_disponiveis, "Livres: %i", MAX_ATENDIMENTOS - atendimentos);
            ssd1306_draw_string(&ssd, str_disponiveis, 8, 41);

            sprintf(str_ocupados, "Ocupados: %i", atendimentos);
            ssd1306_draw_string(&ssd, str_ocupados, 8, 52);

            // Escreve aviso de limite excedido, se ocorreu
            if (info.limite_excedido)
            {
                ssd1306_draw_string(&ssd, "Todos ocupados!", 8, 28);
            }
            else
            {
                ssd1306_draw_string(&ssd, "Verifique!", 8, 28);
            }

            ssd1306_send_data(&ssd);

            if (em_reset)
            {
                em_reset = false; // Reseta o estado do programa após a atualização
            }
        }
    }
}

void vTaskLedRgb(void *params)
{   
    // Pinos
    const uint led_red_pin = 13;
    const uint led_green_pin = 11;
    const uint led_blue_pin = 12;

    // Inicializa os LEDs RGB
    inicializarLed(led_red_pin);
    inicializarLed(led_green_pin);
    inicializarLed(led_blue_pin);

    // Led azul deve iniciar ligado
    gpio_put(led_blue_pin, 1);

    while (true)
    {   
        // Aguarda sinal para atualizar os LEDs RGB
        if (xSemaphoreTake(xLedRgbSem, portMAX_DELAY) == pdTRUE)
        {   
            // Verifica se a contagem de atendimentos está disponível
            if (xSemaphoreTake(xAtendimentosMutex, portMAX_DELAY) == pdTRUE)
            {   
                // Liga a cor de um LED de acordo com o número de atendimentos
                if (atendimentos == LED_BLUE_TRIGGER)
                {
                    gpio_put(led_blue_pin, 1);
                    gpio_put(led_green_pin, 0);
                    gpio_put(led_red_pin, 0);
                }
                else if (atendimentos <= LED_GREEN_TRIGGER)
                {   
                    gpio_put(led_blue_pin, 0);
                    gpio_put(led_green_pin, 1);
                    gpio_put(led_red_pin, 0);
                }
                else if (atendimentos == LED_YELLOW_TRIGGER)
                {
                    gpio_put(led_blue_pin, 0);
                    gpio_put(led_green_pin, 1);
                    gpio_put(led_red_pin, 1);
                }
                else if (atendimentos >= LED_RED_TRIGGER)
                {
                    gpio_put(led_blue_pin, 0);
                    gpio_put(led_green_pin, 0);
                    gpio_put(led_red_pin, 1);
                }

                // Libera o mutex de atendimentos
                xSemaphoreGive(xAtendimentosMutex);

                // Aguarda um tempo para visualização do usuário
                vTaskDelay(pdMS_TO_TICKS(150));
            }
        }
    }
}

void vTaskBuzzer(void *params)
{   
    // Pino
    const uint buzzer_pin = 21;

    // Parâmetros para PWM do buzzer
    uint16_t wrap = 1000;
    float div = 250.0;

    // Obter slice e definir pino como PWM
    gpio_set_function(buzzer_pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(buzzer_pin);

    // Configurar frequência
    pwm_set_wrap(slice, wrap);
    pwm_set_clkdiv(slice, div); 
    pwm_set_gpio_level(buzzer_pin, 0);
    pwm_set_enabled(slice, true);

    // Quantidade de beeps que o buzzer deve emitir
    uint quant_beeps = 0;
    
    while (true)
    {
        if (xQueueReceive(xBuzzerQueue, &quant_beeps, portMAX_DELAY) == pdPASS)
        {   
            // Fazer beeps equivalete à quantidade recebida
            for (int i = 0; i < quant_beeps; i++)
            {
                pwm_set_gpio_level(buzzer_pin, wrap / 2); // Liga o buzzer
                vTaskDelay(pdMS_TO_TICKS(100)); // Aguarda 100ms
                pwm_set_gpio_level(buzzer_pin, 0); // Desliga o buzzer
                vTaskDelay(pdMS_TO_TICKS(100)); // Aguarda 100ms
            }
        }
    }
}

void vTaskEntrada(void *params)
{   
    // Pino para funcionamento
    const uint btn_a_pin = 5;

    // Inicializa o botão A
    gpio_init(btn_a_pin);
    gpio_set_dir(btn_a_pin, GPIO_IN);
    gpio_pull_up(btn_a_pin);

    // Variáveis para debounce
    uint tempoAtual;
    uint tempoAnterior = 0;

    // Buffer para escrita no display
    displayData_t info;
    info.task_reset = false; // Não é uma tarefa de reset

    // Buffer para envio de beeps ao buzzer
    uint quant_beeps = 1;

    while (true)
    {   
        // Verifica se o botão foi pressionado
        tempoAtual = to_us_since_boot(get_absolute_time());
        if (!gpio_get(btn_a_pin) && (tempoAtual - tempoAnterior > 200000) && (!em_reset))
        {   
            // Aguarda o próximo ciclo para evitar múltiplas leituras
            tempoAnterior = tempoAtual;

            // Verifica se a contagem de atendimentos está disponível
            if (xSemaphoreTake(xAtendimentosMutex, portMAX_DELAY) == pdTRUE)
            {   
                // Verifica se ainda há espaço para novos atendimentos
                if (xSemaphoreGive(xAtendimentosSem) == pdTRUE)
                {   
                    atendimentos++;
                    info.limite_excedido = false;

                    // Indica atualização do LED RGB
                    xSemaphoreGive(xLedRgbSem);
                }
                else
                {   
                    // Indica que o limite foi excedido
                    info.limite_excedido = true; 

                    // Realizar emissão de um beep pelo buzzer
                    xQueueSend(xBuzzerQueue, &quant_beeps, portMAX_DELAY);
                }

                // Atualiza o display quando disponível
                if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE)
                {
                    xQueueSend(xDisplayQueue, &info, portMAX_DELAY); // Envia dados para a fila de display
                    xSemaphoreGive(xDisplayMutex); // Libera o display
                }

                xSemaphoreGive(xAtendimentosMutex);
            }
        }
        // Reduz uso da CPU
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vTaskSaida(void *params)
{
    // Pino para funcionamento
    const uint btn_b_pin = 6;

    // Inicializa o botão A
    gpio_init(btn_b_pin);
    gpio_set_dir(btn_b_pin, GPIO_IN);
    gpio_pull_up(btn_b_pin);

    // Variáveis para debounce
    uint tempoAtual;
    uint tempoAnterior = 0;

    // Buffer para escrita no display
    displayData_t info;
    info.task_reset = false; // Não é uma tarefa de reset
    info.limite_excedido = false; // Não se emite aviso de limite para valores menores que 0

    while (true)
    {   
        // Verifica se o botão foi pressionado
        tempoAtual = to_us_since_boot(get_absolute_time());
        if (!gpio_get(btn_b_pin) && (tempoAtual - tempoAnterior > 200000) && (!em_reset))
        {   
            // Aguarda o próximo ciclo para evitar múltiplas leituras
            tempoAnterior = tempoAtual;

            // Verifica se a contagem de atendimentos está disponível
            if (xSemaphoreTake(xAtendimentosMutex, portMAX_DELAY) == pdTRUE)
            {   
                // Verifica se ainda há espaço para novos atendimentos
                if (xSemaphoreTake(xAtendimentosSem, 0) == pdFALSE)
                {   
                    // Ignora chamada se não houver atendimentos
                    xSemaphoreGive(xAtendimentosMutex);
                    continue;
                }

                atendimentos--;

                // Indica atualização do LED RGB
                xSemaphoreGive(xLedRgbSem);

                // Atualiza o display quando disponível
                if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE)
                {
                    xQueueSend(xDisplayQueue, &info, portMAX_DELAY); // Envia dados para a fila de display
                    xSemaphoreGive(xDisplayMutex); // Libera o display
                }

                xSemaphoreGive(xAtendimentosMutex);
            }
        }
        // Reduz uso da CPU
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vTaskReset(void *params)
{   
    // Define informação para o display durante o reset
    displayData_t info;
    info.limite_excedido = false; // Não há limite na tarefa de reset
    info.task_reset = true; // Indica que é uma tarefa de reset

    // Quantidade de beeps que o buzzer deve emitir
    uint quant_beeps = 2;

    while (true)
    {
        if (xSemaphoreTake(xResetSem, portMAX_DELAY) == pdTRUE)
        {
            // Reseta o contador de atendimentos e seu semáforo
            while (xSemaphoreTake(xAtendimentosSem, 0) == pdTRUE) { }
            atendimentos = 0;

            // Garante que a fila do display e do buzzer estejam limpas
            xQueueReset(xDisplayQueue);
            xQueueReset(xBuzzerQueue);

            // Atualiza o display quando disponível
            if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE)
            {
                xQueueSend(xDisplayQueue, &info, portMAX_DELAY); // Envia dados para a fila de display
                xSemaphoreGive(xDisplayMutex); // Libera o display
            }

            // Atualiza LED e buzzer
            xSemaphoreGive(xLedRgbSem);
            xQueueSend(xBuzzerQueue, &quant_beeps, portMAX_DELAY);
        }
    }
}

/*---------------------- FUNÇÕES HELPER ----------------------*/

// Callback do botão do joystick para reset
void irq_reset_handler(uint gpio, uint32_t events)
{
    uint tempoAtual = to_us_since_boot(get_absolute_time());
    if (gpio == btn_joystick_pin && (tempoAtual - tempoAnteriorJS > 200000))
    {   
        tempoAnteriorJS = tempoAtual;

        // Sinaliza início da tarefa de reset
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        em_reset = true; // Muda o estado do programa para reset
        xSemaphoreGiveFromISR(xResetSem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// Inicializa um LED em um pino específico
void inicializarLed(uint pino)
{
    gpio_init(pino);
    gpio_set_dir(pino, GPIO_OUT);
    gpio_put(pino, 0); // Desliga o LED inicialmente
}