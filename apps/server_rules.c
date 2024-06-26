/*
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Copyright 2017-2019 Open Connectivity Foundation
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Licensed under the Apache License, Version 2.0 (the "License"),
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
*/

#include "oc_api.h"
#include "oc_collection.h"
#include "oc_core_res.h"
#include "oc_link.h"
#include "oc_log.h"
#include "oc_ri.h"
#include "port/oc_assert.h"
#include "port/oc_clock.h"
#include "util/oc_atomic.h"

#if defined(OC_INTROSPECTION) && defined(OC_IDD_API)
#include "oc_introspection.h"
#endif /* OC_INTROSPECTION && OC_IDD_API */

#include <signal.h>
#include <stdlib.h>
/* linux specific code */
#include <pthread.h>
static pthread_mutex_t mutex;
static pthread_cond_t cv;

#define MAX_STRING 65 /* max size of the strings. */

typedef struct scenemappings_t
{
  struct scenemappings_t *next;
  char scene[MAX_STRING];
  char key[MAX_STRING];
  char value[MAX_STRING];
} scenemappings_t;
OC_MEMB(smap_s, scenemappings_t, 1);
OC_LIST(smap);

static OC_ATOMIC_INT8_T quit = 0; /* stop variable, used by handle_signal */

/* global property variables for path: "/binaryswitch" */
static bool g_binaryswitch_value = false;

/* global property variables for path: "/audio" */
static bool g_audio_mute = false;
static int g_audio_volume = 50;

/* global property variables for path: /ruleaction and /scenecollection */
static char lastscene[MAX_STRING];
static char ra_lastscene[MAX_STRING];
static oc_string_array_t scenevalues;

/* global property variables for path: /ruleexpression */
static char rule[MAX_STRING];
static bool ruleresult = false;
static bool ruleenable = false;
static bool actionenable = false;

/* Resource handles */
/* Used as input to rule */
static oc_resource_t *res_binaryswitch;
/* Used in the rule action */
static oc_resource_t *res_audio;

#ifdef OC_COLLECTIONS
/* Collection of Scene Members. Records the "lastscene" following a rule action
 */
static oc_resource_t *res_scenecol1;
/* Specification of the rule */
static oc_resource_t *res_ruleexpression;
#endif /* OC_COLLECTIONS */

static pthread_t toggle_switch_thread;

static oc_event_callback_retval_t
set_scene(void *data)
{
  (void)data;
  scenemappings_t *sm = (scenemappings_t *)oc_list_head(smap);
  while (sm) {
    if (strcmp(sm->scene, lastscene) == 0) {
      if (strcmp(sm->key, "volume") == 0) {
        sscanf(sm->value, "%d", &g_audio_volume);
        oc_notify_observers(res_audio);
        break;
      }
    }
    sm = sm->next;
  }
  return OC_EVENT_DONE;
}

static void
perform_rule_action(void)
{
  /*
   * Set lastscene on the target scenecollection
   */
  if (actionenable) {
    strcpy(lastscene, ra_lastscene);
    set_scene(NULL);
  }
}

static void
rule_notify_and_eval(void)
{
  /*
   * rule expression value has changed
   */
  if (ruleenable) {
    /*
     * rule is enabled
     */
    if (g_binaryswitch_value) {
      ruleresult = true;
    } else {
      ruleresult = false;
    }

#ifdef OC_COLLECTIONS
    oc_notify_observers(res_ruleexpression);
#endif /* OC_COLLECTIONS */

    if (actionenable && ruleresult) {
      perform_rule_action();
    }
  } else {
    ruleresult = false;
  }
}

oc_define_interrupt_handler(toggle_switch)
{
  if (res_binaryswitch) {
    oc_notify_observers(res_binaryswitch);
    rule_notify_and_eval();
  }
}

#ifdef OC_IDD_API

#define INTROSPECTION_IDD_FILE "server_rules_IDD.cbor"

static bool
set_introspection_data(size_t device)
{
  FILE *fp = fopen("./" INTROSPECTION_IDD_FILE, "rb");
  if (fp == NULL) {
    return false;
  }
  uint8_t *buffer = NULL;
  if (fseek(fp, 0, SEEK_END) < 0) {
    goto error;
  }
  long ret = ftell(fp);
  if (ret < 0) {
    goto error;
  }
  if (fseek(fp, 0, SEEK_SET) < 0) {
    goto error;
  }

  size_t buffer_size = (size_t)ret;
  buffer = (uint8_t *)malloc(buffer_size * sizeof(uint8_t));
  if (buffer == NULL) {
    goto error;
  }
  size_t fread_ret = fread(buffer, buffer_size, 1, fp);
  fclose(fp);

  if (fread_ret != 1) {
    free(buffer);
    return false;
  }

  if (oc_set_introspection_data_v1(device, buffer, buffer_size) < 0) {
    free(buffer);
    return false;
  }
  OC_PRINTF("\tIntrospection data set '%s.cbor': %d [bytes]\n",
            INTROSPECTION_IDD_FILE, (int)buffer_size);
  free(buffer);
  return true;

error:
  free(buffer);
  fclose(fp);
  return false;
}
#endif /* OC_IDD_API */

/**
 * function to set up the device.
 *
 */
