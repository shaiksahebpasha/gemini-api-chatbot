/* eris.c - Minimal Gemini API chatbot in C.
 *
 * Requirements: libcurl
 * Linux: gcc -std=c11 -Wall -Wextra -O2 eris.c -o eris -lcurl
 * Set GEMINI_API_KEY before running.
 */

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_INPUT 4096
#define MAX_HISTORY 30000
#define MAX_RESPONSE 200000
#define MAX_REQUEST 100000

struct Buffer {
    char *data;
    size_t size;
    size_t capacity;
};

static int buffer_init(struct Buffer *b, size_t capacity) {
    b->data = malloc(capacity);
    if (!b->data) return 0;
    b->data[0] = '\0';
    b->size = 0;
    b->capacity = capacity;
    return 1;
}

static void buffer_free(struct Buffer *b) {
    free(b->data);
    b->data = NULL;
    b->size = b->capacity = 0;
}

static int buffer_append(struct Buffer *b, const char *src, size_t len) {
    if (len > b->capacity - b->size - 1) return 0;
    memcpy(b->data + b->size, src, len);
    b->size += len;
    b->data[b->size] = '\0';
    return 1;
}

static size_t receive_response(void *contents, size_t size, size_t count,
                               void *user_pointer) {
    size_t total = size * count;
    struct Buffer *memory = user_pointer;
    if (!buffer_append(memory, contents, total)) return 0; /* abort curl safely */
    return total;
}

/* Append one JSON-escaped string to out. Returns 0 if it does not fit. */
static int append_json_string(struct Buffer *out, const char *input) {
    size_t i;
    if (!buffer_append(out, "\"", 1)) return 0;

    for (i = 0; input[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)input[i];
        const char *escaped = NULL;
        char one[2] = { (char)c, '\0' };

        switch (c) {
            case '"': escaped = "\\\""; break;
            case '\\': escaped = "\\\\"; break;
            case '\n': escaped = "\\n"; break;
            case '\r': escaped = "\\r"; break;
            case '\t': escaped = "\\t"; break;
            case '\b': escaped = "\\b"; break;
            case '\f': escaped = "\\f"; break;
            default:
                if (c < 0x20) {
                    char unicode_escape[7];
                    snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04x", c);
                    if (!buffer_append(out, unicode_escape, 6)) return 0;
                } else if (!buffer_append(out, one, 1)) {
                    return 0;
                }
                continue;
        }
        if (!buffer_append(out, escaped, strlen(escaped))) return 0;
    }
    return buffer_append(out, "\"", 1);
}

static int append_history_message(struct Buffer *request, const char *role,
                                  const char *text) {
    if (request->size > 0 && !buffer_append(request, ",", 1)) return 0;
    if (!buffer_append(request, "{\"role\":", 8)) return 0;
    if (!append_json_string(request, role)) return 0;
    if (!buffer_append(request, ",\"parts\":[{\"text\":", 20)) return 0;
    if (!append_json_string(request, text)) return 0;
    return buffer_append(request, "}]}", 3);
}

/* Extract the first text value from Gemini's JSON response. */
static int extract_text(const char *json, char *output, size_t output_size) {
    const char *p = json;
    size_t out = 0;

    while ((p = strstr(p, "\"text\"")) != NULL) {
        p += 6;
        while (*p && *p != ':') ++p;
        if (!*p) break;
        ++p;
        while (isspace((unsigned char)*p)) ++p;
        if (*p != '"') continue;
        ++p;

        while (*p && *p != '"') {
            unsigned char c = (unsigned char)*p++;
            if (c == '\\') {
                c = (unsigned char)*p++;
                switch (c) {
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case '"': case '\\': case '/': break;
                    default: break; /* preserve unsupported escapes approximately */
                }
            }
            if (out + 1 >= output_size) break;
            output[out++] = (char)c;
        }
        output[out] = '\0';
        return out > 0;
    }
    if (output_size) output[0] = '\0';
    return 0;
}

