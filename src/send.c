#include <stdio.h>
#include <curl/curl.h>

#include "authenticate.h"  // curl is in httpclient.h
#include "file.h"
#include "mime.h"
#include "send.h"
#include "crypto.h"


#define MESSAGE_ID_SIZE 64


/**
 * Check if file exists on disk
 * https://stackoverflow.com/a/230068/9555588
 */
int fileexists(char *filename) {
    if (access(filename, F_OK) != -1) {
        return 1;
    } 
    // file doesn't exist otherwise
    return 0;
}


/**
 * Returns a nulled-out character buffer. Must be freed
 * by caller.
 */
char* getcharbuf(size_t buf_size) {
    char *buf = (char*)calloc(buf_size, sizeof(*buf));
    if (buf) {
        return buf;
    } else {
        fprintf(stderr, "Unable to allocate buffer for string.\n");
        return NULL;
    }

}


/**
 * Returns an empty string that needs to be freed.
 */
char* getemptystr() {
    char *empty_str = "\0";
    size_t size = 2;
    char *buf_empty = getcharbuf(size);
    if (buf_empty) {
        strcpy(buf_empty, empty_str);
    }    
    return buf_empty;
}

/**
 * Generates a random message ID
 */
char* generate_messageid() {
    // const size_t SIZE = 64;
    char *suffix = "@mailmorse.com>";
    char *messageid = getcharbuf(MESSAGE_ID_SIZE);
    if (!messageid) {
        return NULL;
    }
    size_t len_random = (MESSAGE_ID_SIZE - strlen(suffix) - 1) / 2;
    char *msg_ptr = messageid;

    // first copy over the left angle bracket
    strcpy(msg_ptr, "<");
    msg_ptr++;

    // generate random bytes
    char *temp_buffer = getcharbuf(len_random + 1);
    if (!temp_buffer) {
        free(messageid);
        return NULL;
    }
    populate_strbuffer(temp_buffer, len_random);

    // convert to hex and copy over
    char *random_hex = bytes_to_hex((unsigned char*)temp_buffer, len_random);
    strcpy(msg_ptr, random_hex);

    // add suffix    
    // hex representation is exactly twice the length
    msg_ptr += len_random * 2;  
    strcpy(msg_ptr, suffix);

    // gc
    free(random_hex);
    free(temp_buffer);
    
    return messageid;
}
 

/**
 * Is this string syntatically a valid extension? 
 */
int isvalidext(char *extension, size_t len) {
    // named falsy instead of false in case stdbool is used later
    int falsy = 0;  
    if (len == 0) {
        return falsy;
    }
    if (len > MAX_FILE_EXT_CHAR_COUNT) {
        // we will just consider it invalid
        // if it is not a file type for sending email
        return falsy;
    }
    char *ptr = extension;
    char c;
    while ((c = *ptr) != 0) {
        if (!isalpha(c)) {
            return falsy;
        }
        ++ptr;
    }

    return 1; // true
}


// /*
//  * Add comma-delimited recipients to curl
//  */
// void add_recipients(CURL *curl, char *recipients, char *header_label) {
//     size_t to_len = strlen(to);
//     size_t rcpt_buffer_size = to_len + header_label_size;
//     char to_header [rcpt_buffer_size];
//     memset(to_header, 0, rcpt_buffer_size);

//     char *individual_addr;  // each individual address
//     char *saveptr;  // used by strtok_r
//     char *to_addr_ptr = to;
//     char *delimiter = ",";

//     individual_addr = strtok_r(to_addr_ptr, delimiter, &saveptr);
//     while (individual_addr) {
//         sprintf(to_header, "To: <%s>", individual_addr);
//         headers = curl_slist_append(headers, to_header);
//         recipients = curl_slist_append(recipients, individual_addr);
//         memset(to_header, 0, rcpt_buffer_size);
//         individual_addr = strtok_r(NULL, delimiter, &saveptr);
//     }
// }


