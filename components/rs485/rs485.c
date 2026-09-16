#include "rs485.h"

#include "config.h"
#include "shelly_measurement.h"

#include "esp_err.h"
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "logger.h"

/* Software watchdog supervision. */
#include "watchdog.h"
#include "driver/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <stdio.h>

#define TAG "RS485"

/* RS485 UART hardware configuration. */
#define RS485_UART_PORT UART_NUM_2
#define RS485_BAUD_RATE 115200
#define RS485_TX_PIN 17
#define RS485_RX_PIN 16

/* UART driver buffer sizes. */
#define RS485_RX_BUF_SIZE 512
#define RS485_TX_BUF_SIZE 512

/* RS485 receive task configuration. */
#define RS485_TASK_STACK 4096
#define RS485_TASK_PRIO 5

/* Maximum time allowed for the RS485 task to stop cleanly. */
#define RS485_TASK_STOP_TIMEOUT_MS 1000

/* Maximum length of one hexadecimal trace line. */
#define RS485_TRACE_BUFFER_SIZE 256

/*
 * Per-frame logging is deliberately disabled in normal operation.
 * The Wall Connector retries after roughly 66 ms; formatting and printing a
 * hexadecimal frame before uart_write_bytes() can make an otherwise valid
 * Modbus reply miss that deadline and prevent meter discovery.
 */
#define RS485_TRACE_ENABLED 0

/* Tesla polls a Neurio-compatible Modbus RTU server at slave address 1. */
#define MODBUS_SLAVE_ADDRESS 1U
#define MODBUS_READ_HOLDING_REGISTERS 0x03U
#define MODBUS_READ_INPUT_REGISTERS 0x04U
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS 0x02U
#define MODBUS_MAX_READ_REGISTERS 64U

/* Maximum silence before the Wall Connector is reported disconnected. */
#define MODBUS_CONNECTION_TIMEOUT_MS 5000U

/* Conservative fallback used only if the persistent breaker limit is invalid. */
#define MODBUS_FAILSAFE_CURRENT_A 20.0f
#define MODBUS_FAILSAFE_VOLTAGE_V 230.0f

/* Internal RS485 driver state. */
typedef struct
{
    bool initialized;

    bool stop_requested;

    TaskHandle_t rx_task;

    SemaphoreHandle_t state_mutex;

} rs485_context_t;

/* Global RS485 driver context. */
static rs485_context_t s_rs485;



/* Aggregated diagnostics; never logged in the time-critical reply path. */
static volatile uint32_t s_modbus_requests;
static volatile uint32_t s_modbus_responses;
static volatile uint32_t s_modbus_ignored;
static volatile uint16_t s_modbus_last_start;

/* Timestamp of the most recent supported Modbus request from the charger. */
static volatile TickType_t s_modbus_last_request_tick;

/* Distinguishes startup from a valid request received at tick zero. */
static volatile bool s_modbus_request_seen;

/*
 * Coherent measurement snapshot used to build one complete Modbus response.
 *
 * A single snapshot prevents the high and low words of one FP32 value from
 * coming from two different Shelly updates.
 */
typedef struct
{
    float phase_power[SHELLY_PHASE_COUNT];

    float phase_current[SHELLY_PHASE_COUNT];

    float total_power;

    float total_current;

} modbus_meter_snapshot_t;

static void rs485_log_frame(
    const char *direction,
    const uint8_t *data,
    size_t length);

/* CRC-16/Modbus, least-significant CRC byte transmitted first. */
static uint16_t modbus_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8U; bit++)
        {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1U) ^ 0xA001U)
                             : (uint16_t)(crc >> 1U);
        }
    }

    return crc;
}

static uint16_t modbus_float_word(float value, bool low_word)
{
    uint32_t raw = 0;
    memcpy(&raw, &value, sizeof(raw));
    return low_word ? (uint16_t)(raw & 0xFFFFU)
                    : (uint16_t)(raw >> 16U);
}

/*
 * Builds one coherent meter snapshot for a Modbus response.
 *
 * Valid Shelly data is published directly. If the Shelly measurement is
 * invalid, every active phase is reported at the configured breaker limit.
 * This leaves no apparent headroom and therefore fails safe.
 *
 * No diagnostic is emitted here because this function executes in the
 * time-critical Modbus response path.
 */