static int call_gemini(const char *api_key, const char *model,
                       const char *history, char *answer, size_t answer_size) {
    CURL *curl = NULL;
    CURLcode result;
    long http_code = 0;
    struct Buffer response;
    struct Buffer request;
    struct curl_slist *headers = NULL;
    char url[512];
    int ok = 0;

    if (!buffer_init(&response, MAX_RESPONSE) || !buffer_init(&request, MAX_REQUEST)) {
        fprintf(stderr, "Out of memory.\n");
        buffer_free(&response);
        buffer_free(&request);
        return 0;
    }

    if (!buffer_append(&request, "{\"contents\":[", 13) ||
        !buffer_append(&request, history, strlen(history)) ||
        !buffer_append(&request, "]}", 2)) {
        fprintf(stderr, "Request is too large.\n");
        goto cleanup;
    }

    snprintf(url, sizeof(url),
             "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s",
             model, api_key);

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Could not initialize libcurl.\n");
        goto cleanup;
    }
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "eris-c-gemini-client/1.0");

    result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (result != CURLE_OK) {
        fprintf(stderr, "Network error: %s\n", curl_easy_strerror(result));
        goto cleanup;
    }
    if (http_code < 200 || http_code >= 300) {
        fprintf(stderr, "Gemini HTTP error %ld:\n%s\n", http_code, response.data);
        goto cleanup;
    }
    if (!extract_text(response.data, answer, answer_size)) {
        fprintf(stderr, "Gemini returned no text. Raw response:\n%s\n", response.data);
        goto cleanup;
    }
    ok = 1;

cleanup:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    buffer_free(&response);
    buffer_free(&request);
    return ok;
}

int main(void) {
    const char *api_key = getenv("GEMINI_API_KEY");
    const char *model = getenv("GEMINI_MODEL");
    char input[MAX_INPUT];
    char answer[MAX_RESPONSE];
    struct Buffer history;

    if (!api_key || !*api_key) {
        fprintf(stderr, "Set GEMINI_API_KEY first.\n");
        fprintf(stderr, "Example: export GEMINI_API_KEY='your-key'\n");
        return EXIT_FAILURE;
    }
    if (!model || !*model) model = "gemini-2.5-flash";
    if (!buffer_init(&history, MAX_HISTORY)) {
        fprintf(stderr, "Out of memory.\n");
        return EXIT_FAILURE;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        fprintf(stderr, "Could not initialize curl.\n");
        buffer_free(&history);
        return EXIT_FAILURE;
    }

    printf("ErIs Gemini chatbot (%s)\nType /quit to exit, /clear to reset.\n\n", model);
    for (;;) {
        printf("You: ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0';
        if (!*input) continue;
        if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) break;
        if (strcmp(input, "/clear") == 0) {
            history.size = 0;
            history.data[0] = '\0';
            puts("Conversation cleared.");
            continue;
        }

        size_t old_size = history.size;
        if (old_size > 0 && !buffer_append(&history, ",", 1)) {
            fprintf(stderr, "Conversation history is full; use /clear.\n");
            continue;
        }
        if (!append_history_message(&history, "user", input)) {
            history.size = old_size;
            history.data[history.size] = '\0';
            fprintf(stderr, "Conversation history is full; use /clear.\n");
            continue;
        }

        printf("Gemini: ");
        fflush(stdout);
        if (!call_gemini(api_key, model, history.data, answer, sizeof(answer))) {
            history.size = old_size;
            history.data[history.size] = '\0';
            continue;
        }
        puts(answer);

        old_size = history.size;
        if ((old_size > 0 && !buffer_append(&history, ",", 1)) ||
            !append_history_message(&history, "model", answer)) {
            history.size = old_size;
            history.data[history.size] = '\0';
            fprintf(stderr, "Response not saved; use /clear.\n");
        }
    }

    buffer_free(&history);
    curl_global_cleanup();
    return EXIT_SUCCESS;
}
