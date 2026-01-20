#include "wire_protocol.h"
#include <string.h>

/*
 * CRC-16/CCITT-FALSE lookup table
 * Poly: 0x1021, Init: 0xFFFF
 */
static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

uint16_t wire_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ data[i]) & 0xFF];
    }
    return crc;
}

size_t wire_build_frame(
    uint8_t *out_buf,
    size_t out_buf_size,
    uint8_t msg_type,
    uint16_t seq,
    const uint8_t *payload,
    uint16_t payload_len)
{
    size_t total_len = WIRE_HEADER_SIZE + payload_len + WIRE_CRC_SIZE;
    if (out_buf_size < total_len || payload_len > WIRE_MAX_PAYLOAD) {
        return 0;
    }

    // Build header (little-endian)
    out_buf[0] = WIRE_PROTO_VERSION;
    out_buf[1] = msg_type;
    out_buf[2] = seq & 0xFF;
    out_buf[3] = (seq >> 8) & 0xFF;
    out_buf[4] = payload_len & 0xFF;
    out_buf[5] = (payload_len >> 8) & 0xFF;

    // Copy payload
    if (payload && payload_len > 0) {
        memcpy(&out_buf[WIRE_HEADER_SIZE], payload, payload_len);
    }

    // Compute CRC over header + payload
    uint16_t crc = wire_crc16(out_buf, WIRE_HEADER_SIZE + payload_len);

    // Append CRC (little-endian)
    out_buf[WIRE_HEADER_SIZE + payload_len] = crc & 0xFF;
    out_buf[WIRE_HEADER_SIZE + payload_len + 1] = (crc >> 8) & 0xFF;

    return total_len;
}

bool wire_parse_frame(
    const uint8_t *frame_buf,
    size_t frame_len,
    wire_frame_header_t *header,
    const uint8_t **payload_out)
{
    if (!frame_buf || !header || frame_len < WIRE_HEADER_SIZE + WIRE_CRC_SIZE) {
        return false;
    }

    // Parse header (little-endian)
    header->proto_ver = frame_buf[0];
    header->msg_type = frame_buf[1];
    header->seq = frame_buf[2] | ((uint16_t)frame_buf[3] << 8);
    header->payload_len = frame_buf[4] | ((uint16_t)frame_buf[5] << 8);

    // Validate protocol version
    if (header->proto_ver != WIRE_PROTO_VERSION) {
        return false;
    }

    // Validate frame length
    size_t expected_len = WIRE_HEADER_SIZE + header->payload_len + WIRE_CRC_SIZE;
    if (frame_len < expected_len || header->payload_len > WIRE_MAX_PAYLOAD) {
        return false;
    }

    // Extract and verify CRC
    size_t crc_offset = WIRE_HEADER_SIZE + header->payload_len;
    uint16_t received_crc = frame_buf[crc_offset] | ((uint16_t)frame_buf[crc_offset + 1] << 8);
    uint16_t computed_crc = wire_crc16(frame_buf, crc_offset);

    if (received_crc != computed_crc) {
        return false;
    }

    // Set payload pointer
    if (payload_out) {
        *payload_out = (header->payload_len > 0) ? &frame_buf[WIRE_HEADER_SIZE] : NULL;
    }

    return true;
}

size_t wire_build_cmd_ack(
    uint8_t *out_buf,
    size_t out_buf_size,
    uint16_t seq,
    uint16_t acked_seq,
    uint16_t cmd_id,
    uint8_t status,
    uint16_t detail,
    const uint8_t *optional_data,
    size_t optional_len)
{
    uint8_t payload[WIRE_MAX_PAYLOAD];
    size_t payload_len = sizeof(wire_cmd_ack_t) + optional_len;

    if (payload_len > WIRE_MAX_PAYLOAD) {
        return 0;
    }

    // Build ACK payload (little-endian)
    payload[0] = acked_seq & 0xFF;
    payload[1] = (acked_seq >> 8) & 0xFF;
    payload[2] = cmd_id & 0xFF;
    payload[3] = (cmd_id >> 8) & 0xFF;
    payload[4] = status;
    payload[5] = detail & 0xFF;
    payload[6] = (detail >> 8) & 0xFF;

    if (optional_data && optional_len > 0) {
        memcpy(&payload[sizeof(wire_cmd_ack_t)], optional_data, optional_len);
    }

    return wire_build_frame(out_buf, out_buf_size, MSG_TYPE_COMMAND_ACK, seq, payload, payload_len);
}

