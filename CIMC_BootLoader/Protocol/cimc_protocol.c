#include "cimc_protocol.h"
#include <string.h>

static int8_t CimcProtocol_HexNibble(uint8_t value)
{
    if ((value >= (uint8_t)'0') && (value <= (uint8_t)'9'))
    {
        return (int8_t)(value - (uint8_t)'0');
    }

    if ((value >= (uint8_t)'A') && (value <= (uint8_t)'F'))
    {
        return (int8_t)(value - (uint8_t)'A' + 10U);
    }

    if ((value >= (uint8_t)'a') && (value <= (uint8_t)'f'))
    {
        return (int8_t)(value - (uint8_t)'a' + 10U);
    }

    return -1;
}

static uint8_t CimcProtocol_HexChar(uint8_t value)
{
    static const uint8_t hex_chars[] = "0123456789ABCDEF";

    return hex_chars[value & 0x0FU];
}

uint16_t CimcProtocol_Crc16Modbus(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit_index;

    if ((data == 0) || (len == 0U))
    {
        return crc;
    }

    for (index = 0U; index < len; index++)
    {
        crc ^= data[index];
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc = (uint16_t)(crc >> 1U);
            }
        }
    }

    return crc;
}

cimc_parse_status_t CimcProtocol_DecodeAsciiHex(const uint8_t *ascii,
                                                uint16_t ascii_len,
                                                uint8_t *out_bytes,
                                                uint16_t out_capacity,
                                                uint16_t *out_len)
{
    uint16_t ascii_index;
    uint16_t out_index;
    int8_t high;
    int8_t low;

    if ((ascii == 0) || (out_bytes == 0) || (out_len == 0))
    {
        return CIMC_PARSE_NULL;
    }

    *out_len = 0U;

    /*
     * The contest transport is ASCII-Hex: every protocol byte is carried by
     * two printable hex characters. Reject odd lengths before touching CRC so
     * K-series errors can be diagnosed at the correct layer.
     */
    if ((ascii_len & 1U) != 0U)
    {
        return CIMC_PARSE_ASCII_ODD_LENGTH;
    }

    if ((ascii_len / 2U) > out_capacity)
    {
        return CIMC_PARSE_OUTPUT_TOO_SMALL;
    }

    out_index = 0U;
    for (ascii_index = 0U; ascii_index < ascii_len; ascii_index += 2U)
    {
        high = CimcProtocol_HexNibble(ascii[ascii_index]);
        low = CimcProtocol_HexNibble(ascii[ascii_index + 1U]);
        if ((high < 0) || (low < 0))
        {
            return CIMC_PARSE_ASCII_INVALID_CHAR;
        }

        out_bytes[out_index] = (uint8_t)(((uint8_t)high << 4U) | (uint8_t)low);
        out_index++;
    }

    *out_len = out_index;
    return CIMC_PARSE_OK;
}

cimc_parse_status_t CimcProtocol_EncodeAsciiHex(const uint8_t *bytes,
                                                uint16_t bytes_len,
                                                uint8_t *out_ascii,
                                                uint16_t out_capacity,
                                                uint16_t *out_len)
{
    uint16_t index;
    uint16_t ascii_len;

    if ((bytes == 0) || (out_ascii == 0) || (out_len == 0))
    {
        return CIMC_PARSE_NULL;
    }

    *out_len = 0U;
    ascii_len = (uint16_t)(bytes_len * 2U);
    if (ascii_len > out_capacity)
    {
        return CIMC_PARSE_OUTPUT_TOO_SMALL;
    }

    for (index = 0U; index < bytes_len; index++)
    {
        out_ascii[index * 2U] = CimcProtocol_HexChar((uint8_t)(bytes[index] >> 4U));
        out_ascii[(index * 2U) + 1U] = CimcProtocol_HexChar(bytes[index]);
    }

    *out_len = ascii_len;
    return CIMC_PARSE_OK;
}

cimc_parse_status_t CimcProtocol_ParseFrame(const uint8_t *bytes,
                                            uint16_t bytes_len,
                                            cimc_frame_t *frame)
{
    uint8_t content_len;
    uint16_t expected_len;
    uint16_t received_crc;
    uint16_t calculated_crc;

    if ((bytes == 0) || (frame == 0))
    {
        return CIMC_PARSE_NULL;
    }

    memset(frame, 0, sizeof(cimc_frame_t));

    if (bytes_len < CIMC_FRAME_FIXED_SIZE)
    {
        return CIMC_PARSE_FRAME_TOO_SHORT;
    }
    if (bytes_len > CIMC_FRAME_MAX_SIZE)
    {
        return CIMC_PARSE_FRAME_TOO_LONG;
    }

    if (CimcProtocol_ReadBe16(&bytes[0]) != CIMC_FRAME_START_FLAG)
    {
        return CIMC_PARSE_BAD_START;
    }
    if (CimcProtocol_ReadBe16(&bytes[bytes_len - 2U]) != CIMC_FRAME_END_FLAG)
    {
        return CIMC_PARSE_BAD_END;
    }

    content_len = bytes[7];
    expected_len = (uint16_t)(CIMC_FRAME_FIXED_SIZE + content_len);
    if (bytes_len != expected_len)
    {
        return CIMC_PARSE_LENGTH_MISMATCH;
    }

    if (bytes[8] != CIMC_PROTOCOL_VERSION)
    {
        return CIMC_PARSE_VERSION_MISMATCH;
    }

    received_crc = CimcProtocol_ReadBe16(&bytes[9U + content_len]);
    calculated_crc = CimcProtocol_Crc16Modbus(bytes, (uint16_t)(9U + content_len));
    if (received_crc != calculated_crc)
    {
        return CIMC_PARSE_CRC_MISMATCH;
    }

    frame->device_id = CimcProtocol_ReadBe16(&bytes[2]);
    frame->frame_type = bytes[4];
    frame->command = CimcProtocol_ReadBe16(&bytes[5]);
    frame->content_len = content_len;
    frame->protocol_version = bytes[8];
    if (content_len > 0U)
    {
        memcpy(frame->content, &bytes[9], content_len);
    }

    return CIMC_PARSE_OK;
}

