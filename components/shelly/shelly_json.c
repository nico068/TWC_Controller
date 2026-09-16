#include "shelly_json.h"
#include "config.h"

#include "logger.h"

#include "cJSON.h"

#include <math.h>
#include <string.h>

#define TAG "SHELLY_JSON"

/* Parses a Shelly Gen1 measurement. */
bool shelly_json_parse_gen1_measurement(
    const char *json,
    shelly_measurement_t *measurement);

/* Parses a Shelly Gen2 V1 measurement. */
bool shelly_json_parse_gen2_v1_measurement(
    const char *json,
    shelly_measurement_t *measurement);

/* Parses a Shelly Gen2 V2 measurement. */
bool shelly_json_parse_gen2_v2_measurement(
    const char *json,
    shelly_measurement_t *measurement);

/* Internal parsers. */
static bool shelly_json_parse_gen1(
    cJSON *root,
    shelly_measurement_t *measurement);

static bool shelly_json_parse_gen2_v1(
    cJSON *root,
    shelly_measurement_t *measurement);

static bool shelly_json_parse_gen2_v2(
    cJSON *root,
    shelly_measurement_t *measurement);

/*
 * Copies a single-channel Shelly measurement into the configured phase slot.
 *
 * Gen1 and Gen2 V1 devices expose only one electrical channel. In automatic
 * mode, that channel is stored as phase A. When phase A, B or C is explicitly
 * selected, the measurement is stored in the corresponding array position.
 */
static bool shelly_json_store_single_phase_details(
    shelly_measurement_t *measurement)
{
    const uint8_t configured_phase =
        config_get()->shelly_phase;

    if (configured_phase > SHELLY_PHASE_COUNT)
    {
        return false;
    }

    /*
     * Un Shelly monophasé n'a qu'un canal.
     * Phase 0 signifie mode automatique/triphasé : utiliser L1.
     * Phase 1/2/3 place la mesure sur le canal correspondant.
     */
    const unsigned index =
        (configured_phase == 0U)
            ? 0U
            : (unsigned)(configured_phase - 1U);

    measurement->phase_power[index] =
        measurement->power;

    measurement->phase_voltage[index] =
        measurement->voltage;

    measurement->phase_current[index] =
        measurement->current;

    return true;
}

/* Parses a Shelly Gen1 measurement. */
bool shelly_json_parse_gen1_measurement(
    const char *json,
    shelly_measurement_t *measurement)
{
    if ((json == NULL) ||
        (measurement == NULL))
    {
        return false;
    }

    memset(
        measurement,
        0,
        sizeof(*measurement));

    cJSON *root =
        cJSON_Parse(json);

    if (root == NULL)
    {
        logger_warn(
            TAG,
            "Invalid JSON");

        return false;
    }

    bool success =
        shelly_json_parse_gen1(
            root,
            measurement);

    cJSON_Delete(root);

    return success;
}

/* Parses a Shelly Gen2 V1 measurement. */
bool shelly_json_parse_gen2_v1_measurement(
    const char *json,
    shelly_measurement_t *measurement)
{
    if ((json == NULL) ||
        (measurement == NULL))
    {
        return false;
    }

    memset(
        measurement,
        0,
        sizeof(*measurement));

    cJSON *root =
        cJSON_Parse(json);

    if (root == NULL)
    {
        logger_warn(
            TAG,
            "Invalid JSON");

        return false;
    }

    bool success =
        shelly_json_parse_gen2_v1(
            root,
            measurement);

    cJSON_Delete(root);

    return success;
}

/* Parses a Shelly Gen2 V2 measurement. */
bool shelly_json_parse_gen2_v2_measurement(
    const char *json,
    shelly_measurement_t *measurement)
{
    if ((json == NULL) ||
        (measurement == NULL))
    {
        return false;
    }

    memset(
        measurement,
        0,
        sizeof(*measurement));

    cJSON *root =
        cJSON_Parse(json);

    if (root == NULL)
    {
        logger_warn(
            TAG,
            "Invalid JSON");

        return false;
    }

    bool success =
        shelly_json_parse_gen2_v2(
            root,
            measurement);

    cJSON_Delete(root);

    return success;
}