static int
app_init(void)
{
  oc_activate_interrupt_handler(toggle_switch);
  int ret = oc_init_platform("ocf", NULL, NULL);
  /* the settings determine the appearance of the device on the network
     can be OCF1.3.1 or OCF2.0.0 (or even higher)
     supplied values are for OCF1.3.1 */
  ret |= oc_add_device("/oic/d", "oic.d.stb", "Set Top Box",
                       "ocf.2.2.5",                   /* icv value */
                       "ocf.res.1.3.0, ocf.sh.1.3.0", /* dmv value */
                       NULL, NULL);
  if (ret < 0) {
    return ret;
  }
  strcpy(rule, "(switch:value = true)");
  strcpy(lastscene, "normalaudio");
  strcpy(ra_lastscene, "loudaudio");
  oc_new_string_array(&scenevalues, (size_t)2);
  oc_string_array_add_item(scenevalues, lastscene);
  oc_string_array_add_item(scenevalues, ra_lastscene);
  scenemappings_t *sm = (scenemappings_t *)oc_memb_alloc(&smap_s);
  if (sm) {
    strcpy(sm->scene, "normalaudio");
    strcpy(sm->key, "volume");
    sprintf(sm->value, "%f", 40.0);
    oc_list_add(smap, sm);
  }
  sm = (scenemappings_t *)oc_memb_alloc(&smap_s);
  if (sm) {
    strcpy(sm->scene, "loudaudio");
    strcpy(sm->key, "volume");
    sprintf(sm->value, "%d", 60);
    oc_list_add(smap, sm);
  }
#ifdef OC_IDD_API
  if (!set_introspection_data(/*device*/ 0)) {
    OC_PRINTF("%s", "\tERROR Could not read '" INTROSPECTION_IDD_FILE "'\n"
                    "\tIntrospection data not set for device.\n");
  }
#endif /* OC_IDD_API */
  return 0;
}

static void *
toggle_switch_resource(void *data)
{
  (void)data;
  while (OC_ATOMIC_LOAD8(quit) != 1) {
    int c = getchar();
    if (c == EOF) {
      break;
    }

    if (OC_ATOMIC_LOAD8(quit) != 1) {
      (void)getchar(); // consume newline
      if (c == 48) {
        g_binaryswitch_value = false;
      } else {
        g_binaryswitch_value = true;
      }
      oc_signal_interrupt_handler(toggle_switch);
    }
  }
  return NULL;
}

/**
 * get method for "/binaryswitch" resource.
 * function is called to intialize the return values of the GET method.
 * initialisation of the returned values are done from the global property
 * values. Resource Description: This Resource describes a binary switch
 * (on/off). The Property "value" is a boolean. A value of 'true' means that the
 * switch is on. A value of 'false' means that the switch is off.
 *
 * @param request the request representation.
 * @param interfaces the interface used for this call
 * @param user_data the user data.
 */
static void
get_binaryswitch(oc_request_t *request, oc_interface_mask_t interfaces,
                 void *user_data)
{
  (void)user_data; /* not used */
  /* TODO: SENSOR add here the code to talk to the HW if one implements a
     sensor. the call to the HW needs to fill in the global variable before it
     returns to this function here. alternative is to have a callback from the
     hardware that sets the global variables. */

  OC_PRINTF("get_binaryswitch: interface %d\n", interfaces);
  oc_rep_start_root_object();
  switch (interfaces) {
  case OC_IF_BASELINE:
    OC_PRINTF("   Adding Baseline info\n");
    oc_process_baseline_interface(request->resource);
    /* fall through */
  case OC_IF_A:
    /* property "value" */
    oc_rep_set_boolean(root, value, g_binaryswitch_value);
    OC_PRINTF("   value : %d\n", g_binaryswitch_value); /* not handled value */
    break;
  default:
    break;
  }
  oc_rep_end_root_object();
  oc_send_response(request, OC_STATUS_OK);
}

/**
* post method for "/binaryswitch" resource.
* The function has as input the request body, which are the input values of the
POST method.
* The input values (as a set) are checked if all supplied values are correct.
* If the input values are correct, they will be assigned to the global  property
values.
*/
static void
post_binaryswitch(oc_request_t *request, oc_interface_mask_t interfaces,
                  void *user_data)
{
  (void)interfaces;
  (void)user_data;
  bool error_state = false;
  OC_PRINTF("post_binaryswitch:\n");
  oc_rep_t *rep = request->request_payload;
  /* loop over the request document to check if all inputs are ok */
  while (rep != NULL) {
    OC_PRINTF("key: (check) %s \n", oc_string(rep->name));
    if (memcmp(oc_string(rep->name), "value", 5) == 0) {
      /* property "value" of type boolean exist in payload */
      if (rep->type != OC_REP_BOOL) {
        error_state = true;
        OC_PRINTF("   property 'value' is not of type bool %d \n", rep->type);
      }
    }

    rep = rep->next;
  }
  /* if the input is ok, then process the input document and assign the global
   * variables */
  if (error_state == false) {
    /* loop over all the properties in the input document */
    oc_rep_t *rep = request->request_payload;
    while (rep != NULL) {
      OC_PRINTF("key: (assign) %s \n", oc_string(rep->name));
      /* no error: assign the variables */
      if (memcmp(oc_string(rep->name), "value", 5) == 0) {
        /* assign "value" */
        g_binaryswitch_value = rep->value.boolean;
      }
      rep = rep->next;
    }
    /* set the response */
    OC_PRINTF("Set response \n");
    oc_rep_start_root_object();
    oc_rep_set_boolean(root, value, g_binaryswitch_value);
    oc_rep_end_root_object();

    oc_send_response(request, OC_STATUS_CHANGED);
    rule_notify_and_eval();
  } else {
    /* TODO: add error response, if any */
    oc_send_response(request, OC_STATUS_NOT_MODIFIED);
  }
}

/**
 * get method for "/audio" resource.
 * function is called to intialize the return values of the GET method.
 * initialisation of the returned values are done from the global property
 * values. Resource Description:
 *
 * @param request the request representation.
 * @param interfaces the interface used for this call
 * @param user_data the user data.
 */
