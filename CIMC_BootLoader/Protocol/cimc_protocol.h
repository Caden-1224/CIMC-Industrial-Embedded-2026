#ifndef CIMC_PROTOCOL_H
#define CIMC_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define CIMC_FRAME_START_FLAG       0xA5B6U
#define CIMC_FRAME_END_FLAG         0xB6A5U
#define CIMC_PROTOCOL_VERSION       0x02U

#define CIMC_DEVICE_ID_BROADCAST    0xFFFFU
#define CIMC_DEVICE_ID_MIN          0x0001U
#define CIMC_DEVICE_ID_MAX          0xFFFEU

#define CIMC_FRAME_TYPE_COMMAND     0x01U
#define CIMC_FRAME_TYPE_RESPONSE    0x02U
#define CIMC_FRAME_TYPE_HEARTBEAT   0x05U
#define CIMC_FRAME_TYPE_ERROR       0xFFU

#define CIMC_CMD_HEARTBEAT          0x8888U
#define CIMC_CMD_DISCOVERY          0xFFFFU
#define CIMC_CMD_ERROR              0xEEEEU

#define CIMC_RESPONSE_OK            0xFFU

#define CIMC_FRAME_FIXED_SIZE       13U
#define CIMC_FRAME_MAX_CONTENT_SIZE 255U
#define CIMC_FRAME_MAX_SIZE         (CIMC_FRAME_FIXED_SIZE + CIMC_FRAME_MAX_CONTENT_SIZE)
#define CIMC_ASCII_FRAME_MAX_SIZE   (CIMC_FRAME_MAX_SIZE * 2U)

typedef enum
{
    CIMC_PARSE_OK = 0,
    CIMC_PARSE_NULL,
    CIMC_PARSE_ASCII_ODD_LENGTH,
    CIMC_PARSE_ASCII_INVALID_CHAR,
    CIMC_PARSE_OUTPUT_TOO_SMALL,
    CIMC_PARSE_FRAME_TOO_SHORT,
    CIMC_PARSE_FRAME_TOO_LONG,
    CIMC_PARSE_BAD_START,
    CIMC_PARSE_BAD_END,
    CIMC_PARSE_LENGTH_MISMATCH,
    CIMC_PARSE_VERSION_MISMATCH,
    CIMC_PARSE_CRC_MISMATCH
} cimc_parse_status_t;

typedef struct
{
    uint16_t device_id;
    uint8_t frame_type;
    uint16_t command;
    uint8_t content_len;
    uint8_t protocol_version;
    uint8_t content[CIMC_FRAME_MAX_CONTENT_SIZE];
} cimc_frame_t;

/**
 * @brief Calculate CRC-16-Modbus over one protocol byte range.
 * @param data First byte of the protocol range to check.
 * @param len Number of bytes included in the calculation.
 * @return Reflected Modbus CRC value before transport byte-order conversion.
 */
uint16_t CimcProtocol_Crc16Modbus(const uint8_t *data, uint16_t len);

/**
 * @brief Decode one ASCII-Hex transport frame into raw protocol bytes.
 * @param ascii First ASCII byte received from USART1 RS485.
 * @param ascii_len Number of ASCII bytes to decode. Whitespace must be trimmed by the caller.
 * @param out_bytes Output buffer that receives the binary protocol frame.
 * @param out_capacity Writable size of out_bytes.
 * @param out_len Decoded binary frame length on success.
 * @return Parse status describing success or the first transport-level failure.
 */
cimc_parse_status_t CimcProtocol_DecodeAsciiHex(const uint8_t *ascii,
                                                uint16_t ascii_len,
                                                uint8_t *out_bytes,
                                                uint16_t out_capacity,
                                                uint16_t *out_len);

/**
 * @brief Encode one raw protocol frame as uppercase ASCII-Hex for USART transport.
 * @param bytes Binary frame to encode.
 * @param bytes_len Number of binary bytes.
 * @param out_ascii Output buffer receiving uppercase ASCII characters.
 * @param out_capacity Writable size of out_ascii.
 * @param out_len Encoded ASCII length on success.
 * @return Parse status. CIMC_PARSE_OUTPUT_TOO_SMALL means out_ascii is too small.
 */
cimc_parse_status_t CimcProtocol_EncodeAsciiHex(const uint8_t *bytes,
                                                uint16_t bytes_len,
                                                uint8_t *out_ascii,
                                                uint16_t out_capacity,
                                                uint16_t *out_len);

/**
 * @brief Parse and validate one binary CIMC protocol frame.
 * @param bytes Binary frame after ASCII-Hex decoding.
 * @param bytes_len Binary frame length.
 * @param frame Parsed business frame fields on success.
 * @return Parse status. CRC, length, flag, and protocol-version errors are separated for K-series tests.
 */