static bool shelly_json_parse_gen1(
    cJSON *root,
    shelly_measurement_t *measurement)
{
    cJSON *meters =
        cJSON_GetObjectItem(
            root,
            "meters");

    if (!cJSON_IsArray(meters))
    {
        return false;
    }

    cJSON *meter =
        cJSON_GetArrayItem(
            meters,
            0);

    if (meter == NULL)
    {
        return false;
    }

    cJSON *power =
        cJSON_GetObjectItem(
            meter,
            "power");

    cJSON *voltage =
        cJSON_GetObjectItem(
            meter,
            "voltage");

    cJSON *energy =
        cJSON_GetObjectItem(
            meter,
            "total");

    /* A valid current measurement is required for power management.
    * Gen1 does not provide current directly, so power and voltage
    * must be available to calculate it safely. */

    /* Reject missing, non-finite or unusable electrical values. */
    if (!cJSON_IsNumber(power) ||
        !cJSON_IsNumber(voltage) ||
        !isfinite(power->valuedouble) ||
        !isfinite(voltage->valuedouble) ||
        (voltage->valuedouble <= 1.0))
    {
        return false;
    }

    /* Store the validated active power. */
    measurement->power =
        power->valuedouble;

    /* Store the validated voltage. */
    measurement->voltage =
        voltage->valuedouble;

    /* Calculate the current from power and voltage. */
    measurement->current =
        measurement->power /
        measurement->voltage;

    /* Clamp negative current values to zero amperes. */
    if (measurement->current < 0.0f)
    {
        measurement->current =
            0.0f;
    }

    /* Store the imported energy only when the value is finite. */
    if (cJSON_IsNumber(energy) &&
        isfinite(energy->valuedouble))
    {
        measurement->energy =
            energy->valuedouble;
    }

    /* Preserve the single Gen1 channel in the configured phase slot. */
    return shelly_json_store_single_phase_details(
        measurement);
}

static bool shelly_json_parse_gen2_v1(
    cJSON *root,
    shelly_measurement_t *measurement)
{
    cJSON *sw =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "switch:0");

    if (!cJSON_IsObject(sw))
    {
        return false;
    }

    cJSON *power =
        cJSON_GetObjectItemCaseSensitive(
            sw,
            "apower");

    cJSON *voltage =
        cJSON_GetObjectItemCaseSensitive(
            sw,
            "voltage");

    cJSON *current =
        cJSON_GetObjectItemCaseSensitive(
            sw,
            "current");

    cJSON *energy =
        cJSON_GetObjectItemCaseSensitive(
            sw,
            "aenergy");

    /* A valid current measurement is required for power management.
    * If the current is unavailable, power and voltage must allow
    * the current to be calculated safely. */
    if (cJSON_IsNumber(current))
    {
        /* Reject a non-finite current value. */
        if (!isfinite(current->valuedouble))
        {
            return false;
        }
    }
    else
    {
        /* Reject missing, non-finite or unusable fallback values. */
        if (!cJSON_IsNumber(power) ||
            !cJSON_IsNumber(voltage) ||
            !isfinite(power->valuedouble) ||
            !isfinite(voltage->valuedouble) ||
            (voltage->valuedouble <= 1.0))
        {
            return false;
        }
    }

    /* Store the active power only when the value is finite. */
    if (cJSON_IsNumber(power) &&
        isfinite(power->valuedouble))
    {
        measurement->power =
            power->valuedouble;
    }

    /* Store the voltage only when the value is finite. */
    if (cJSON_IsNumber(voltage) &&
        isfinite(voltage->valuedouble))
    {
        measurement->voltage =
            voltage->valuedouble;
    }

    if (cJSON_IsNumber(current))
    {
        /* Store the measured current in amperes. */
        measurement->current =
            current->valuedouble;
    }
    else
    {
        /* Calculate the current from power and voltage. */
        measurement->current =
            measurement->power /
            measurement->voltage;
    }

    /* Clamp negative current values to zero amperes. */
    if (measurement->current < 0.0f)
    {
        measurement->current =
            0.0f;
    }

    if (cJSON_IsObject(energy))
    {
        cJSON *total =
            cJSON_GetObjectItemCaseSensitive(
                energy,
                "total");

        /* Store the imported energy only when the value is finite. */
        if (cJSON_IsNumber(total) &&
            isfinite(total->valuedouble))
        {
            measurement->energy =
                total->valuedouble;
        }
    }

    /* Preserve the single Gen2 V1 channel in the configured phase slot. */
    return shelly_json_store_single_phase_details(
        measurement);
}