static void modbus_build_meter_snapshot(
    modbus_meter_snapshot_t *snapshot)
{
    memset(
        snapshot,
        0,
        sizeof(*snapshot));

    const system_config_t *config =
        config_get();

    const uint8_t configured_phase =
        config->shelly_phase;

    const float safe_current =
        (config->breaker_limit > 0U)
            ? (float)config->breaker_limit
            : MODBUS_FAILSAFE_CURRENT_A;

    shelly_measurement_t measurement;

    bool valid =
        (configured_phase <= SHELLY_PHASE_COUNT) &&
        shelly_measurement_get(
            &measurement);

    if (valid)
    {
        if (configured_phase == 0U)
        {
            /*
             * Triphase mode: publish Shelly A/B/C as Modbus CT1/CT2/CT3.
             */
            for (unsigned i = 0U;
                 i < SHELLY_PHASE_COUNT;
                 i++)
            {
                if (!isfinite(measurement.phase_current[i]) ||
                    (measurement.phase_current[i] < 0.0f) ||
                    !isfinite(measurement.phase_power[i]))
                {
                    valid =
                        false;

                    break;
                }

                snapshot->phase_current[i] =
                    measurement.phase_current[i];

                snapshot->phase_power[i] =
                    measurement.phase_power[i];

                snapshot->total_current +=
                    snapshot->phase_current[i];

                snapshot->total_power +=
                    snapshot->phase_power[i];
            }
        }
        else
        {
            /*
             * Monophase mode: use the configured Shelly source phase, but
             * publish it as CT1 because the Wall Connector has one active line.
             */
            const unsigned source_index =
                (unsigned)(configured_phase - 1U);

            if (!isfinite(measurement.phase_current[source_index]) ||
                (measurement.phase_current[source_index] < 0.0f) ||
                !isfinite(measurement.phase_power[source_index]))
            {
                valid =
                    false;
            }
            else
            {
                snapshot->phase_current[0] =
                    measurement.phase_current[source_index];

                snapshot->phase_power[0] =
                    measurement.phase_power[source_index];

                snapshot->total_current =
                    snapshot->phase_current[0];

                snapshot->total_power =
                    snapshot->phase_power[0];
            }
        }
    }

    if (valid)
    {
        return;
    }

    /*
     * Reject every partially constructed value before generating fail-safe
     * data. Unused monophase channels remain at zero.
     */
    memset(
        snapshot,
        0,
        sizeof(*snapshot));

    const unsigned active_phases =
        (configured_phase == 0U)
            ? SHELLY_PHASE_COUNT
            : 1U;

    for (unsigned i = 0U;
         i < active_phases;
         i++)
    {
        snapshot->phase_current[i] =
            safe_current;

        snapshot->phase_power[i] =
            safe_current *
            MODBUS_FAILSAFE_VOLTAGE_V;

        snapshot->total_current +=
            snapshot->phase_current[i];

        snapshot->total_power +=
            snapshot->phase_power[i];
    }
}

static bool modbus_register_is_supported(uint16_t address)
{
    return (address == 0U) ||
           ((address >= 1U) && (address <= 55U)) ||
           ((address >= 0x88U) && (address <= 0x92U)) ||
           ((address >= 0xF4U) && (address <= 0xFDU)) ||
           ((address >= 40002U) && (address <= 40007U));
}

static bool modbus_range_is_supported(uint16_t start, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++)
    {
        if (!modbus_register_is_supported((uint16_t)(start + i)))
        {
            return false;
        }
    }
    return true;
}