static void
get_audio(oc_request_t *request, oc_interface_mask_t interfaces,
          void *user_data)
{
  (void)user_data; /* not used */

  OC_PRINTF("get_audio: interface %d\n", interfaces);
  oc_rep_start_root_object();
  switch (interfaces) {
  case OC_IF_BASELINE:
    OC_PRINTF("   Adding Baseline info\n");
    oc_process_baseline_interface(request->resource);
    /* fall through */
  case OC_IF_A:
    oc_rep_set_int(root, volume, g_audio_volume);
    oc_rep_set_boolean(root, mute, g_audio_mute);
    break;
  default:
    break;
  }
  oc_rep_end_root_object();
  oc_send_response(request, OC_STATUS_OK);
}

/**
* post method for "/audio" resource.
* The function has as input the request body, which are the input values of the
POST method.
* The input values (as a set) are checked if all supplied values are correct.
* If the input values are correct, they will be assigned to the global  property
values.
*/
static void
post_audio(oc_request_t *request, oc_interface_mask_t interfaces,
           void *user_data)
{
  (void)interfaces;
  (void)user_data;
  OC_PRINTF("post_audio:\n");
  oc_rep_t *rep = request->request_payload;

  while (rep != NULL) {
    switch (rep->type) {
    case OC_REP_BOOL:
      g_audio_mute = rep->value.boolean;
      OC_PRINTF("value: %d\n", g_audio_mute);
      break;
    case OC_REP_INT:
      g_audio_volume = (int)rep->value.integer;
      OC_PRINTF("value: %d\n", g_audio_volume);
      break;
    default:
      oc_send_response(request, OC_STATUS_BAD_REQUEST);
      return;
      break;
    }
    rep = rep->next;
  }

  oc_send_response(request, OC_STATUS_CHANGED);
}

#ifdef OC_COLLECTIONS
static void
encode_interfaces_mask(CborEncoder *parent, unsigned iface_mask)
{
  oc_rep_set_key(parent, "if");
  oc_rep_start_array((parent), if);
  if ((iface_mask & OC_IF_R) != 0) {
    oc_rep_add_text_string(if, "oic.if.r");
  }
  if ((iface_mask & OC_IF_RW) != 0) {
    oc_rep_add_text_string(if, "oic.if.rw");
  }
  if ((iface_mask & OC_IF_A) != 0) {
    oc_rep_add_text_string(if, "oic.if.a");
  }
  if ((iface_mask & OC_IF_S) != 0) {
    oc_rep_add_text_string(if, "oic.if.s");
  }
  if ((iface_mask & OC_IF_LL) != 0) {
    oc_rep_add_text_string(if, "oic.if.ll");
  }
  if ((iface_mask & OC_IF_CREATE) != 0) {
    oc_rep_add_text_string(if, "oic.if.create");
  }
  if ((iface_mask & OC_IF_B) != 0) {
    oc_rep_add_text_string(if, "oic.if.b");
  }
  if ((iface_mask & OC_IF_BASELINE) != 0) {
    oc_rep_add_text_string(if, "oic.if.baseline");
  }
  if ((iface_mask & OC_IF_W) != 0) {
    oc_rep_add_text_string(if, "oic.if.w");
  }
  if ((iface_mask & OC_IF_STARTUP) != 0) {
    oc_rep_add_text_string(if, "oic.if.startup");
  }
  if ((iface_mask & OC_IF_STARTUP_REVERT) != 0) {
    oc_rep_add_text_string(if, "oic.if.startup.revert");
  }
  oc_rep_end_array(parent, if);
}

/**
 * get method for "/scenemember1" resource.
 * function is called to intialize the return values of the GET method.
 * initialisation of the returned values are done from the global property
 * values. Resource Description:
 *
 * @param request the request representation.
 * @param interfaces the interface used for this call
 * @param user_data the user data.
 */
static void
get_scenemember(oc_request_t *request, oc_interface_mask_t interfaces,
                void *user_data)
{
  (void)user_data; /* not used */

  OC_PRINTF("get_scenemember: interface %d\n", interfaces);
  oc_rep_start_root_object();
  switch (interfaces) {
  case OC_IF_BASELINE:
    oc_process_baseline_interface(request->resource);

    // "link" Property
    oc_rep_set_object(root, link);
    oc_rep_set_text_string(link, href, oc_string(res_audio->uri));
    oc_rep_set_string_array(link, rt, res_audio->types);
    encode_interfaces_mask(oc_rep_object(link), res_audio->interfaces);
    oc_rep_close_object(root, link);

    // SceneMappings array
    oc_rep_set_array(root, SceneMappings);
    scenemappings_t *sm = (scenemappings_t *)oc_list_head(smap);
    while (sm) {
      oc_rep_object_array_begin_item(SceneMappings);
      oc_rep_set_text_string(SceneMappings, scene, sm->scene);
      oc_rep_set_text_string(SceneMappings, memberProperty, sm->key);
      oc_rep_set_text_string(SceneMappings, memberValue, sm->value);
      oc_rep_object_array_end_item(SceneMappings);
      sm = sm->next;
    }
    oc_rep_close_array(root, SceneMappings);

    break;
  default:
    break;
  }
  oc_rep_end_root_object();
  oc_send_response(request, OC_STATUS_OK);
}

/**
 * get method for "/ruleexpression" resource.
 * function is called to intialize the return values of the GET method.
 * initialisation of the returned values are done from the global property
 * values. Resource Description:
 *
 * @param request the request representation.
 * @param interfaces the interface used for this call
 * @param user_data the user data.
 */
