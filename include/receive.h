#ifndef RECEIVE_H
#define RECEIVE_H

#include "endpoints.h"
#include "statuscodes.h"


#ifdef __cplusplus
extern "C" {
#endif

int morse_retrieve_last_n(char *bearertoken, size_t n);

#ifdef __cplusplus
}
#endif

#endif