/**
 * MAIN implementation of sendmail
 */
int sendmail_inner(char *from, char *to, char *cc, char *bcc, 
             char *subject, char *body, char *mimetype,
             char **attachments) {
    // validate inputs
    // add TO (required)
    if (!to) {
        fprintf(stderr, "Recipient must be supplied.\n");
        return -1;
    }
    if (strlen(to) == 0) {
        fprintf(stderr, "Cannot provide empty recipient.\n");
        return -1;
    }

    // will point to curl handle
    CURL *curl;

    // default return result
    CURLcode res = CURLE_OK;

    // curl_slist is a linked list
    struct curl_slist *headers = NULL;
    struct curl_slist *recipients = NULL;
    struct curl_slist *slist = NULL;
    curl_mime *mime;
    curl_mime *alt;
    curl_mimepart *part;
    // const char **cpp;

    printf("Getting the bearer token...\n");
    char *bearer_token = getgooglebearertoken();

    if (!bearer_token) {
        fprintf(stderr, "Failed to get bearer token.\n");
        exit(1);
    }
    printf("Got token.\n");
    // printf("token: %s\n", bearer_token);
    
    curl = curl_easy_init();
    if (curl) {
        printf("curl init OK\n");       
        curl_easy_setopt(curl, CURLOPT_URL, GOOGLE_SMTPS);

        // auth        
        curl_easy_setopt(curl, CURLOPT_USERNAME, from);
        curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, "AUTH=XOAUTH2");
        curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, bearer_token);
        curl_easy_setopt(curl, CURLOPT_SASL_IR, 1L);

        // use SSL
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

        // set a timeout
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

        // verbose output
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        // provides space for adding label (e.g. "From: ")
        size_t header_label_size = 32;

        // add FROM (not strictly required)
        if (from) {
            size_t from_len = strlen(from);
            if (from_len > 0) {
                char from_header [from_len + header_label_size];
                sprintf(from_header, "From: <%s>", from);
                headers = curl_slist_append(headers, from_header);
                curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from);               
            }          
        }      

        size_t to_len = strlen(to);
        size_t rcpt_buffer_size = to_len + header_label_size;
        char to_header [rcpt_buffer_size];
        memset(to_header, 0, rcpt_buffer_size);
        sprintf(to_header, "To: %s", to);            
        headers = curl_slist_append(headers, to_header);
        // curl_slist_append copies the string,
        // so we can reuse this buffer
        memset(to_header, 0, rcpt_buffer_size);

        // copy recipients to alloc'd buffer
        char all_recips [rcpt_buffer_size];
        memset(all_recips, 0, rcpt_buffer_size);
        sprintf(all_recips, "%s", to);

        char *individual_addr;  // each individual address
        char *saveptr;  // used by strtok_r
        char *to_addr_ptr = all_recips;
        char *delimiter = ",";

        individual_addr = strtok_r(to_addr_ptr, delimiter, &saveptr);        
        while (individual_addr) {
            recipients = curl_slist_append(recipients, individual_addr);            
            individual_addr = strtok_r(NULL, delimiter, &saveptr);
        }
        printf("foo\n");

        // sprintf(to_header, "To: <%s>", to);
        // headers = curl_slist_append(headers, to_header);
        // recipients = curl_slist_append(recipients, to);

        // add CC
        // if (cc) {
        //     size_t cc_len = strlen(cc);
        //     char cc_header [cc_len + header_label_size];
        //     sprintf(cc_header, "Cc: <%s>", cc);
        //     headers = curl_slist_append(headers, cc_header);
        //     recipients = curl_slist_append(recipients, cc);
        // }
        
        // add all the recipients
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        // add the subject header
        if (subject) {
            size_t subject_len = strlen(subject);
            char subject_header [subject_len + header_label_size];
            sprintf(subject_header, "Subject: %s", subject);
            headers = curl_slist_append(headers, subject_header);
        } // else, handle empty subject?

        // add the message ID
        char *message_id = generate_messageid();
        if (message_id) {
            char message_id_header [MESSAGE_ID_SIZE + header_label_size];
            sprintf(message_id_header, "Message-ID: %s", message_id);
            headers = curl_slist_append(headers, message_id_header);
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        if (body && mimetype) {
            mime = curl_mime_init(curl);
            alt = curl_mime_init(curl);
            if (strcmp(mimetype, MIME_TYPE_HTML) == 0) {
                // MIME_x constants defined in mime.h
                
                // HTML
                part = curl_mime_addpart(alt);
                curl_mime_data(part, body, CURL_ZERO_TERMINATED);
                curl_mime_type(part, MIME_TYPE_HTML);
            }
            else {
                // plain text
                part = curl_mime_addpart(alt);
                curl_mime_data(part, body, CURL_ZERO_TERMINATED);
            }
        }       

        /* Create the inline part. */
        part = curl_mime_addpart(mime);
        curl_mime_subparts(part, alt);
        curl_mime_type(part, "multipart/alternative");
        slist = curl_slist_append(NULL, "Content-Disposition: inline");
        curl_mime_headers(part, slist, 1);

        if (attachments) {
            char *attachment = *attachments;
            while (attachment) {
                if (fileexists(attachment)) {
                    part = curl_mime_addpart(mime);
                    curl_mime_filedata(part, attachment);                   
                }
                ++attachment;
            }         
        }

        // set mime
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

        printf("Sending message....\n");
        /* Send the message */
        res = curl_easy_perform(curl);

        /* Check for errors */
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                  curl_easy_strerror(res));
        }
          
        curl_cleanup:  
            printf("Cleaning...\n");
            /* Free lists. */
            curl_slist_free_all(recipients);
            curl_slist_free_all(headers);

            /* curl won't send the QUIT command until you call cleanup, so you should
             * be able to re-use this connection for additional messages (setting
             * CURLOPT_MAIL_FROM and CURLOPT_MAIL_RCPT as required, and calling
             * curl_easy_perform() again. It may not be a good idea to keep the
             * connection open for a very long time though (more than a few minutes
             * may result in the server timing out the connection), and you do want to
             * clean up in the end.
             */
            curl_easy_cleanup(curl);

            /* Free multipart message. */
            curl_mime_free(mime);
    }
    free(bearer_token);
    printf("Done.\n");

    return (int)res;
}


