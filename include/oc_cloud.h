/****************************************************************************
 *
 * Copyright (c) 2019 Intel Corporation
 * Copyright 2019 Jozef Kralik All Rights Reserved.
 * Copyright 2018 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/**
 * @file
 */
#ifndef OC_CLOUD_H
#define OC_CLOUD_H

#include "oc_client_state.h"
#include "oc_export.h"
#include "oc_link.h"
#include "oc_ri.h"
#include "oc_session_events.h"
#include "util/oc_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  @brief Cloud connection status.
*/
typedef enum {
  OC_CLOUD_INITIALIZED = 0,
  OC_CLOUD_REGISTERED = 1 << 0,
  OC_CLOUD_LOGGED_IN = 1 << 1,
  OC_CLOUD_TOKEN_EXPIRY = 1 << 2,
  OC_CLOUD_REFRESHED_TOKEN = 1 << 3,
  OC_CLOUD_LOGGED_OUT = 1 << 4,
  OC_CLOUD_FAILURE = 1 << 5,
  OC_CLOUD_DEREGISTERED = 1 << 6,
} oc_cloud_status_t;

typedef enum oc_cps_t {
  OC_CPS_UNINITIALIZED = 0,
  OC_CPS_READYTOREGISTER,
  OC_CPS_REGISTERING,
  OC_CPS_REGISTERED,
  OC_CPS_FAILED,
  OC_CPS_DEREGISTERING
} oc_cps_t;

typedef enum {
  CLOUD_OK = 0,
  CLOUD_ERROR_RESPONSE = 1,
  CLOUD_ERROR_CONNECT = 2,
  CLOUD_ERROR_REFRESH_ACCESS_TOKEN = 3,
  CLOUD_ERROR_UNAUTHORIZED = 4,
} oc_cloud_error_t;

typedef struct oc_cloud_context_t oc_cloud_context_t;

/**
  @brief A function pointer for handling the cloud status.
  @param ctx Cloud context (cannot be NULL)
  @param status Current status of the cloud.
  @param user_data User data
*/
typedef void (*oc_cloud_cb_t)(struct oc_cloud_context_t *ctx,
                              oc_cloud_status_t status, void *user_data)
  OC_NONNULL(1);

/**
 * @brief Callback invoked by the cloud manager when cloud change state to
 * logged in or a keepalive response is received.
 *
 * @param response_received Keepalive response received, true if received,
 * otherwise false
 * @param next_ping Delay in milliseconds before next keepalive ping
 * @param next_ping_timeout Timeout in seconds for next keepalive ping
 * @param user_data User data passed from the caller
 *
 * @return true if the cloud manager should continue sending keepalive pings,
 * false if cloud manager should consider the connection lost
 */
typedef bool (*oc_cloud_on_keepalive_response_cb_t)(bool response_received,
                                                    uint64_t *next_ping,
                                                    uint16_t *next_ping_timeout,
                                                    void *user_data)
  OC_NONNULL(2, 3);

/**
 * @brief Enumeration defining cloud actions.
 */
typedef enum {
  OC_CLOUD_ACTION_UNKNOWN = 0,       /**< Unknown cloud action. */
  OC_CLOUD_ACTION_REGISTER = 1,      /**< Cloud registration action. */
  OC_CLOUD_ACTION_LOGIN = 2,         /**< Cloud login action. */
  OC_CLOUD_ACTION_REFRESH_TOKEN = 3, /**< Cloud token refresh action. */
} oc_cloud_action_t;

/**
 * @brief Convert cloud action to a string representation.
 *
 * @param action Cloud action to convert.
 * @return const char* String representation of the cloud action.
 */
const char *oc_cloud_action_to_str(oc_cloud_action_t action) OC_RETURNS_NONNULL;