static void
get_ruleexpression(oc_request_t *request, oc_interface_mask_t interfaces,
                   void *user_data)
{
  (void)user_data; /* not used */

  OC_PRINTF("get_ruleexpression: interface %d\n", interfaces);
  oc_rep_start_root_object();
  switch (interfaces) {
  case OC_IF_BASELINE:
    OC_PRINTF("   Adding Baseline info\n");
    oc_process_baseline_interface(request->resource);
    /* fall through */
  case OC_IF_RW:
    oc_rep_set_boolean(root, ruleresult, ruleresult);
    oc_rep_set_boolean(root, ruleenable, ruleenable);
    oc_rep_set_boolean(root, actionenable, actionenable);
    oc_rep_set_text_string(root, rule, rule);
    break;
  default:
    break;
  }
  oc_rep_end_root_object();
  oc_send_response(request, OC_STATUS_OK);
}

/**
* post method for "/ruleexpression" resource.
* The function has as input the request body, which are the input values of the
POST method.
* The input values (as a set) are checked if all supplied values are correct.
* If the input values are correct, they will be assigned to the global  property
values.
*/
static void
post_ruleexpression(oc_request_t *request, oc_interface_mask_t interfaces,
                    void *user_data)
{
  (void)interfaces;
  (void)user_data;

  OC_PRINTF("post_ruleexpression:\n");
  oc_rep_t *rep = request->request_payload;
  while (rep != NULL) {
    OC_PRINTF("  %s :", oc_string(rep->name));
    switch (rep->type) {
    case OC_REP_BOOL:
      if (oc_string_len(rep->name) == 10 &&
          memcmp(oc_string(rep->name), "ruleenable", 10) == 0) {
        ruleenable = rep->value.boolean;
        /* If the rule has been newly enabled evaluate the rule expression */
        if (ruleenable) {
          rule_notify_and_eval();
        }
      } else if (oc_string_len(rep->name) == 12 &&
                 memcmp(oc_string(rep->name), "actionenable", 12) == 0) {
        actionenable = rep->value.boolean;
      } else if (oc_string_len(rep->name) == 10 &&
                 memcmp(oc_string(rep->name), "ruleresult", 10) == 0) {
        /* Attempt to set the result, verify rule is disabled and actions are
         * enabled */
        if (!ruleenable && actionenable) {
          ruleresult = rep->value.boolean;
          if (ruleresult) {
            perform_rule_action();
          }
        } else {
          // Invalid state for setting ruleresult by a client; fail the request
          oc_send_response(request, OC_STATUS_METHOD_NOT_ALLOWED);
          return;
          break;
        }
      }
      break;
    default:
      oc_send_response(request, OC_STATUS_BAD_REQUEST);
      return;
      break;
    }
    rep = rep->next;
  }

  oc_send_response(request, OC_STATUS_CHANGED);
}

/**
 * get method for "/ruleaction" resource.
 * function is called to intialize the return values of the GET method.
 * initialisation of the returned values are done from the global property
 * values. Resource Description:
 *
 * @param request the request representation.
 * @param interfaces the interface used for this call
 * @param user_data the user data.
 */
static void
get_ruleaction(oc_request_t *request, oc_interface_mask_t interfaces,
               void *user_data)
{
  (void)user_data; /* not used */
  OC_PRINTF("get_ruleaction: interface %d\n", interfaces);
  oc_rep_start_root_object();
  switch (interfaces) {
  case OC_IF_BASELINE:
    oc_process_baseline_interface(request->resource);
  /* fall through */
  case OC_IF_RW:
    oc_rep_set_text_string(root, scenevalue, ra_lastscene);
    oc_rep_set_object(root, link);
    oc_rep_set_text_string(link, href, "/scenecollection1");
    oc_rep_set_string_array(link, rt, res_scenecol1->types);
    encode_interfaces_mask(oc_rep_object(link), res_scenecol1->interfaces);
    oc_rep_close_object(root, link);
    break;
  default:
    break;
  }
  oc_rep_end_root_object();
  oc_send_response(request, OC_STATUS_OK);
}

/**
* post method for "/ruleaction" resource.
* The function has as input the request body, which are the input values of the
POST method.
* The input values (as a set) are checked if all supplied values are correct.
* If the input values are correct, they will be assigned to the global  property
values.
*/
static void
post_ruleaction(oc_request_t *request, oc_interface_mask_t interfaces,
                void *user_data)
{
  (void)interfaces;
  (void)user_data;
  OC_PRINTF("post_ruleaction:\n");
  oc_rep_t *rep = request->request_payload;
  while (rep) {
    if (rep->type == OC_REP_STRING && oc_string_len(rep->name) == 10 &&
        memcmp(oc_string(rep->name), "scenevalue", 10) == 0) {
      size_t i;
      bool match = false;
      for (i = 0; i < oc_string_array_get_allocated_size(scenevalues); i++) {
        const char *sv = oc_string_array_get_item(scenevalues, i);
        if (strlen(sv) == oc_string_len(rep->value.string) &&
            memcmp(sv, oc_string(rep->value.string),
                   oc_string_len(rep->value.string)) == 0) {
          match = true;
          break;
        }
      }
      if (!match) {
        oc_send_response(request, OC_STATUS_BAD_REQUEST);
      }
      strncpy(ra_lastscene, oc_string(rep->value.string), sizeof(ra_lastscene));
      ra_lastscene[sizeof(ra_lastscene) - 1] = '\0';
    } else {
      oc_send_response(request, OC_STATUS_BAD_REQUEST);
    }
    rep = rep->next;
  }

  oc_send_response(request, OC_STATUS_CHANGED);
}

/**
 * Callbacks for handling Collection level Properties on Scene Collection
 */