/* Neurio identity and measurement register map used by known Gen 3 projects. */
static uint16_t modbus_register_value(
    uint16_t address,
    const modbus_meter_snapshot_t *snapshot)
{
    if (address == 0U)
    {
        return 0x0000U;
    }

    static const uint16_t identity[55] =
    {
        0x3078, 0x3030, 0x3030, 0x3034, 0x3731, 0x3442, 0x3035, 0x3638,
        0x3631, 0x0000, 0x312E, 0x362E, 0x312D, 0x5465, 0x736C, 0x6100,
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x3031, 0x322E, 0x3030, 0x3032,
        0x3041, 0x2E48, 0x0000, 0xFFFF, 0x3930, 0x3935, 0x3400, 0x5641,
        0x4834, 0x3831, 0x3041, 0x4230, 0x3233, 0x3100, 0xFFFF, 0xFFFF,
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x3034, 0x3A37,
        0x313A, 0x3442, 0x3A30, 0x353A, 0x3638, 0x3A36, 0x3100
    };

    if ((address >= 1U) && (address <= 55U))
    {
        return identity[address - 1U];
    }

    /* Phase power registers: CT1, CT2, CT3 and unused CT4. */
    if ((address >= 0x88U) &&
        (address <= 0x8FU))
    {
        const unsigned channel =
            (unsigned)((address - 0x88U) / 2U);

        float value =
            0.0f;

        if (channel < SHELLY_PHASE_COUNT)
        {
            value =
                snapshot->phase_power[channel];
        }

        return modbus_float_word(
            value,
            (address & 1U) != 0U);
    }

    /* Aggregate active power registers. */
    if ((address == 0x90U) ||
        (address == 0x91U))
    {
        return modbus_float_word(
            snapshot->total_power,
            address == 0x91U);
    }

    /* Phase current registers: CT1, CT2, CT3 and unused CT4. */
    if ((address >= 0xF4U) &&
        (address <= 0xFBU))
    {
        const unsigned channel =
            (unsigned)((address - 0xF4U) / 2U);

        float value =
            0.0f;

        if (channel < SHELLY_PHASE_COUNT)
        {
            value =
                snapshot->phase_current[channel];
        }

        return modbus_float_word(
            value,
            (address & 1U) != 0U);
    }

    /* Aggregate current registers. */
    if ((address == 0xFCU) ||
        (address == 0xFDU))
    {
        return modbus_float_word(
            snapshot->total_current,
            address == 0xFDU);
    }

    switch (address)
    {
        case 0x92U: return 0U;
        case 40002U: return 0x0001U;
        case 40003U: return 0x0042U;
        case 40004U: return 0x4765U;
        case 40005U: return 0x6E65U;
        case 40006U: return 0x7261U;
        case 40007U: return 0x6300U;
        default: return 0U;
    }
}

static void modbus_send_response(uint8_t function, uint16_t start, uint16_t count)
{
    /*
     * Capture the Shelly data once so every register in this response comes
     * from the same electrical measurement.
     */
    modbus_meter_snapshot_t snapshot;

    modbus_build_meter_snapshot(
        &snapshot);

    uint8_t response[3U + (MODBUS_MAX_READ_REGISTERS * 2U) + 2U];
    response[0] = MODBUS_SLAVE_ADDRESS;
    response[1] = function;
    response[2] = (uint8_t)(count * 2U);

    for (uint16_t i = 0; i < count; i++)
    {
        uint16_t value = modbus_register_value((uint16_t)(start + i), &snapshot);
        response[3U + (i * 2U)] = (uint8_t)(value >> 8U);
        response[4U + (i * 2U)] = (uint8_t)value;
    }

    size_t payload_length = 3U + ((size_t)count * 2U);
    uint16_t crc = modbus_crc16(response, payload_length);
    response[payload_length] = (uint8_t)crc;
    response[payload_length + 1U] = (uint8_t)(crc >> 8U);
    size_t response_length = payload_length + 2U;

    int written =
        uart_write_bytes(
            RS485_UART_PORT,
            response,
            response_length);

    esp_err_t tx_result =
        ESP_FAIL;

    if (written == (int)response_length)
    {
        tx_result =
            uart_wait_tx_done(
                RS485_UART_PORT,
                pdMS_TO_TICKS(100));
    }

    if (written != (int)response_length)
    {
        logger_error(
            TAG,
            "MODBUS TX WRITE FAILED: expected=%u written=%d start=%u count=%u",
            (unsigned)response_length,
            written,
            (unsigned)start,
            (unsigned)count);
    }
    else if (tx_result != ESP_OK)
    {
        logger_error(
            TAG,
            "MODBUS TX WAIT FAILED: error=%s start=%u count=%u",
            esp_err_to_name(tx_result),
            (unsigned)start,
            (unsigned)count);
    }
    else
    {
        s_modbus_responses++;
    }

    /* Never place diagnostics before the time-critical UART transmission. */
    rs485_log_frame("GEN3 MODBUS TX", response, response_length);
}

static void modbus_send_exception(uint8_t function, uint8_t exception_code)
{
    uint8_t response[5];
    response[0] = MODBUS_SLAVE_ADDRESS;
    response[1] = (uint8_t)(function | 0x80U);
    response[2] = exception_code;

    uint16_t crc = modbus_crc16(response, 3U);
    response[3] = (uint8_t)crc;
    response[4] = (uint8_t)(crc >> 8U);

    int written = uart_write_bytes(
        RS485_UART_PORT,
        response,
        sizeof(response));

    if ((written == (int)sizeof(response)) &&
        (uart_wait_tx_done(
             RS485_UART_PORT,
             pdMS_TO_TICKS(100)) == ESP_OK))
    {
        s_modbus_responses++;
    }
    else
    {
        logger_warn(TAG, "GEN3 Modbus exception transmission failed");
    }
}