int sendmail(char *to, char *cc, char *bcc, 
             char *subject, char *body, char *mimetype,
             char **attachments) {
    char *user_email = getgmailaddress();
    int result = sendmail_inner(user_email, to, cc, bcc, subject, body, mimetype, attachments);
    free(user_email);
    return result;
}


//------------------------------------------------------------
// Below is modified sample code from curl

#define FROM    GOOGLE_EMAIL_W_BRACKETS
#define TO      GOOGLE_EMAIL_W_BRACKETS
#define CC      ""


static const char *headers_text[] = {
  "Date: Wed, 30 Jan 2019 17:02:43 +0100",
  "To: " TO,
  "From: " FROM " (Example User)",
  // "Cc: " CC " (Another example User)",
  "Message-ID: <dcd7cb38-11eb-487a-9f3a-e652a9458efd@"
    "rfcpedant.example.org>",
  "Subject: example sending a MIME-formatted message",
  NULL
};

static const char inline_text[] =
  "This is the inline text message of the e-mail.\r\n"
  "\r\n"
  "  It could be a lot of lines that would be displayed in an e-mail\r\n"
  "viewer that is not able to handle HTML.\r\n";

static const char inline_html[] =
  "<html><body>\r\n"
  "<p>This is the inline <b>HTML</b> message of the e-mail.</p>"
  "<br />\r\n"
  "<p>It could be a lot of HTML data that would be displayed by "
  "e-mail viewers able to handle HTML.</p>"
  "</body></html>\r\n";