cimc_parse_status_t CimcProtocol_BuildFrame(uint16_t device_id,
                                            uint8_t frame_type,
                                            uint16_t command,
                                            const uint8_t *content,
                                            uint8_t content_len,
                                            uint8_t *out_bytes,
                                            uint16_t out_capacity,
                                            uint16_t *out_len)
{
    uint16_t frame_len;
    uint16_t crc;

    if ((out_bytes == 0) || (out_len == 0))
    {
        return CIMC_PARSE_NULL;
    }
    if ((content_len > 0U) && (content == 0))
    {
        return CIMC_PARSE_NULL;
    }

    *out_len = 0U;
    frame_len = (uint16_t)(CIMC_FRAME_FIXED_SIZE + content_len);
    if (frame_len > out_capacity)
    {
        return CIMC_PARSE_OUTPUT_TOO_SMALL;
    }

    CimcProtocol_WriteBe16(&out_bytes[0], CIMC_FRAME_START_FLAG);
    CimcProtocol_WriteBe16(&out_bytes[2], device_id);
    out_bytes[4] = frame_type;
    CimcProtocol_WriteBe16(&out_bytes[5], command);
    out_bytes[7] = content_len;
    out_bytes[8] = CIMC_PROTOCOL_VERSION;

    if (content_len > 0U)
    {
        memcpy(&out_bytes[9], content, content_len);
    }

    crc = CimcProtocol_Crc16Modbus(out_bytes, (uint16_t)(9U + content_len));
    CimcProtocol_WriteBe16(&out_bytes[9U + content_len], crc);
    CimcProtocol_WriteBe16(&out_bytes[11U + content_len], CIMC_FRAME_END_FLAG);

    *out_len = frame_len;
    return CIMC_PARSE_OK;
}

cimc_parse_status_t CimcProtocol_BuildOkFrame(uint16_t device_id,
                                              uint16_t command,
                                              uint8_t *out_bytes,
                                              uint16_t out_capacity,
                                              uint16_t *out_len)
{
    const uint8_t ok_content = CIMC_RESPONSE_OK;

    return CimcProtocol_BuildFrame(device_id,
                                   CIMC_FRAME_TYPE_RESPONSE,
                                   command,
                                   &ok_content,
                                   1U,
                                   out_bytes,
                                   out_capacity,
                                   out_len);
}

cimc_parse_status_t CimcProtocol_BuildErrorFrame(uint16_t device_id,
                                                 uint16_t command,
                                                 uint8_t *out_bytes,
                                                 uint16_t out_capacity,
                                                 uint16_t *out_len)
{
    return CimcProtocol_BuildFrame(device_id,
                                   CIMC_FRAME_TYPE_ERROR,
                                   command,
                                   0,
                                   0U,
                                   out_bytes,
                                   out_capacity,
                                   out_len);
}

uint32_t CimcProtocol_Crc32Update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t index;
    uint32_t bit_index;

    if ((data == 0) || (len == 0UL))
    {
        return crc;
    }

    for (index = 0UL; index < len; index++)
    {
        crc ^= data[index];
        for (bit_index = 0UL; bit_index < 8UL; bit_index++)
        {
            if ((crc & 1UL) != 0UL)
            {
                crc = (crc >> 1UL) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1UL;
            }
        }
    }

    return crc;
}

uint32_t CimcProtocol_Crc32Finalize(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

uint32_t CimcProtocol_Crc32Calc(const uint8_t *data, uint32_t len)
{
    return CimcProtocol_Crc32Finalize(CimcProtocol_Crc32Update(0xFFFFFFFFUL, data, len));
}

uint16_t CimcProtocol_ReadBe16(const uint8_t *data)
{
    if (data == 0)
    {
        return 0U;
    }

    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

uint32_t CimcProtocol_ReadBe32(const uint8_t *data)
{
    if (data == 0)
    {
        return 0UL;
    }

    return (((uint32_t)data[0] << 24U) |
            ((uint32_t)data[1] << 16U) |
            ((uint32_t)data[2] << 8U) |
            (uint32_t)data[3]);
}

void CimcProtocol_WriteBe16(uint8_t *data, uint16_t value)
{
    if (data == 0)
    {
        return;
    }

    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)(value & 0xFFU);
}

void CimcProtocol_WriteBe32(uint8_t *data, uint32_t value)
{
    if (data == 0)
    {
        return;
    }

    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)((value >> 16U) & 0xFFU);
    data[2] = (uint8_t)((value >> 8U) & 0xFFU);
    data[3] = (uint8_t)(value & 0xFFU);
}

void CimcProtocol_WriteFloatBe(uint8_t *data, float value)
{
    uint32_t raw;

    if (data == 0)
    {
        return;
    }

    memcpy(&raw, &value, sizeof(raw));
    CimcProtocol_WriteBe32(data, raw);
}