static void modbus_process_request(const uint8_t request[8])
{
    uint16_t received_crc = (uint16_t)request[6] | ((uint16_t)request[7] << 8U);
    if (modbus_crc16(request, 6U) != received_crc)
    {
        return;
    }

    uint8_t function = request[1];
    uint16_t start = ((uint16_t)request[2] << 8U) | request[3];
    uint16_t count = ((uint16_t)request[4] << 8U) | request[5];
    s_modbus_requests++;
    s_modbus_last_start = start;

    rs485_log_frame("GEN3 MODBUS RX", request, 8U);

    if ((function != MODBUS_READ_HOLDING_REGISTERS) &&
        (function != MODBUS_READ_INPUT_REGISTERS))
    {
        logger_error(
            TAG,
            "MODBUS REJECTED FUNCTION: %u",
            (unsigned)function);

        return;
    }

    if (count == 0U)
    {
        logger_error(
            TAG,
            "MODBUS REJECTED ZERO COUNT");

        return;
    }

    if (count > MODBUS_MAX_READ_REGISTERS)
    {
        logger_error(
            TAG,
            "MODBUS REJECTED COUNT: %u max=%u",
            (unsigned)count,
            (unsigned)MODBUS_MAX_READ_REGISTERS);

        return;
    }

    if (!modbus_range_is_supported(
            start,
            count))
    {
        s_modbus_ignored++;

        logger_error(
            TAG,
            "MODBUS REJECTED RANGE: start=%u count=%u",
            (unsigned)start,
            (unsigned)count);

        modbus_send_exception(
            function,
            MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);

        return;
    }

    /*
    * A supported request proves that the Wall Connector is present and is
    * actively polling this remote meter.
    */
    s_modbus_last_request_tick =
        xTaskGetTickCount();

    s_modbus_request_seen =
        true;

    modbus_send_response(function, start, count);
}

/* Returns whether the RS485 task has been requested to stop. */
static bool rs485_stop_is_requested(void)
{
    bool stop_requested;

    /* Protect the task control state while reading the stop request. */
    xSemaphoreTake(
        s_rs485.state_mutex,
        portMAX_DELAY);

    stop_requested =
        s_rs485.stop_requested;

    xSemaphoreGive(
        s_rs485.state_mutex);

    return stop_requested;
}

/* Marks the RS485 receive task as terminated. */
static void rs485_task_mark_stopped(void)
{
    /* Protect the task control state while clearing the task state. */
    xSemaphoreTake(
        s_rs485.state_mutex,
        portMAX_DELAY);

    s_rs485.rx_task =
        NULL;

    xSemaphoreGive(
        s_rs485.state_mutex);
}

/* Releases RS485 resources allocated by the driver. */
static void rs485_cleanup(void)
{
    uart_driver_delete(
        RS485_UART_PORT);
   

    if (s_rs485.state_mutex != NULL)
    {
        vSemaphoreDelete(
            s_rs485.state_mutex);

        s_rs485.state_mutex =
            NULL;
    }

    s_rs485.initialized =
        false;
}

/* Dumps one RS485 frame as hexadecimal values. */
static void rs485_log_frame(
    const char *direction,
    const uint8_t *data,
    size_t length)
{
    #if !RS485_TRACE_ENABLED
        (void)direction;
        (void)data;
        (void)length;
        return;
    #endif
    
    if ((direction == NULL) ||
        (data == NULL) ||
        (length == 0))
    {
        return;
    }

    char line[RS485_TRACE_BUFFER_SIZE];

    size_t position = 0;

    int written = snprintf(
        line,
        sizeof(line),
        "%s (%u): ",
        direction,
        (unsigned)length);

    if ((written < 0) ||
        ((size_t)written >= sizeof(line)))
    {
        return;
    }

    position =
        (size_t)written;

    for (size_t i = 0; i < length; i++)
    {
        if ((sizeof(line) - position) < 4)
        {
            break;
        }

        written = snprintf(
            &line[position],
            sizeof(line) - position,
            "%02X ",
            data[i]);

        if (written < 0)
        {
            break;
        }

        position += (size_t)written;
    }

    logger_info(
        TAG,
        "%s",
        line);
}