/**
 * @brief Callback invoked by the cloud manager when the cloud wants to schedule
 * an action.
 *
 * @param action Cloud action to schedule.
 * @param retry_count Retries count - 0 means the first attempt to perform the
 * action.
 * @param delay Delay the action in milliseconds before executing it.
 * @param timeout Timeout in seconds for the action.
 * @param user_data User data passed from the caller.
 *
 * @return true if the cloud manager should continue to schedule the action,
 *         false if the cloud manager should stop for OC_CLOUD_ACTION_REGISTER
 * or restart for other actions.
 */
typedef bool (*oc_cloud_schedule_action_cb_t)(oc_cloud_action_t action,
                                              uint8_t retry_count,
                                              uint64_t *delay,
                                              uint16_t *timeout,
                                              void *user_data) OC_NONNULL(3, 4);

/**
 * @brief Get cloud context for device.
 */
OC_API
oc_cloud_context_t *oc_cloud_get_context(size_t device);

/**
 * @brief Get device index from cloud context.
 *
 * @param ctx cloud context (cannot be NULL)
 * @return size_t device index
 */
size_t oc_cloud_get_device(const oc_cloud_context_t *ctx) OC_NONNULL();

/**
 * @brief Get authorization provider name.
 *
 * The name of the Authorisation Provider through which access token was
 * obtained.
 *
 * @param ctx cloud context (cannot be NULL)
 * @return auth provider ID
 *
 * @see `apn` property in the cloud configuration resource
 */
OC_API
const char *oc_cloud_get_apn(const oc_cloud_context_t *ctx) OC_NONNULL();

/**
 * @brief Get the URL of the OCF Cloud.
 *
 * @param ctx cloud context (cannot be NULL)
 * @return cloud interface server URL
 *
 * @see `cis` property in the cloud configuration resource
 */
OC_API
const char *oc_cloud_get_cis(const oc_cloud_context_t *ctx) OC_NONNULL();

/**
 * @brief Get the access token.
 *
 * Access token is returned by an Authorisation Provider or an OCF Cloud.
 *
 * @param ctx cloud context (cannot be NULL)
 * @return access token
 *
 * @see `at` property in the cloud configuration resource
 */
OC_API
const char *oc_cloud_get_at(const oc_cloud_context_t *ctx) OC_NONNULL();

/**
 * @brief Get the identity of the OCF Cloud.
 *
 * The ID is in the string form of a UUID.
 *
 * @param ctx cloud context (cannot be NULL)
 * @return identity of the OCF Cloud
 *
 * @see `sid` property in the cloud configuration resource
 */
OC_API
const char *oc_cloud_get_sid(const oc_cloud_context_t *ctx) OC_NONNULL();

/**
 * @brief Get the OCF Cloud User identifier
 *
 * The ID is in the string form of a UUID.
 *
 * @param ctx cloud context (cannot be NULL)
 * @return identity of the OCF Cloud
 *
 * @see `uid` property in the cloud configuration resource
 */
OC_API
const char *oc_cloud_get_uid(const oc_cloud_context_t *ctx) OC_NONNULL();

/**
 * @brief Start cloud registration process.
 *
 * @param ctx cloud context (cannot be NULL)
 * @param cb callback function invoked on status change
 * @param data user data provided to the status change function
 * @return int 0 on success
 * @return int -1 on error
 */
OC_API
int oc_cloud_manager_start(oc_cloud_context_t *ctx, oc_cloud_cb_t cb,
                           void *data);
/**
 * @brief Stop cloud registration process, remove related pending delayed
 * callbacks and clean-up data.
 *
 * @param ctx cloud context (cannot be NULL)
 * @return int 0 on success
 * @return int -1 on error
 */
OC_API
int oc_cloud_manager_stop(oc_cloud_context_t *ctx);

/**
 * @brief Restart cloud registration process with the current configuration.
 *
 * @note The cloud manager must be started before calling this function.
 *
 * @param ctx cloud context (cannot be NULL)
 */
OC_API
void oc_cloud_manager_restart(oc_cloud_context_t *ctx) OC_NONNULL();

