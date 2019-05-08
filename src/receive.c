#include <curl/curl.h>
#include <stdio.h>

#include "authenticate.h"
#include "receive.h"
#include "memstruct.h"


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


int morse_exec_imap(const char *bearertoken, 
            const char *imap_url,
            const char *username,
            const char *command,
            MemoryStruct *mem  
            ) {
    MorseStatusCode result = MorseStatus_OK; 
    CURL *curl;
    CURLcode res = CURLE_OK;
    char *mailbox = "INBOX";
    char *base_url = GOOGLE_IMAPS;


    if (!mem) {
        fprintf(stderr, "Passed in memory struct cannot be null.\n");
        return (int)MorseStatus_InvalidArg;
    }

    if (!mem->memory) {
        fprintf(stderr, "Passed in memory struct doesn't have memory.\n");
        return (int)MorseStatus_MemoryError;
    }

    curl = curl_easy_init();
    if (curl) {
        printf("curl init OK [IMAP]\n");
        /* Set username and password */
        curl_easy_setopt(curl, CURLOPT_USERNAME, username);
        // curl_easy_setopt(curl, CURLOPT_PASSWORD, "secret");
        curl_easy_setopt(curl, CURLOPT_URL, imap_url);
        curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, "AUTH=XOAUTH2");
        curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, bearertoken);
        curl_easy_setopt(curl, CURLOPT_SASL_IR, 1L);

        // use SSL
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

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
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
            result = MorseStatus_CurlError;
        }

        /* Always cleanup */
        curl_easy_cleanup(curl);
    }

    return (int)result;
}


/*
 * Gets each line of an IMAP response and gets rid of the
 * asterisk and space at the beginning of each line. Caller
 * must free the returned list.
 */
struct curl_slist* get_response_lines(char *imap_output) {
    // using curl's built-in linked list
    struct curl_slist* lines = NULL;

     
    return lines;
}

/*
 * Gets the maximum (last) ID available
 */
unsigned long get_last_id_from(char *output) {
    unsigned long result = 0L; 


    return result;
}


int morse_retrieve_last_n(char *bearertoken, size_t n) {
    char *base_url = GOOGLE_IMAPS;
    char *gmail_imap_url = construct_url(base_url, "INBOX");
    char *username = getgmailaddress(bearertoken);
   
    MemoryStruct imap_result;
    memory_struct_init(&imap_result);
    int res = morse_exec_imap(bearertoken, gmail_imap_url, username, "UID FETCH 68676 BODY[TEXT]", &imap_result); 
    if (imap_result.memory) {
        printf("Result:\n");
        printf("%s", imap_result.memory);
        printf("%zu bytes\n", imap_result.size);
    }
    free(imap_result.memory);    
    free(gmail_imap_url);
    free(username);
    return res; 
}