/* Reads raw Gen 3 bus bytes without sending or parsing a meter response. */
static void rs485_poll_frames(void)
{
    uint8_t rx[8];

    static uint8_t request_buffer[256];
    static size_t request_length = 0;

    int length = uart_read_bytes(
        RS485_UART_PORT,
        rx,
        sizeof(rx),
        pdMS_TO_TICKS(10));

    if (length < 0)
    {
        logger_error(
            TAG,
            "UART read failed: %d",
            length);

        return;
    }

    if (length == 0)
    {
        return;
    }

    for (int i = 0; i < length; i++)
    {
        if (request_length >=
            sizeof(request_buffer))
        {
            request_length = 0;
        }

        request_buffer[request_length++] =
            rx[i];

        while (request_length >= 8U)
        {
            uint16_t received_crc =
                (uint16_t)request_buffer[6] |
                ((uint16_t)request_buffer[7] << 8U);

            uint16_t calculated_crc =
                modbus_crc16(
                    request_buffer,
                    6U);

            if ((request_buffer[0] ==
                    MODBUS_SLAVE_ADDRESS) &&
                ((request_buffer[1] ==
                    MODBUS_READ_HOLDING_REGISTERS) ||
                 (request_buffer[1] ==
                    MODBUS_READ_INPUT_REGISTERS)) &&
                (calculated_crc == received_crc))
            {
                modbus_process_request(
                    request_buffer);

                memmove(
                    request_buffer,
                    request_buffer + 8U,
                    request_length - 8U);

                request_length -= 8U;

                continue;
            }

            memmove(
                request_buffer,
                request_buffer + 1U,
                request_length - 1U);

            request_length--;
        }
    }
}

/* RS485 receive task. */
static void rs485_task(void *arg)
{
    (void)arg;

    uint32_t previous_requests = 0;
    uint32_t previous_responses = 0;
    uint32_t previous_ignored = 0;

    TickType_t last_report =
        xTaskGetTickCount();

    watchdog_register(
        WD_RS485);

    while (!rs485_stop_is_requested())
    {
        watchdog_feed(
            WD_RS485);

        /*
         * Read and process Modbus data directly from the UART driver.
         * No UART event queue is used.
         */
        rs485_poll_frames();

        TickType_t now =
            xTaskGetTickCount();

        if ((now - last_report) >=
            pdMS_TO_TICKS(5000))
        {
            logger_info(
                TAG,
                "GEN3 Modbus/5s: requests=%lu responses=%lu ignored=%lu last_start=%u",
                (unsigned long)(
                    s_modbus_requests -
                    previous_requests),
                (unsigned long)(
                    s_modbus_responses -
                    previous_responses),
                (unsigned long)(
                    s_modbus_ignored -
                    previous_ignored),
                (unsigned)
                    s_modbus_last_start);

            previous_requests =
                s_modbus_requests;

            previous_responses =
                s_modbus_responses;

            previous_ignored =
                s_modbus_ignored;

            last_report =
                now;
        }
    }

    watchdog_unregister(
        WD_RS485);

    rs485_task_mark_stopped();

    vTaskDelete(
        NULL);
}

/* Initializes the UART used by the automatic-direction RS485 converter. */
esp_err_t rs485_init(void)
{
    if (s_rs485.initialized)
    {
        return ESP_OK;
    }

    memset(
        &s_rs485,
        0,
        sizeof(s_rs485));
    

    /* Create the mutex protecting the RS485 task state. */
    s_rs485.state_mutex =
        xSemaphoreCreateMutex();

    if (s_rs485.state_mutex == NULL)
    {
        logger_error(
            TAG,
            "Unable to create state mutex");

        return ESP_ERR_NO_MEM;
    }

    uart_config_t uart_cfg =
    {
        .baud_rate = RS485_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };

    /* Install the UART driver and allocate its buffers. */
    esp_err_t err = uart_driver_install(
        RS485_UART_PORT,
        RS485_RX_BUF_SIZE,
        RS485_TX_BUF_SIZE,
        0,
        NULL,
        0);

    if (err != ESP_OK)
    {
        /* Release the state mutex created before UART installation. */
        if (s_rs485.state_mutex != NULL)
        {
            vSemaphoreDelete(
                s_rs485.state_mutex);

            s_rs485.state_mutex =
                NULL;
        }

        return err;
    }

    /* Configure the UART parameters. */
    err = uart_param_config(
        RS485_UART_PORT,
        &uart_cfg);

    if (err != ESP_OK)
    {
        rs485_cleanup();

        return err;
    }

    /* RS5040 switches direction automatically; no RTS/CTS wire is connected. */
    err = uart_set_pin(
        RS485_UART_PORT,
        RS485_TX_PIN,
        RS485_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE);

    if (err != ESP_OK)
    {
        rs485_cleanup();

        return err;
    }

    /* The auto-direction RS5040 needs ordinary UART mode, not RTS control. */
    err = uart_set_mode(
        RS485_UART_PORT,
        UART_MODE_UART);

    if (err != ESP_OK)
    {
        rs485_cleanup();

        return err;
    }

    s_rs485.initialized =
        true;

    logger_info(
        TAG,
        "Automatic-direction RS485 initialized "
        "(UART2, TX17, RX16, 115200 8N1)");

    return ESP_OK;
}

