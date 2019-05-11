#include <stdio.h>

#include "authenticate.h"
#include "curlyfries.h"
#include "imapcmd.h"
#include "imap_request.h"
#include "imap_response.h"
#include "memstruct.h"
#include "receive.h"


/**
 * Caller must free returned buffer
 */
char* construct_url(char *base_url, char *path) {
    if (!base_url) {
        fprintf(stderr, "Null base URL supplied.\n");
        return NULL;
    }
    int path_is_nonempty = 1;
    if (!path) {
        path_is_nonempty = 0;
    }

    size_t total_len;
    
    if (path_is_nonempty) {
        total_len = strlen(path) + strlen(base_url);
    }
    else {
        total_len = strlen(base_url);
    }
    // total_len + 2 to account for forward slash
    char *buffer = calloc(total_len + 2, sizeof(*buffer));
    if (!buffer) {
        fprintf(stderr, "Failed to allocate memory.\n");
        return NULL;
    }

    if (path_is_nonempty) {
        sprintf(buffer, "%s/%s", base_url, path);
    }
    else {
        strcpy(buffer, base_url);
    }
    return buffer;
}


ImapResponse* morse_exec_imap_stateful(CURL *curl, char *command) {
    ImapResponse *response = imap_response_new();

    // set a timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    // verbose output
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    // COMMAND GOES HERE
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, command);

    // set callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curl_mem_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)response->data);

    /* Perform the fetch */
    CURLcode res = curl_easy_perform(curl);

    /* Check for errors */
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
        response->status = (int)MorseStatus_CurlError;
    }

    return response;
}


int morse_exec_imap_xoauth2(const char *bearertoken, 
                            const char *imap_url,
                            const char *username,
                            const char *command,
                            MemoryStruct *mem  
                            ) {
    MorseStatusCode result = MorseStatus_OK; 
    CURL *curl;
    CURLcode res = CURLE_OK;

    if (!mem) {
        fprintf(stderr, "Passed in memory struct cannot be null.\n");
        return (int)MorseStatus_InvalidArg;
    }

    if (!mem->memory) {
        fprintf(stderr, "Passed in memory struct doesn't have memory.\n");
        return (int)MorseStatus_MemoryError;
    }

    curl = get_curl_xoauth2(bearertoken, imap_url, username);
    if (curl) {
        printf("curl init OK [IMAP]\n");
        // /* Set username and password */
        // curl_easy_setopt(curl, CURLOPT_USERNAME, username);
        // // curl_easy_setopt(curl, CURLOPT_PASSWORD, "secret");
        // curl_easy_setopt(curl, CURLOPT_URL, imap_url);
        // curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, "AUTH=XOAUTH2");
        // curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, bearertoken);
        // curl_easy_setopt(curl, CURLOPT_SASL_IR, 1L);

        // // use SSL
        // curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

        // set a timeout
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

        // verbose output
        // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        // COMMAND GOES HERE
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, command);

        // set callback
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curl_mem_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)mem);

        /* Perform the fetch */
        res = curl_easy_perform(curl);

        /* Check for errors */
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
            result = MorseStatus_CurlError;
        }

        /* Always cleanup */
        curl_easy_cleanup(curl);
    }

    return (int)result;
}


typedef enum retchar_t {
    /* this string contains no newline chars */
    RET_NONE,

    /* this string uses \n */
    RET_NEWLINE,

    /* this string uses \r\n */
    RET_CARRIAGE
} ReturnChar;


ReturnChar get_return_type(char *string) {
    ReturnChar retchar = RET_NONE;

    if (!string) {
        // this is technically true
        // in the event of a NULL ptr
        return retchar;
    }

    // check if we have a newline char
    char *ptr = strchr(string, '\n');
    if (!ptr) {
        return retchar; // NO return characters
    }
   
    retchar = RET_NEWLINE;
    char *r_ptr = strchr(string, '\r');

    if (r_ptr && (r_ptr[1] == '\n')) {
        retchar = RET_CARRIAGE;  
    }
    return retchar;
}


/*
 * Gets each line of an IMAP response.
 * Caller must free the returned list.
 */
struct curl_slist* get_response_lines(char *imap_output) {
    // using curl's built-in linked list
    struct curl_slist* lines = NULL;
    
    // determine type of return (\r\n or \n)
    ReturnChar newline_char = get_return_type(imap_output);
        
    if (newline_char == RET_NONE) {
        // just return the input as the first element
        // of the linked list
        lines = curl_slist_append(lines, imap_output);
        return lines;
    }

    // do not modify original string
    char *copy_of_imap_output = strdup(imap_output);
    if (!copy_of_imap_output) {
        fprintf(stderr, "Not enough memory for IMAP output\n");
        return NULL;
    }

    // save ptr
    char *ptr = NULL;

    // is this carriage return or newline feed?
    char *line_sep; 
    if (newline_char == RET_NEWLINE) {
        line_sep = "\n";
    } else {
        line_sep = "\r\n";
    }

    ptr = strtok(copy_of_imap_output, line_sep);
    while (ptr != NULL) {
        lines = curl_slist_append(lines, ptr);
        ptr = strtok(NULL, line_sep);
    }

    free(copy_of_imap_output);
        
    return lines;
}


CURL* get_imap_curl_google() {
    char *base_url = GOOGLE_IMAPS;
    char *gmail_imap_url = construct_url(base_url, "INBOX");
    CURL *curl = get_google_curl(gmail_imap_url);
    return curl;
}

// ImapResponse* morse_exec_imap_google(char *imap_cmd) {
//     char *base_url = GOOGLE_IMAPS;
//     char *gmail_imap_url = construct_url(base_url, "INBOX");
//     char *bearertoken = getgooglebearertoken(); 
//     char *username = getgmailaddress(bearertoken);
//     // ImapResponse *response = imap_response_new();

//     // execute the command and get the response
//     // int res = morse_exec_imap_xoauth2(
//     //     bearertoken,
//     //     gmail_imap_url,
//     //     username,
//     //     imap_cmd,
//     //     response->data
//     //     );

//     ImapResponse *response;
//     CURL *curl = get_curl_xoauth2(bearertoken, gmail_imap_url, username);
//     if (curl) {
//         response = morse_exec_imap_stateful(curl, imap_cmd);
//         curl_easy_cleanup(curl);
//     }

//     // clean up
//     free(gmail_imap_url);
//     free(username);
//     free(bearertoken);

//     return response;
// }


int morse_list_folders() {
    char *command1 = imapcmd_select_box("[Gmail]/All Mail");
    char *command2 = imapcmd_list_messages();

    CURL *curl;
    curl = get_imap_curl_google();
    ImapResponse *response1 = morse_exec_imap_stateful(curl, command1);
    if (response1) {
        if (response1->status == 0) {
            printf("%s\n", response1->data->memory);
            printf("\n");
            ImapResponse *second_resp = morse_exec_imap_stateful(curl, command2);
            if (second_resp) {
                printf("%s\n", second_resp->data->memory);
                imap_response_free(second_resp);
            }
        }
        imap_response_free(response1);
    }

    free(command1);
    free(command2);
    curl_easy_cleanup(curl);
    return 0;
}