int test_smtp(void) {
    CURL *curl;
    CURLcode res = CURLE_OK;
    struct curl_slist *headers = NULL;
    struct curl_slist *recipients = NULL;
    struct curl_slist *slist = NULL;
    curl_mime *mime;
    curl_mime *alt;
    curl_mimepart *part;
    const char **cpp;

    printf("Getting the bearer token...\n");
    char *bearer_token = getgooglebearertoken();
    printf("token: %s\n", bearer_token);

    if (!bearer_token) {
        fprintf(stderr, "Failed to get bearer token.\n");
        exit(1);
    }

    curl = curl_easy_init();
    if (curl) {
        printf("curl init OK\n");       
        /* This is the URL for your mailserver */
        curl_easy_setopt(curl, CURLOPT_URL, GOOGLE_SMTPS);

        // auth        
        curl_easy_setopt(curl, CURLOPT_USERNAME, GOOGLE_EMAIL);
        curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, "AUTH=XOAUTH2");
        curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, bearer_token);
        curl_easy_setopt(curl, CURLOPT_SASL_IR, 1L);

        /* Note that this option isn't strictly required, omitting it will result
         * in libcurl sending the MAIL FROM command with empty sender data. All
         * autoresponses should have an empty reverse-path, and should be directed
         * to the address in the reverse-path which triggered them. Otherwise,
         * they could cause an endless loop. See RFC 5321 Section 4.5.5 for more
         * details.
         */
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, FROM);

        // use SSL
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

        // set a timeout
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

        // verbose output
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        /* Add two recipients, in this particular case they correspond to the
         * To: and Cc: addressees in the header, but they could be any kind of
         * recipient. */
        recipients = curl_slist_append(recipients, TO);
        // recipients = curl_slist_append(recipients, CC);
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        /* Build and set the message header list. */
        for(cpp = headers_text; *cpp; cpp++)
            headers = curl_slist_append(headers, *cpp);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        /* Build the mime message. */
        mime = curl_mime_init(curl);

        /* The inline part is an alternative proposing the html and the text
           versions of the e-mail. */
        alt = curl_mime_init(curl);

        /* HTML message. */
        part = curl_mime_addpart(alt);
        curl_mime_data(part, inline_html, CURL_ZERO_TERMINATED);
        curl_mime_type(part, "text/html");

        /* Text message. */
        // part = curl_mime_addpart(alt);
        // curl_mime_data(part, inline_text, CURL_ZERO_TERMINATED);

        /* Create the inline part. */
        part = curl_mime_addpart(mime);
        curl_mime_subparts(part, alt);
        curl_mime_type(part, "multipart/alternative");
        slist = curl_slist_append(NULL, "Content-Disposition: inline");
        curl_mime_headers(part, slist, 1);

        /* Add the README file as an attachment. */
        part = curl_mime_addpart(mime);
        curl_mime_filedata(part, "README.md");
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

        printf("Sending message....\n");
        /* Send the message */
        res = curl_easy_perform(curl);

        /* Check for errors */
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                  curl_easy_strerror(res));
        }
            
        /* Free lists. */
        curl_slist_free_all(recipients);
        curl_slist_free_all(headers);

        /* curl won't send the QUIT command until you call cleanup, so you should
         * be able to re-use this connection for additional messages (setting
         * CURLOPT_MAIL_FROM and CURLOPT_MAIL_RCPT as required, and calling
         * curl_easy_perform() again. It may not be a good idea to keep the
         * connection open for a very long time though (more than a few minutes
         * may result in the server timing out the connection), and you do want to
         * clean up in the end.
         */
        curl_easy_cleanup(curl);

        /* Free multipart message. */
        curl_mime_free(mime);
    }
    free(bearer_token);

    return (int)res;
}