cimc_parse_status_t CimcProtocol_ParseFrame(const uint8_t *bytes,
                                            uint16_t bytes_len,
                                            cimc_frame_t *frame);

/**
 * @brief Build one binary CIMC protocol frame.
 * @param device_id Response or target device ID.
 * @param frame_type CIMC frame type, for example CIMC_FRAME_TYPE_RESPONSE.
 * @param command Command word carried by the frame.
 * @param content Payload bytes. May be NULL only when content_len is zero.
 * @param content_len Payload byte count.
 * @param out_bytes Output buffer receiving the binary frame.
 * @param out_capacity Writable size of out_bytes.
 * @param out_len Binary frame length on success.
 * @return Parse status. Output capacity and payload-size errors are reported explicitly.
 */
cimc_parse_status_t CimcProtocol_BuildFrame(uint16_t device_id,
                                            uint8_t frame_type,
                                            uint16_t command,
                                            const uint8_t *content,
                                            uint8_t content_len,
                                            uint8_t *out_bytes,
                                            uint16_t out_capacity,
                                            uint16_t *out_len);

/**
 * @brief Build an OK response frame whose payload is the contest-defined 0xFF byte.
 * @param device_id Device ID that should appear in the response.
 * @param command Command being acknowledged.
 * @param out_bytes Output buffer receiving the binary frame.
 * @param out_capacity Writable size of out_bytes.
 * @param out_len Binary frame length on success.
 * @return Parse status from CimcProtocol_BuildFrame.
 */
cimc_parse_status_t CimcProtocol_BuildOkFrame(uint16_t device_id,
                                              uint16_t command,
                                              uint8_t *out_bytes,
                                              uint16_t out_capacity,
                                              uint16_t *out_len);

/**
 * @brief Build a contest error response frame.
 * @param device_id Device ID that should appear in the response.
 * @param command Error command word, normally CIMC_CMD_ERROR or the original command for OTA.
 * @param out_bytes Output buffer receiving the binary frame.
 * @param out_capacity Writable size of out_bytes.
 * @param out_len Binary frame length on success.
 * @return Parse status from CimcProtocol_BuildFrame.
 */
cimc_parse_status_t CimcProtocol_BuildErrorFrame(uint16_t device_id,
                                                 uint16_t command,
                                                 uint8_t *out_bytes,
                                                 uint16_t out_capacity,
                                                 uint16_t *out_len);

/**
 * @brief Continue a reflected CRC32 calculation with one more byte range.
 * @param crc Current CRC accumulator. Use 0xFFFFFFFF before the first chunk.
 * @param data First byte of the next payload chunk.
 * @param len Number of bytes in this chunk.
 * @return Updated, non-finalized CRC accumulator.
 */
uint32_t CimcProtocol_Crc32Update(uint32_t crc, const uint8_t *data, uint32_t len);

/**
 * @brief Finalize a reflected CRC32 accumulator.
 * @param crc Current accumulator returned by CimcProtocol_Crc32Update.
 * @return Final CRC32 value used by the OTA metadata and Bootloader checks.
 */
uint32_t CimcProtocol_Crc32Finalize(uint32_t crc);

/**
 * @brief Calculate a full reflected CRC32 in one call.
 * @param data First byte of the checked byte range.
 * @param len Number of bytes included in the calculation.
 * @return Final CRC32 value.
 */
uint32_t CimcProtocol_Crc32Calc(const uint8_t *data, uint32_t len);

/**
 * @brief Read a big-endian 16-bit protocol field.
 * @param data Two-byte field stored in transport order.
 * @return Host-order unsigned value.
 */
uint16_t CimcProtocol_ReadBe16(const uint8_t *data);

/**
 * @brief Read a big-endian 32-bit protocol field.
 * @param data Four-byte field stored in transport order.
 * @return Host-order unsigned value.
 */
uint32_t CimcProtocol_ReadBe32(const uint8_t *data);

/**
 * @brief Write a big-endian 16-bit protocol field.
 * @param data Two-byte output buffer.
 * @param value Host-order unsigned value to encode.
 */
void CimcProtocol_WriteBe16(uint8_t *data, uint16_t value);

/**
 * @brief Write a big-endian 32-bit protocol field.
 * @param data Four-byte output buffer.
 * @param value Host-order unsigned value to encode.
 */
void CimcProtocol_WriteBe32(uint8_t *data, uint32_t value);

/**
 * @brief Write one IEEE754 single-precision value in big-endian byte order.
 * @param data Four-byte output buffer.
 * @param value Floating-point business value to encode for the contest protocol.
 */
void CimcProtocol_WriteFloatBe(uint8_t *data, float value);

#endif /* CIMC_PROTOCOL_H */