size_t wire_build_telemetry(
    uint8_t *out_buf,
    size_t out_buf_size,
    uint16_t seq,
    uint32_t timestamp_ms,
    uint16_t di_bits,
    uint16_t ro_bits,
    uint32_t alarm_bits,
    const wire_controller_data_t *controllers,
    uint8_t controller_count)
{
    uint8_t payload[WIRE_MAX_PAYLOAD];
    size_t payload_len = sizeof(wire_telemetry_header_t) +
                         controller_count * sizeof(wire_controller_data_t);

    if (payload_len > WIRE_MAX_PAYLOAD || controller_count > 3) {
        return 0;
    }

    // Build telemetry header (little-endian)
    size_t offset = 0;

    payload[offset++] = timestamp_ms & 0xFF;
    payload[offset++] = (timestamp_ms >> 8) & 0xFF;
    payload[offset++] = (timestamp_ms >> 16) & 0xFF;
    payload[offset++] = (timestamp_ms >> 24) & 0xFF;

    payload[offset++] = di_bits & 0xFF;
    payload[offset++] = (di_bits >> 8) & 0xFF;

    payload[offset++] = ro_bits & 0xFF;
    payload[offset++] = (ro_bits >> 8) & 0xFF;

    payload[offset++] = alarm_bits & 0xFF;
    payload[offset++] = (alarm_bits >> 8) & 0xFF;
    payload[offset++] = (alarm_bits >> 16) & 0xFF;
    payload[offset++] = (alarm_bits >> 24) & 0xFF;

    payload[offset++] = controller_count;

    // Append controller data
    for (uint8_t i = 0; i < controller_count; i++) {
        const wire_controller_data_t *c = &controllers[i];
        payload[offset++] = c->controller_id;
        payload[offset++] = c->pv_x10 & 0xFF;
        payload[offset++] = (c->pv_x10 >> 8) & 0xFF;
        payload[offset++] = c->sv_x10 & 0xFF;
        payload[offset++] = (c->sv_x10 >> 8) & 0xFF;
        payload[offset++] = c->op_x10 & 0xFF;
        payload[offset++] = (c->op_x10 >> 8) & 0xFF;
        payload[offset++] = c->mode;
        payload[offset++] = c->age_ms & 0xFF;
        payload[offset++] = (c->age_ms >> 8) & 0xFF;
    }

    return wire_build_frame(out_buf, out_buf_size, MSG_TYPE_TELEMETRY_SNAPSHOT, seq, payload, offset);
}

