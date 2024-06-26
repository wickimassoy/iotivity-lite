/****************************************************************************
 *
 * Copyright (c) 2019 Intel Corporation
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

#ifndef OC_MNT_INTERNAL_H
#define OC_MNT_INTERNAL_H

#include "api/oc_helpers_internal.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OCF_MNT_URI "/oic/mnt"
#define OCF_MNT_RT "oic.wk.mnt"

/**
 * @brief Creation of the oic.wk.mnt resource.
 *
 * @param device index of the device to which the resource is to be created
 */
void oc_create_maintenance_resource(size_t device);

/** @brief Check if the URI matches the maintenance resource URI (with or
 * without the leading slash)
 */
bool oc_is_maintenance_resource_uri(oc_string_view_t uri);

#ifdef __cplusplus
}
#endif

#endif /* OC_MNT_INTERNAL_H */
