# mbedTLS patches

Applicable to mbedTLS v3.5.0.

## Functional patch 01-ocf-anon-psk.patch

Extends mbedTLS functionality by:

* MBEDTLS_KEY_EXCHANGE_ECDH_ANON
* mbedtls_ssl_conf_ekus
* psk identity hint
* mbedtls_ssl_conf_iterate_own_certs
* compiler warning fixes

How to create:

Go to mbedTLS directory and stage all modified files except `.gitignore`  and `mbedtls_config.h`. Save the diff to `01-ocf-anon-psk.patch`:

```sh
cd deps/mbedtls
git add -u
git reset .gitignore include/mbedtls/mbedtls_config.h
git diff --staged > ../../patches/mbedtls/3.5/01-ocf-anon-psk.patch
```

Unstage everything again by:

```sh
git reset HEAD .
```

## Configuration patch 02-ocf-mbedtls-config.patch

The difference between CMake and Make builds is that CMake uses mbedTLS as a library (ie. it links with a `.a` or a `.so` file), whilst Make includes mbedTLS source files directly into IoTivity-lite targets. These two builds systems require slightly different includes and macro definitions to work, so two patches are needed.

The main difference is that for Make, mbedTLS is only build within IoTivity-lite source code, so all macro definitions and functions are available. For CMake there are testing utilities that can be built and that are independent of IoTivity-lite. These testing utilities are useful to run if we want to know that our functional patches didn't break any mbedTLS functionality. To make mbedTLS generate these utility target set `ENABLE_TESTING=ON` or `ENABLE_PROGRAMS=ON`.

For Make we can directly include headers and refer to functions from Iotivity-lite:

`mbedtls_config.h` for Makefile build:

```C
...

#include <oc_config.h>
#include <oc_log.h>               // OC_LOG_LEVEL_DEBUG_MACRO, OC_LOG_LEVEL_ERROR_MACRO
#include "port/oc_connectivity.h" // OC_PDU_SIZE
#include "port/oc_assert.h"       // oc_exit

...

#define MBEDTLS_PLATFORM_STD_EXIT oc_exit

...

#ifndef OC_DYNAMIC_ALLOCATION
#define MBEDTLS_SSL_IN_CONTENT_LEN (OC_PDU_SIZE)
#endif

#ifndef OC_DYNAMIC_ALLOCATION
#define MBEDTLS_SSL_OUT_CONTENT_LEN (OC_PDU_SIZE)
#endif /* !OC_DYNAMIC_ALLOCATION */

...

```

For CMake we can include headers and refer to functions from Iotivity-lite when building IoTivity-lite targets, but when building stand-alone mbedTLS utilities everything has to be self-contained.
To do this we use `__OC_PLATFORM` macro, which is defined when building Iotivity-lite targets and not defined when building mbedTLS stand-alone targets. We hide the includes into `mbedtls_oc_platform.h`, which is included by `mbedtls_config.h` and generated by CMake depending on the type of the build.

`mbedtls_config.h` for CMake build:

```C
...

#include "mbedtls_oc_platform.h" // __OC_SSL_CONTENT_LEN

...
#ifdef __OC_PLATFORM
#define MBEDTLS_PLATFORM_STD_EXIT oc_exit
#endif /* __OC_PLATFORM */
...

#ifdef __OC_SSL_CONTENT_LEN
#define MBEDTLS_SSL_IN_CONTENT_LEN (__OC_SSL_CONTENT_LEN)
#endif /* !__OC_SSL_CONTENT_LEN */

#ifdef __OC_SSL_CONTENT_LEN
#define MBEDTLS_SSL_OUT_CONTENT_LEN (__OC_SSL_CONTENT_LEN)
#endif /* !__OC_SSL_CONTENT_LEN */

...

```

`mbedtls_oc_platform.h` is generated either from `mbedtls_oc_platform.h.in` or `mbedtls_oc_platform-standalone.h.in` depending whether building of mbedTLS standalone utilities is enabled.

### Configuration patch 02-ocf-mbedtls-config.patch for CMake

Updates `mbedtls_config.h` to configure mbedTLS to work with IoTivity-lite library. Used when building by CMake.

How to create:

Go to mbedTLS directory and stage `.gitignore`, `mbedtls_config.h`, `mbedtls_oc_platform-standalone.h.in` and `mbedtls_oc_platform.h.in`. Save the diff to `cmake/02-ocf-mbedtls-config.patch`:

```sh
git add .gitignore include/mbedtls/mbedtls_config.h include/mbedtls/mbedtls_oc_platform-standalone.h.in include/mbedtls/mbedtls_oc_platform.h.in
git diff --staged > ../../patches/mbedtls/3.5/cmake/02-ocf-mbedtls-config.patch
```

### Configuration patch 02-ocf-mbedtls-config.patch for Make

Updates `mbedtls_config.h` to configure mbedTLS to work with IoTivity-lite library. Used when building by Make.

Go to mbedTLS directory and stage `mbedtls_config.h`. Save the diff to `make/02-ocf-mbedtls-config.patch`:

```sh
git add .gitignore include/mbedtls/mbedtls_config.h
git diff --staged > ../../patches/mbedtls/3.5/make/02-ocf-mbedtls-config.patch
```