static bool
set_scenecol_properties(const oc_resource_t *resource, const oc_rep_t *rep,
                        void *data)
{
  (void)resource;
  (void)data;
  while (rep != NULL) {
    switch (rep->type) {
    case OC_REP_STRING:
      if (oc_string_len(rep->name) == 9 &&
          memcmp(oc_string(rep->name), "lastScene", 9) == 0) {
        size_t i;
        bool match = false;
        for (i = 0; i < oc_string_array_get_allocated_size(scenevalues); i++) {
          const char *sv = oc_string_array_get_item(scenevalues, i);
          if (strlen(sv) == oc_string_len(rep->value.string) &&
              memcmp(sv, oc_string(rep->value.string),
                     oc_string_len(rep->value.string)) == 0) {
            match = true;
            break;
          }
        }
        if (!match) {
          return false;
        }
        strncpy(lastscene, oc_string(rep->value.string), sizeof(lastscene));
        lastscene[sizeof(lastscene) - 1] = '\0';
        oc_set_delayed_callback(NULL, &set_scene, 0);
      }
      break;
    default:
      break;
    }
    rep = rep->next;
  }
  return true;
}

static void
get_scenecol_properties(const oc_resource_t *resource,
                        oc_interface_mask_t iface_mask, void *data)
{
  (void)resource;
  (void)data;
  switch (iface_mask) {
  case OC_IF_BASELINE:
    oc_rep_set_text_string(root, lastScene, lastscene);
    oc_rep_set_string_array(root, sceneValues, scenevalues);
    break;
  default:
    break;
  }
}

#endif /* OC_COLLECTIONS */

static bool
register_switch(void)
{
  OC_PRINTF("Register Resource with local path \"/binaryswitch\"\n");
  res_binaryswitch = oc_new_resource("Binary Switch", "/binaryswitch", 1, 0);
  if (res_binaryswitch == NULL) {
    OC_PRINTF("ERROR: could not allocate /binaryswitch\n");
    return false;
  }
  oc_resource_bind_resource_type(res_binaryswitch, "oic.r.switch.binary");
  oc_resource_bind_resource_interface(res_binaryswitch, OC_IF_A);
  oc_resource_set_default_interface(res_binaryswitch, OC_IF_A);
  oc_resource_set_discoverable(res_binaryswitch, true);
  oc_resource_set_periodic_observable(res_binaryswitch, 1);
  oc_resource_set_request_handler(res_binaryswitch, OC_GET, get_binaryswitch,
                                  NULL);
  oc_resource_set_request_handler(res_binaryswitch, OC_POST, post_binaryswitch,
                                  NULL);
  if (!oc_add_resource(res_binaryswitch)) {
    OC_PRINTF("ERROR: could not register /binaryswitch\n");
    return false;
  }
  return true;
}

static bool
register_audio(void)
{
  OC_PRINTF("Register Resource with local path \"/audio\"\n");
  res_audio = oc_new_resource("Audio", "/audio", 1, 0);
  if (res_audio == NULL) {
    OC_PRINTF("ERROR: could not allocate /audio\n");
    return false;
  }
  oc_resource_bind_resource_type(res_audio, "oic.r.audio");
  oc_resource_bind_resource_interface(res_audio, OC_IF_A);
  oc_resource_set_default_interface(res_audio, OC_IF_A);
  oc_resource_set_discoverable(res_audio, true);
  oc_resource_set_periodic_observable(res_audio, 1);
  oc_resource_set_request_handler(res_audio, OC_GET, get_audio, NULL);
  oc_resource_set_request_handler(res_audio, OC_POST, post_audio, NULL);
  if (!oc_add_resource(res_audio)) {
    OC_PRINTF("ERROR: could not register /audio\n");
    return false;
  }
  return true;
}

#ifdef OC_COLLECTIONS
static oc_resource_t *
register_scenemember(void)
{
  OC_PRINTF("Register Resource with local path \"/scenemember1\"\n");
  oc_resource_t *res_scenemember1 =
    oc_new_resource("Scene Member 1", "/scenemember1", 1, 0);
  if (res_scenemember1 == NULL) {
    OC_PRINTF("ERROR: could not allocate /scenemember1\n");
    return NULL;
  }
  oc_resource_bind_resource_type(res_scenemember1, "oic.wk.scenemember");
  oc_resource_set_discoverable(res_scenemember1, true);
  oc_resource_set_periodic_observable(res_scenemember1, 1);
  oc_resource_set_request_handler(res_scenemember1, OC_GET, get_scenemember,
                                  NULL);
  if (!oc_add_resource(res_scenemember1)) {
    OC_PRINTF("ERROR: could not register /scenemember1\n");
    return NULL;
  }
  return res_scenemember1;
}

static bool
register_scenecollection(oc_resource_t *res_scenemember1)
{
  OC_PRINTF("Register Collection with local path \"/scenecollection1\"\n");
  res_scenecol1 =
    oc_new_collection("Scene Collection 1", "/scenecollection1", 1, 0);
  if (res_scenecol1 == NULL) {
    OC_PRINTF("ERROR: could not allocate /scenecollection1\n");
    return false;
  }

  // Remove batch from the set of supported interafaces
  res_scenecol1->interfaces = OC_IF_BASELINE | OC_IF_LL;
  oc_resource_bind_resource_type(res_scenecol1, "oic.wk.scenecollection");
  oc_resource_set_discoverable(res_scenecol1, true);

  if (!oc_collection_add_mandatory_rt(res_scenecol1, "oic.wk.scenemember")) {
    OC_PRINTF("ERROR: could not add mandatory rt oic.wk.scenemember\n");
    return false;
  }
  if (!oc_collection_add_supported_rt(res_scenecol1, "oic.wk.scenemember")) {
    OC_PRINTF("ERROR: could not add supported rt oic.wk.scenemember\n");
    return false;
  }
  oc_resource_set_properties_cbs(res_scenecol1, get_scenecol_properties, NULL,
                                 set_scenecol_properties, NULL);
  if (!oc_add_collection_v1(res_scenecol1)) {
    OC_PRINTF("ERROR: could not register /scenecollection1\n");
    return false;
  }

  oc_link_t *sm1 = oc_new_link(res_scenemember1);
  if (sm1 == NULL) {
    OC_PRINTF("ERROR: could not allocate /scenemember1 link\n");
    return false;
  }
  oc_collection_add_link(res_scenecol1, sm1);

  return true;
}