/*
 * Parses the three electrical channels returned by EM.GetStatus.
 *
 * In triphase mode, all three channels are required and the legacy current
 * field receives the highest phase current for breaker protection.
 *
 * In monophase mode, only the configured Shelly phase is required.
 */
static bool shelly_json_parse_gen2_v2(
    cJSON *root,
    shelly_measurement_t *measurement)
{
    /* The POST RPC response contains EM.GetStatus fields in "result". */
    cJSON *result =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "result");

    if (!cJSON_IsObject(result))
    {
        return false;
    }

    const uint8_t configured_phase =
        config_get()->shelly_phase;

    if (configured_phase > SHELLY_PHASE_COUNT)
    {
        return false;
    }

    static const char *current_fields[SHELLY_PHASE_COUNT] =
    {
        "a_current",
        "b_current",
        "c_current"
    };

    static const char *voltage_fields[SHELLY_PHASE_COUNT] =
    {
        "a_voltage",
        "b_voltage",
        "c_voltage"
    };

    static const char *power_fields[SHELLY_PHASE_COUNT] =
    {
        "a_act_power",
        "b_act_power",
        "c_act_power"
    };

    float highest_current =
        0.0f;

    /*
     * Read either all three phases or only the configured monophase source.
     * The output structure was cleared before entering this parser, therefore
     * unused phase slots remain at zero.
     */
    for (unsigned i = 0U;
         i < SHELLY_PHASE_COUNT;
         i++)
    {
        if ((configured_phase != 0U) &&
            (i != (unsigned)(configured_phase - 1U)))
        {
            continue;
        }

        cJSON *current =
            cJSON_GetObjectItemCaseSensitive(
                result,
                current_fields[i]);

        cJSON *power =
            cJSON_GetObjectItemCaseSensitive(
                result,
                power_fields[i]);

        cJSON *voltage =
            cJSON_GetObjectItemCaseSensitive(
                result,
                voltage_fields[i]);

        /* Current and active power are mandatory for an active phase. */
        if (!cJSON_IsNumber(current) ||
            !isfinite(current->valuedouble) ||
            (current->valuedouble < 0.0))
        {
            return false;
        }

        if (!cJSON_IsNumber(power) ||
            !isfinite(power->valuedouble))
        {
            return false;
        }

        measurement->phase_current[i] =
            (float)current->valuedouble;

        measurement->phase_power[i] =
            (float)power->valuedouble;

        /* Voltage is optional because current is supplied directly. */
        if (cJSON_IsNumber(voltage) &&
            isfinite(voltage->valuedouble) &&
            (voltage->valuedouble > 1.0))
        {
            measurement->phase_voltage[i] =
                (float)voltage->valuedouble;
        }

        highest_current =
            fmaxf(
                highest_current,
                measurement->phase_current[i]);
    }

    /*
     * Keep the legacy current field compatible with power_manager:
     * triphase uses the most loaded phase and monophase uses the selected one.
     */
    measurement->current =
        highest_current;

    if (configured_phase == 0U)
    {
        cJSON *total_power =
            cJSON_GetObjectItemCaseSensitive(
                result,
                "total_act_power");

        if (!cJSON_IsNumber(total_power) ||
            !isfinite(total_power->valuedouble))
        {
            return false;
        }

        /* Preserve the Shelly-provided aggregate power for existing users. */
        measurement->power =
            (float)total_power->valuedouble;

        /* Use phase A voltage as the legacy triphase reference voltage. */
        measurement->voltage =
            measurement->phase_voltage[0];
    }
    else
    {
        const unsigned selected_index =
            (unsigned)(configured_phase - 1U);

        /* Preserve the selected phase in the legacy aggregate fields. */
        measurement->power =
            measurement->phase_power[selected_index];

        measurement->voltage =
            measurement->phase_voltage[selected_index];
    }

    /* EM.GetStatus does not provide the energy counter used by this module. */
    measurement->energy =
        0.0f;

    return true;
}
