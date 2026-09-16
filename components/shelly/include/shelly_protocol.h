#ifndef SHELLY_PROTOCOL_H
#define SHELLY_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Shelly Gen1 REST API. */
#define SHELLY_URI_GEN1_INFO \
    "/shelly"

#define SHELLY_URI_GEN1_STATUS \
    "/status"

/* Shelly Gen2 RPC endpoint. */
#define SHELLY_URI_GEN2_RPC \
    "/rpc"

/* Shelly Gen2 RPC requests. */
#define SHELLY_RPC_DEVICE_INFO \
    "{\"id\":1,\"method\":\"Shelly.GetDeviceInfo\"}"

#define SHELLY_RPC_GET_STATUS \
    "{\"id\":1,\"method\":\"Shelly.GetStatus\"}"

#define SHELLY_RPC_EM_GET_STATUS \
    "{\"id\":1,\"method\":\"EM.GetStatus\",\"params\":{\"id\":0}}"

#ifdef __cplusplus
}
#endif

#endif