/**
 * @brief Send request to register device to cloud.
 *
 * @param ctx cloud context
 * @param cb callback function invoked on status change
 * @param data user data provided to the status change function
 * @return int 0 on success
 * @return int -1 on error
 */
OC_API
int oc_cloud_register(oc_cloud_context_t *ctx, oc_cloud_cb_t cb, void *data);

/**
 * @brief Send request to sign in the device to the cloud.
 *
 * @param ctx cloud context
 * @param cb callback function invoked on status change
 * @param data user data provided to the status change function
 * @return int 0 on success
 * @return int -1 on error
 */
OC_API
int oc_cloud_login(oc_cloud_context_t *ctx, oc_cloud_cb_t cb, void *data);

/**
 * @brief Send request to sign out the device to the cloud.
 *
 * @param ctx cloud context
 * @param cb callback function invoked on status change
 * @param data user data provided to the status change function
 * @return int 0 on success
 * @return int -1 on error
 */
OC_API
int oc_cloud_logout(oc_cloud_context_t *ctx, oc_cloud_cb_t cb, void *data);

/**
 * @brief Send request to deregister device from cloud.
 *
 * @note If the device is not signed in then the request requires additional
 * data. If the request becomes larger than is allowed because of this then
 * this call will attempt to sign in to avoid sending this additional data.
 *
 * @param ctx cloud context
 * @param cb callback function invoked on status change
 * @param data user data provided to the status change function
 * @return int 0 on success
 * @return int -1 on error
 *
 * @note oc_cloud_deregister shouldn't be called when oc_cloud_login or
 * oc_cloud_refresh_token have been invoked and haven't yet received a response.
 *
 * @see oc_cloud_login
 * @see oc_cloud_refresh_token
 */
OC_API
int oc_cloud_deregister(oc_cloud_context_t *ctx, oc_cloud_cb_t cb, void *data);

/**
 * @brief Send request to refresh the device access token to the cloud.
 *
 * @param ctx cloud context
 * @param cb callback function invoked on status change
 * @param data user data provided to the status change function
 * @return int 0 on success
 * @return int -1 on error
 */
OC_API
int oc_cloud_refresh_token(oc_cloud_context_t *ctx, oc_cloud_cb_t cb,
                           void *data);

OC_API
int oc_cloud_get_token_expiry(const oc_cloud_context_t *ctx) OC_NONNULL();

/**
 * @brief Set Time to Live value in the provided cloud context.
 *
 * @param ctx Cloud context to update, must not be NULL.
 * @param ttl Time to live value in seconds.
 */
OC_API
void oc_cloud_set_published_resources_ttl(oc_cloud_context_t *ctx, uint32_t ttl)
  OC_NONNULL();

/**
 * @brief Publish resource to cloud.
 *
 * Function checks that resource is contained in list of published or to-be
 * published resources. If it is, the function does nothing. If it is not, then
 * the resource is added to the to-be published resources list and a publish
 * request with this list is sent to the cloud server.
 *
 * @param resource the resource to be published
 */
OC_API
int oc_cloud_add_resource(oc_resource_t *resource);

/**
 * @brief Unpublish resource from cloud.
 *
 * @param resource the resource to be unpublished
 */
OC_API
void oc_cloud_delete_resource(oc_resource_t *resource);

/**
 * @brief Republish previously published devices.
 *
 * @param device the device index
 */
OC_API
int oc_cloud_publish_resources(size_t device);

OC_API
int oc_cloud_discover_resources(const oc_cloud_context_t *ctx,
                                oc_discovery_all_handler_t handler,
                                void *user_data);

/**
 * @brief Configure cloud properties.
 *
 * @deprecated replaced by oc_cloud_provision_conf_resource_v1 in v2.2.5.13
 */