static bool
register_scenelist(void)
{
  OC_PRINTF("Register Collection with local path \"/scenelist\"\n");
  oc_resource_t *res_scenelist =
    oc_new_collection("Scene List", "/scenelist", 1, 0);
  if (res_scenelist == NULL) {
    OC_PRINTF("ERROR: could not allocate /scenelist\n");
    return false;
  }
  oc_resource_bind_resource_type(res_scenelist, "oic.wk.scenelist");
  oc_resource_set_discoverable(res_scenelist, true);
  // Remove batch from the set of supported interafaces
  res_scenelist->interfaces = OC_IF_BASELINE | OC_IF_LL;

  if (!oc_collection_add_mandatory_rt(res_scenelist,
                                      "oic.wk.scenecollection")) {
    OC_PRINTF("ERROR: could not add mandatory rt oic.wk.scenecollection\n");
    return false;
  }
  if (!oc_collection_add_supported_rt(res_scenelist,
                                      "oic.wk.scenecollection")) {
    OC_PRINTF("ERROR: could not add supported rt oic.wk.scenecollection\n");
    return false;
  }

  if (!oc_add_collection_v1(res_scenelist)) {
    OC_PRINTF("ERROR: could not register /scenelist\n");
    return false;
  }

  oc_link_t *sc1 = oc_new_link(res_scenecol1);
  if (sc1 == NULL) {
    OC_PRINTF("ERROR: could not allocate /scenecollection1 link\n");
    return false;
  }
  oc_collection_add_link(res_scenelist, sc1);
  return true;
}

static bool
register_ruleexpression(void)
{
  OC_PRINTF("Register Resource with local path \"/ruleexpression\"\n");
  res_ruleexpression =
    oc_new_resource("Rule Expression", "/ruleexpression", 1, 0);
  if (res_ruleexpression == NULL) {
    OC_PRINTF("ERROR: could not allocate /ruleexpression\n");
    return false;
  }
  oc_resource_bind_resource_type(res_ruleexpression, "oic.r.rule.expression");
  oc_resource_bind_resource_interface(res_ruleexpression, OC_IF_RW);
  oc_resource_set_default_interface(res_ruleexpression, OC_IF_RW);
  oc_resource_set_discoverable(res_ruleexpression, true);
  oc_resource_set_periodic_observable(res_ruleexpression, 1);
  oc_resource_set_request_handler(res_ruleexpression, OC_GET,
                                  get_ruleexpression, NULL);
  oc_resource_set_request_handler(res_ruleexpression, OC_POST,
                                  post_ruleexpression, NULL);
  if (!oc_add_resource(res_ruleexpression)) {
    OC_PRINTF("ERROR: could not register /ruleexpression\n");
    return false;
  }
  return true;
}

static oc_resource_t *
register_ruleaction(void)
{
  OC_PRINTF("Register Resource with local path \"/ruleaction\"\n");
  oc_resource_t *res_ruleaction =
    oc_new_resource("Rule Action", "/ruleaction", 1, 0);
  if (res_ruleaction == NULL) {
    OC_PRINTF("ERROR: could not allocate /ruleaction\n");
    return NULL;
  }
  oc_resource_bind_resource_type(res_ruleaction, "oic.r.rule.action");
  oc_resource_bind_resource_interface(res_ruleaction, OC_IF_RW);
  oc_resource_set_default_interface(res_ruleaction, OC_IF_RW);
  oc_resource_set_discoverable(res_ruleaction, true);
  oc_resource_set_periodic_observable(res_ruleaction, 1);
  oc_resource_set_request_handler(res_ruleaction, OC_GET, get_ruleaction, NULL);
  oc_resource_set_request_handler(res_ruleaction, OC_POST, post_ruleaction,
                                  NULL);
  if (!oc_add_resource(res_ruleaction)) {
    OC_PRINTF("ERROR: could not register /ruleaction\n");
    return NULL;
  }
  return res_ruleaction;
}

static oc_resource_t *
register_ruleinputcollection(void)
{
  OC_PRINTF("Register Collection with local path \"/ruleinputcollection\"\n");
  oc_resource_t *res_ruleinputcol =
    oc_new_collection("Rule Input Collection", "/ruleinputcollection", 1, 0);
  if (res_ruleinputcol == NULL) {
    OC_PRINTF("ERROR: could not allocate /ruleinputcollection\n");
    return NULL;
  }
  // Remove batch from the set of supported interafaces
  res_ruleinputcol->interfaces = OC_IF_BASELINE | OC_IF_LL;
  oc_resource_bind_resource_type(res_ruleinputcol,
                                 "oic.r.rule.inputcollection");
  oc_resource_set_discoverable(res_ruleinputcol, true);

  // oc_collection_add_mandatory_rt(res_ruleinputcol, "oic.r.switch.binary");
  if (!oc_collection_add_supported_rt(res_ruleinputcol,
                                      "oic.r.switch.binary")) {
    OC_PRINTF("ERROR: could not add supported rt oic.r.switch.binary\n");
    return NULL;
  }
  if (!oc_add_collection_v1(res_ruleinputcol)) {
    OC_PRINTF("ERROR: could not register /ruleinputcollection\n");
    return NULL;
  }

  oc_link_t *ric1 = oc_new_link(res_binaryswitch);
  if (ric1 == NULL) {
    OC_PRINTF("ERROR: could not allocate /binaryswitch link\n");
    return NULL;
  }
  // Replace the default rel array with ["hosts"] with just "ruleinput"
  oc_link_clear_rels(ric1);
  oc_link_add_rel(ric1, "ruleinput");
  if (!oc_link_add_link_param(ric1, "anchor", "switch")) {
    OC_PRINTF("ERROR: could not add anchor link param to /binaryswitch\n");
    return NULL;
  }
  oc_link_set_interfaces(ric1, OC_IF_A);
  oc_collection_add_link(res_ruleinputcol, ric1);
  return res_ruleinputcol;
}

