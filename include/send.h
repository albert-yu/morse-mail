#ifndef SEND_H
#define SEND_H

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "endpoints.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX_FILE_EXT_CHAR_COUNT
#define MAX_FILE_EXT_CHAR_COUNT 4
#endif


/**
 * Send an email to the designated recipients.
 * Recipients are comma-delimited strings.
 * @bearertoken - the token used for authentication
 * @to - the main recipient(s)
 * @cc - copied recipient(s)
 * @bcc - blind copied recipient(s)
 * @subject - email subject
 * @body - string of body
 * @mimetype - the MIME type of the body (text/html, text/plain)
 * @attachments - the file paths of the attachments, NULL-terminated
 */
int morse_sendmail(char *bearertoken, char *to, char *cc, char *bcc, 
             char *subject, char *body, char *mimetype,
             char **attachments);


#ifdef __cplusplus
}
#endif


#endif