#pragma once

#include "CANopen.h"

namespace robot_control::communication::canopen {

/**
 * Close a borrowed SDO client and discard only its unsent upstream buffer.
 * @param client Initialized client owned by the calling lifecycle thread.
 * No CAN transmission occurs. Other buffers and their pending count are retained.
 * This cannot retract a frame already submitted to the kernel or controller.
 */
inline void close_sdo_client(CO_SDOclient_t& client) noexcept {
    CO_SDOclientClose(&client);
    CO_FLAG_CLEAR(client.CANrxNew);
    if (client.CANtxBuff != nullptr && client.CANtxBuff->bufferFull) {
        client.CANtxBuff->bufferFull = false;
        if (client.CANdevTx != nullptr && client.CANdevTx->CANtxCount > 0U) {
            client.CANdevTx->CANtxCount = static_cast<uint16_t>(client.CANdevTx->CANtxCount - 1U);
        }
    }
}

} // namespace robot_control::communication::canopen