size_t wire_build_telemetry_ext(
    uint8_t *out_buf,
    size_t out_buf_size,
    uint16_t seq,
    uint32_t timestamp_ms,
    uint16_t di_bits,
    uint16_t ro_bits,
    uint32_t alarm_bits,
    const wire_controller_data_t *controllers,
    uint8_t controller_count,
    const wire_telemetry_run_state_t *run_state)
{
    uint8_t payload[WIRE_MAX_PAYLOAD];
    size_t base_len = sizeof(wire_telemetry_header_t) +
                      controller_count * sizeof(wire_controller_data_t);
    size_t ext_len = (run_state != NULL) ? sizeof(wire_telemetry_run_state_t) : 0;
    size_t payload_len = base_len + ext_len;

    if (payload_len > WIRE_MAX_PAYLOAD || controller_count > 3) {
        return 0;
    }

    // Build telemetry header (little-endian)
    size_t offset = 0;

    payload[offset++] = timestamp_ms & 0xFF;
    payload[offset++] = (timestamp_ms >> 8) & 0xFF;
    payload[offset++] = (timestamp_ms >> 16) & 0xFF;
    payload[offset++] = (timestamp_ms >> 24) & 0xFF;

    payload[offset++] = di_bits & 0xFF;
    payload[offset++] = (di_bits >> 8) & 0xFF;

    payload[offset++] = ro_bits & 0xFF;
    payload[offset++] = (ro_bits >> 8) & 0xFF;

    payload[offset++] = alarm_bits & 0xFF;
    payload[offset++] = (alarm_bits >> 8) & 0xFF;
    payload[offset++] = (alarm_bits >> 16) & 0xFF;
    payload[offset++] = (alarm_bits >> 24) & 0xFF;

    payload[offset++] = controller_count;

    // Append controller data
    for (uint8_t i = 0; i < controller_count; i++) {
        const wire_controller_data_t *c = &controllers[i];
        payload[offset++] = c->controller_id;
        payload[offset++] = c->pv_x10 & 0xFF;
        payload[offset++] = (c->pv_x10 >> 8) & 0xFF;
        payload[offset++] = c->sv_x10 & 0xFF;
        payload[offset++] = (c->sv_x10 >> 8) & 0xFF;
        payload[offset++] = c->op_x10 & 0xFF;
        payload[offset++] = (c->op_x10 >> 8) & 0xFF;
        payload[offset++] = c->mode;
        payload[offset++] = c->age_ms & 0xFF;
        payload[offset++] = (c->age_ms >> 8) & 0xFF;
    }

    // Append extended run state if provided (16 bytes total)
    if (run_state != NULL) {
        payload[offset++] = run_state->machine_state;

        payload[offset++] = run_state->run_elapsed_ms & 0xFF;
        payload[offset++] = (run_state->run_elapsed_ms >> 8) & 0xFF;
        payload[offset++] = (run_state->run_elapsed_ms >> 16) & 0xFF;
        payload[offset++] = (run_state->run_elapsed_ms >> 24) & 0xFF;

        payload[offset++] = run_state->run_remaining_ms & 0xFF;
        payload[offset++] = (run_state->run_remaining_ms >> 8) & 0xFF;
        payload[offset++] = (run_state->run_remaining_ms >> 16) & 0xFF;
        payload[offset++] = (run_state->run_remaining_ms >> 24) & 0xFF;

        payload[offset++] = run_state->target_temp_x10 & 0xFF;
        payload[offset++] = (run_state->target_temp_x10 >> 8) & 0xFF;

        payload[offset++] = run_state->recipe_step;
        payload[offset++] = run_state->interlock_bits;
        payload[offset++] = run_state->lazy_poll_active;
        payload[offset++] = run_state->idle_timeout_min;
        payload[offset++] = run_state->reserved;  // offset 15 - padding to 16 bytes
    }

    return wire_build_frame(out_buf, out_buf_size, MSG_TYPE_TELEMETRY_SNAPSHOT, seq, payload, offset);
}

size_t wire_build_event(
    uint8_t *out_buf,
    size_t out_buf_size,
    uint16_t seq,
    uint16_t event_id,
    uint8_t severity,
    uint8_t source,
    const uint8_t *event_data,
    size_t event_data_len)
{
    uint8_t payload[WIRE_MAX_PAYLOAD];
    size_t payload_len = sizeof(wire_event_header_t) + event_data_len;

    if (payload_len > WIRE_MAX_PAYLOAD) {
        return 0;
    }

    // Build event header (little-endian)
    payload[0] = event_id & 0xFF;
    payload[1] = (event_id >> 8) & 0xFF;
    payload[2] = severity;
    payload[3] = source;

    if (event_data && event_data_len > 0) {
        memcpy(&payload[sizeof(wire_event_header_t)], event_data, event_data_len);
    }

    return wire_build_frame(out_buf, out_buf_size, MSG_TYPE_EVENT, seq, payload, payload_len);
}