OC_API
int oc_cloud_provision_conf_resource(oc_cloud_context_t *ctx,
                                     const char *server,
                                     const char *access_token,
                                     const char *server_id,
                                     const char *auth_provider) OC_NONNULL(1)
  OC_DEPRECATED("replaced by oc_cloud_provision_conf_resource_v1 in v2.2.5.13");

/**
 * @brief Configure cloud properties.
 *
 * @param ctx Cloud context to update (cannot be be NULL)
 * @param server Cloud server URL (cannot be be NULL)
 * @param server_len Length of the server URL
 * @param access_token Access token from an Authorization Provider (cannot be be
 * NULL)
 * @param access_token_len Length of the access token
 * @param server_id Cloud server ID (cannot be be NULL)
 * @param server_id_len Length of the server ID
 * @param auth_provider Name of the Authorization Provider which provided the
 * access token
 * @param auth_provider_len Length of the Authorization Provider name
 *
 * @return int 0 on success
 * @return int -1 on error
 *
 * @note Cloud manager will be restarted if is was started previously
 */
OC_API
int oc_cloud_provision_conf_resource_v1(
  oc_cloud_context_t *ctx, const char *server, size_t server_len,
  const char *access_token, size_t access_token_len, const char *server_id,
  size_t server_id_len, const char *auth_provider, size_t auth_provider_len)
  OC_NONNULL(1, 2, 4, 6);

/**
 * @brief Set identity certificate chain to establish TLS connection.
 *
 * @param ctx Cloud context to update, must not be NULL.
 * @param selected_identity_cred_id Selected identity certificate chain id.
 * -1(default) means any.
 */
OC_API
void oc_cloud_set_identity_cert_chain(oc_cloud_context_t *ctx,
                                      int selected_identity_cred_id)
  OC_NONNULL();
/**
 * @brief Get selected identity certificate chain to establish TLS connection.
 *
 * @param ctx Cloud context to update, must not be NULL.
 * @return Selected identity certificate chain id. -1 means any.
 */
OC_API
int oc_cloud_get_identity_cert_chain(const oc_cloud_context_t *ctx)
  OC_NONNULL();

/**
 * @brief Set keepalive parameters for the cloud manager.
 *
 * @param ctx Cloud context to update, must not be NULL.
 * @param on_keepalive_response Callback invoked by the cloud manager when cloud
 * change state to logged in or a keepalive response is received.
 * @param user_data User data passed from the caller
 */
OC_API
void oc_cloud_set_keepalive(
  oc_cloud_context_t *ctx,
  oc_cloud_on_keepalive_response_cb_t on_keepalive_response, void *user_data)
  OC_NONNULL(1);

/**
 * @brief Set a custom scheduler for actions in the cloud manager. By default,
 * the cloud manager uses its own scheduler.
 *
 * This function allows you to set a custom scheduler to define delay and
 * timeout for actions.
 *
 * @param ctx Cloud context to update. Must not be NULL.
 * @param on_schedule_action Callback invoked by the cloud manager when the
 * cloud wants to schedule an action.
 * @param user_data User data passed from the caller to be provided during the
 * callback.
 *
 * @note The provided cloud context (`ctx`) must not be NULL.
 * @see oc_cloud_schedule_action_cb_t
 */
OC_API
void oc_cloud_set_schedule_action(
  oc_cloud_context_t *ctx, oc_cloud_schedule_action_cb_t on_schedule_action,
  void *user_data) OC_NONNULL(1);

/**
 * @brief Remove cloud context values, disconnect, and stop the cloud manager,
 * without releasing the context.
 *
 * @param ctx Cloud context to clear, must not be NULL.
 * @param dump_async If true, store the context to storage in an asynchronous
 * manner; otherwise, perform the dump while executing this function.
 */
OC_API
void oc_cloud_context_clear(oc_cloud_context_t *ctx, bool dump_async)
  OC_NONNULL();

#ifdef __cplusplus
}
#endif

#endif /* OC_CLOUD_H */
