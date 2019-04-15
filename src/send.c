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


/*
 * Add comma-delimited recipients to recipients (linked list)
 * Returns pointer to first item in list
 */
struct curl_slist* add_recipients(struct curl_slist *recipients, char *recips_str) {
    // if *recipients is NULL, that means list is empty, which is okay
    size_t rcpt_buffer_size = strlen(recips_str) + 1;
    char all_recips [rcpt_buffer_size];
    memset(all_recips, 0, rcpt_buffer_size);

    // copy recipients to alloc'd buffer  
    sprintf(all_recips, "%s", recips_str);

    char *individual_addr;  // each individual address
    char *saveptr;  // used by strtok_r
    char *to_addr_ptr = all_recips;
    char *delimiter = ",";

    individual_addr = strtok_r(to_addr_ptr, delimiter, &saveptr);        
    while (individual_addr) {
        // printf("indiv: (%s)\n", individual_addr);
        recipients = curl_slist_append(recipients, individual_addr);         
        individual_addr = strtok_r(NULL, delimiter, &saveptr);
    }

    saveptr = NULL;
    return recipients;
}


void print_list(struct curl_slist *list) {
    struct curl_slist     *next;
    struct curl_slist     *item;

    if(!list)
        return;

    item = list;
    do {
        next = item->next;
        printf("item: %s\n", item->data);
        item = next;
    } while(next);
}


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
                sprintf(from_header, "From: %s", from);
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

        
        // copy recipients to alloc'd buffer, because
        // add_recipients modifies the input string (second argument)
        char all_recips [rcpt_buffer_size];
        memset(all_recips, 0, rcpt_buffer_size);
        sprintf(all_recips, "%s", to);
        recipients = add_recipients(recipients, all_recips);

        print_list(recipients);

        // add CC
        if (cc) {
            // add CC header
            size_t cc_len = strlen(cc);
            char cc_header [cc_len + header_label_size];
            sprintf(cc_header, "Cc: %s", cc);
            headers = curl_slist_append(headers, cc_header);

            // re-use all_recips buffer
            memset(all_recips, 0, rcpt_buffer_size);
            sprintf(all_recips, "%s", cc);
            
            recipients = add_recipients(recipients, all_recips);
        }

        if (bcc) {
            // TODO
        }
        
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