static oc_resource_t *
register_ruleactioncol(oc_resource_t *res_ruleaction)
{
  OC_PRINTF("Register Collection with local path \"/ruleactioncollection\"\n");
  oc_resource_t *res_ruleactioncol =
    oc_new_collection("Rule Action Collection", "/ruleactioncollection", 1, 0);
  if (res_ruleactioncol == NULL) {
    OC_PRINTF("ERROR: could not allocate /ruleactioncollection\n");
    return NULL;
  }
  // Remove batch from the set of supported interafaces
  res_ruleactioncol->interfaces = OC_IF_BASELINE | OC_IF_LL;
  oc_resource_bind_resource_type(res_ruleactioncol,
                                 "oic.r.rule.actioncollection");
  oc_resource_set_discoverable(res_ruleactioncol, true);

  if (!oc_collection_add_mandatory_rt(res_ruleactioncol, "oic.r.rule.action")) {
    OC_PRINTF("ERROR: could not add mandatory rt oic.r.rule.action\n");
    return NULL;
  }
  if (!oc_collection_add_supported_rt(res_ruleactioncol, "oic.r.rule.action")) {
    OC_PRINTF("ERROR: could not add supported rt oic.r.rule.action\n");
    return NULL;
  }

  if (!oc_add_collection_v1(res_ruleactioncol)) {
    OC_PRINTF("ERROR: could not register /ruleactioncollection\n");
    return NULL;
  }

  oc_link_t *rac1 = oc_new_link(res_ruleaction);
  if (rac1 == NULL) {
    OC_PRINTF("ERROR: could not allocate /ruleaction link\n");
    return NULL;
  }
  oc_collection_add_link(res_ruleactioncol, rac1);
  return res_ruleactioncol;
}

static bool
register_rule(oc_resource_t *res_ruleinputcol, oc_resource_t *res_ruleactioncol)
{
  OC_PRINTF("Register Collection with local path \"/rule\"\n");
  oc_resource_t *res_rule = oc_new_collection("Rule", "/rule", 1, 0);
  if (res_rule == NULL) {
    OC_PRINTF("ERROR: could not allocate /rule\n");
    return false;
  }
  // Remove batch from the set of supported interafaces
  res_rule->interfaces = OC_IF_BASELINE | OC_IF_LL;
  oc_resource_bind_resource_type(res_rule, "oic.r.rule");
  oc_resource_set_discoverable(res_rule, true);

  if (!oc_collection_add_mandatory_rt(res_rule, "oic.r.rule.expression") ||
      !oc_collection_add_mandatory_rt(res_rule, "oic.r.rule.inputcollection") ||
      !oc_collection_add_mandatory_rt(res_rule,
                                      "oic.r.rule.actioncollection")) {
    OC_PRINTF("ERROR: could not add mandatory rt to /rule\n");
    return false;
  }

  if (!oc_collection_add_supported_rt(res_rule, "oic.r.rule.expression") ||
      !oc_collection_add_supported_rt(res_rule, "oic.r.rule.inputcollection") ||
      !oc_collection_add_supported_rt(res_rule,
                                      "oic.r.rule.actioncollection")) {
    OC_PRINTF("ERROR: could not add supported rt to /rule\n");
    return false;
  }

  if (!oc_add_collection_v1(res_rule)) {
    OC_PRINTF("ERROR: could not register /rule\n");
    return false;
  }

  oc_link_t *r1 = oc_new_link(res_ruleexpression);
  if (r1 == NULL) {
    OC_PRINTF("ERROR: could not allocate /ruleexpression link\n");
    return false;
  }
  oc_collection_add_link(res_rule, r1);

  oc_link_t *r2 = oc_new_link(res_ruleinputcol);
  if (r2 == NULL) {
    OC_PRINTF("ERROR: could not allocate /ruleinputcol link\n");
    return false;
  }
  oc_collection_add_link(res_rule, r2);

  oc_link_t *r3 = oc_new_link(res_ruleactioncol);
  if (r3 == NULL) {
    OC_PRINTF("ERROR: could not allocate /ruleactioncol link\n");
    return false;
  }
  oc_collection_add_link(res_rule, r3);
  return true;
}

#endif /* OC_COLLECTIONS */

/**
 * register all the resources to the stack
 * this function registers all application level resources:
 * - each resource path is bind to a specific function for the supported methods
 * (GET, POST, PUT)
 * - each resource is
 *   - secure
 *   - observable
 *   - discoverable
 *   - used interfaces (from the global variables).
 */
