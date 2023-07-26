/****************************************************************************
 *
 * Copyright (c) 2023 plgd.dev s.r.o.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************/

#ifndef OC_QUERY_INTERNAL_H
#define OC_QUERY_INTERNAL_H

#include "api/oc_helpers_internal.h"
#include "oc_ri.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef OC_SERVER

/** @brief Encode interface to string in format "if=<interface>" */
oc_string_view_t oc_query_encode_interface(oc_interface_mask_t iface_mask);

#endif /* OC_SERVER */

#ifdef __cplusplus
}
#endif

#endif /* OC_QUERY_INTERNAL_H */
