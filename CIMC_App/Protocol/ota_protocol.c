#include "ota_protocol.h"
#include "bl_partition.h"
#include "cimc_protocol.h"
#include <stddef.h>

bool OtaProtocol_IsStreamHeaderValid(const ota_stream_header_t *stream)
{
    uint32_t crc_expect;

    if (stream == 0)
    {
        return false;
    }

    if ((stream->magic != OTA_FRAME_MAGIC) ||
        (stream->header_version != OTA_STREAM_HEADER_VERSION) ||
        (stream->header_size != sizeof(ota_stream_header_t)) ||
        (stream->target_addr != BL_APP1_START_ADDR) ||
        (stream->image_type != OTA_IMAGE_TYPE_APP1) ||
        (stream->app_size == 0UL) ||
        (stream->app_size > BL_APP2_SIZE))
    {
        return false;
    }

    crc_expect = CimcProtocol_Crc32Calc((const uint8_t *)stream,
                                        (uint32_t)offsetof(ota_stream_header_t, header_crc32));
    return (crc_expect == stream->header_crc32);
}