static void
register_resources(void)
{
  if (!register_switch()) {
    oc_abort("Failed to register /binaryswitch resource\n");
  }
  if (!register_audio()) {
    oc_abort("Failed to register /audio resource\n");
  }

#ifdef OC_COLLECTIONS
  oc_resource_t *res_scenemember1 = register_scenemember();
  if (res_scenemember1 == NULL) {
    oc_abort("Failed to register /scenemember1 resource\n");
  }
  if (!register_scenecollection(res_scenemember1)) {
    oc_abort("Failed to register /scenecollection1 resource\n");
  }
  if (!register_scenelist()) {
    oc_abort("Failed to register /scenelist resource\n");
  }
  if (!register_ruleexpression()) {
    oc_abort("Failed to register /ruleexpression resource\n");
  }
  oc_resource_t *res_ruleaction = register_ruleaction();
  if (res_ruleaction == NULL) {
    oc_abort("Failed to register /ruleaction resource\n");
  }
  oc_resource_t *res_ruleinputcol = register_ruleinputcollection();
  if (res_ruleinputcol == NULL) {
    oc_abort("Failed to register /ruleinputcollection resource\n");
  }
  oc_resource_t *res_ruleactioncol = register_ruleactioncol(res_ruleaction);
  if (res_ruleactioncol == NULL) {
    oc_abort("Failed to register /ruleactioncollection resource\n");
  }
  if (!register_rule(res_ruleinputcol, res_ruleactioncol)) {
    oc_abort("Failed to register /rule resource\n");
  }
#endif /* OC_COLLECTIONS */
}

/**
 * signal the event loop (Linux)
 * wakes up the main function to handle the next callback
 */
static void
signal_event_loop(void)
{
  pthread_cond_signal(&cv);
}

/**
 * handle Ctrl-C
 * @param signal the captured signal
 */
static void
handle_signal(int signal)
{
  (void)signal;
  OC_ATOMIC_STORE8(quit, 1);
  signal_event_loop();
}

/**
 * Display UUID of device
 */
static void
display_device_uuid(void)
{
  char buffer[OC_UUID_LEN];
  oc_uuid_to_str(oc_core_get_device_id(0), buffer, sizeof(buffer));

  OC_PRINTF("Started device with ID: %s\n", buffer);
}

static bool
init(void)
{
  struct sigaction sa;
  sigfillset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = handle_signal;
  sigaction(SIGINT, &sa, NULL);

  int err = pthread_mutex_init(&mutex, NULL);
  if (err != 0) {
    OC_PRINTF("ERROR: pthread_mutex_init failed (error=%d)!\n", err);
    return false;
  }
  pthread_condattr_t attr;
  err = pthread_condattr_init(&attr);
  if (err != 0) {
    OC_PRINTF("ERROR: pthread_condattr_init failed (error=%d)!\n", err);
    pthread_mutex_destroy(&mutex);
    return false;
  }
  err = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  if (err != 0) {
    OC_PRINTF("ERROR: pthread_condattr_setclock failed (error=%d)!\n", err);
    pthread_condattr_destroy(&attr);
    pthread_mutex_destroy(&mutex);
    return false;
  }
  err = pthread_cond_init(&cv, &attr);
  if (err != 0) {
    OC_PRINTF("ERROR: pthread_cond_init failed (error=%d)!\n", err);
    pthread_condattr_destroy(&attr);
    pthread_mutex_destroy(&mutex);
    return false;
  }
  pthread_condattr_destroy(&attr);
  return true;
}

static void
deinit(void)
{
  pthread_cond_destroy(&cv);
  pthread_mutex_destroy(&mutex);
}

static void
run_loop(void)
{
  /* linux specific loop */
  oc_clock_time_t next_event_mt;
  while (OC_ATOMIC_LOAD8(quit) != 1) {
    next_event_mt = oc_main_poll_v1();
    pthread_mutex_lock(&mutex);
    if (next_event_mt == 0) {
      pthread_cond_wait(&cv, &mutex);
    } else {
      struct timespec next_event = { 1, 0 };
      oc_clock_time_t next_event_cv;
      if (oc_clock_monotonic_time_to_posix(next_event_mt, CLOCK_MONOTONIC,
                                           &next_event_cv)) {
        next_event = oc_clock_time_to_timespec(next_event_cv);
      }
      pthread_cond_timedwait(&cv, &mutex, &next_event);
    }
    pthread_mutex_unlock(&mutex);
  }
}

/**
 * main application.
 * intializes the global variables
 * registers and starts the handler
 * handles (in a loop) the next event.
 * shuts down the stack
 */
int
main(void)
{
  if (!init()) {
    return -1;
  }

  /* set the flag for oic/con resource. */
  oc_set_con_res_announced(true);

  /* initializes the handlers structure */
  static const oc_handler_t handler = {
    .init = app_init,
    .signal_event_loop = signal_event_loop,
    .register_resources = register_resources,
#ifdef OC_CLIENT

    .requests_entry = 0,
#endif
  };

  OC_PRINTF("OCF Server name : \"Rules Test Server\"\n");

#ifdef OC_SECURITY
  OC_PRINTF("Intialize Secure Resources\n");
  oc_storage_config("./server_rules_creds");
#endif /* OC_SECURITY */
  oc_set_max_app_data_size(13312);

  if (pthread_create(&toggle_switch_thread, NULL, &toggle_switch_resource,
                     NULL) != 0) {
    deinit();
    return -1;
  }

  /* start the stack */
  int ret = oc_main_init(&handler);
  if (ret < 0) {
    deinit();
    return ret;
  }

  oc_resource_t *con_resource = oc_core_get_resource_by_index(OCF_CON, 0);
  oc_resource_set_observable(con_resource, false);

  display_device_uuid();
  OC_PRINTF("OCF server \"Rules Test Server\" running, waiting on incomming "
            "connections.\n");
  run_loop();

  /* free up strings and scenemappings */
  oc_free_string_array(&scenevalues);
  scenemappings_t *sm = (scenemappings_t *)oc_list_pop(smap);
  while (sm) {
    oc_memb_free(&smap_s, sm);
    sm = (scenemappings_t *)oc_list_pop(smap);
  }

  /* shut down the stack */
  oc_main_shutdown();
  pthread_join(toggle_switch_thread, NULL);
  deinit();
  return 0;
}