/* Starts the RS485 receive task. */
esp_err_t rs485_start(void)
{
    if (!s_rs485.initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Protect the receive task state while preparing a new instance. */
    xSemaphoreTake(
        s_rs485.state_mutex,
        portMAX_DELAY);

    if (s_rs485.rx_task != NULL)
    {
        xSemaphoreGive(
            s_rs485.state_mutex);

        return ESP_OK;
    }

    /* Clear any previous stop request before starting the receive task. */
    s_rs485.stop_requested =
        false;

    /* Do not retain a connected state from a previous RS485 task instance. */
    s_modbus_last_request_tick =
        0;

    s_modbus_request_seen =
        false;

    xSemaphoreGive(
        s_rs485.state_mutex);

    /*
     * Create the task outside the mutex because it may start immediately
     * and attempt to acquire the RS485 state mutex.
     */
    BaseType_t result =
        xTaskCreatePinnedToCore(
            rs485_task,
            "rs485",
            RS485_TASK_STACK,
            NULL,
            RS485_TASK_PRIO,
            &s_rs485.rx_task,
            tskNO_AFFINITY);

    if (result != pdPASS)
    {
        /* Keep the receive task state consistent after a creation failure. */
        xSemaphoreTake(
            s_rs485.state_mutex,
            portMAX_DELAY);

        s_rs485.rx_task =
            NULL;       

        xSemaphoreGive(
            s_rs485.state_mutex);

        logger_error(
            TAG,
            "Unable to create RS485 task");

        return ESP_FAIL;
    }

    logger_info(
        TAG,
        "RS485 started");

    return ESP_OK;
}

/*
 * Returns whether the Gen 3 Wall Connector is actively polling the emulated
 * remote meter. A started UART alone is not proof that the charger is wired.
 */
bool rs485_wall_connector_is_connected(void)
{
    if (!s_modbus_request_seen)
    {
        return false;
    }

    const TickType_t now =
        xTaskGetTickCount();

    const TickType_t last_request =
        s_modbus_last_request_tick;

    return (now - last_request) <=
        pdMS_TO_TICKS(
            MODBUS_CONNECTION_TIMEOUT_MS);
}

/* Stops the RS485 receive task and releases its resources. */
esp_err_t rs485_stop(void)
{
    bool task_running;

    if (!s_rs485.initialized)
    {
        return ESP_OK;
    }

    /* Protect the receive task state while requesting the stop. */
    xSemaphoreTake(
        s_rs485.state_mutex,
        portMAX_DELAY);

    task_running =
        (s_rs485.rx_task != NULL);

    if (task_running)
    {
        s_rs485.stop_requested =
            true;
    }

    xSemaphoreGive(
        s_rs485.state_mutex);

    if (task_running)
    {
        TickType_t start_tick =
            xTaskGetTickCount();

        /* Wait until the receive task has terminated itself. */
        for (;;)
        {
            xSemaphoreTake(
                s_rs485.state_mutex,
                portMAX_DELAY);

            task_running =
                (s_rs485.rx_task != NULL);

            xSemaphoreGive(
                s_rs485.state_mutex);

            if (!task_running)
            {
                break;
            }

            if ((xTaskGetTickCount() - start_tick) >=
                pdMS_TO_TICKS(
                    RS485_TASK_STOP_TIMEOUT_MS))
            {
                logger_error(
                    TAG,
                    "RS485 task stop timeout");

                return ESP_ERR_TIMEOUT;
            }

            vTaskDelay(
                pdMS_TO_TICKS(
                    10));
        }
    }

    /* Release the RS485 resources after the receive task has stopped. */
    rs485_cleanup();

    logger_info(
        TAG,
        "RS485 stopped");

    return ESP_OK;
}
