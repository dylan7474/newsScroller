/*
 * main.c - A multi-line, multi-speed news ticker using SDL2, SDL_ttf, and cJSON.
 *
 * Features:
 * - Renders smooth text using TrueType fonts (SDL_ttf).
 * - Parses news headlines safely using the cJSON library.
 * - Loads settings from an external 'config.ini' file.
 * - Each headline scrolls at an independent, random speed.
 * - Each headline is displayed in a random color from a predefined list.
 * - Press SPACE to pause/resume scrolling.
 * - Press ESC to quit.
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <curl/curl.h>
#include "cJSON.h" // For robust JSON parsing

// --- Structs ---

// Holds settings loaded from config.ini
struct Config {
    char api_key[128];
    char font_path[256];
    char country_code[8]; // Added country code
    int font_size;
    SDL_Color background_color;
    SDL_Color colors[10];
    int num_colors;
};

// Holds the data for a single scrolling line of text
struct NewsLine {
    char* text;
    bool owns_text;
    float scroll_x;
    float scroll_speed;
    int y_position;
    SDL_Color color;
    SDL_Texture* texture;
    int texture_width;
    int texture_height;
};

// Used by libcurl to store fetched data in memory
struct MemoryStruct {
    char *memory;
    size_t size;
};

// --- Constants ---
#define MAX_LINES 20 // Maximum number of headlines to display
#define LINE_PADDING 10 // Vertical space between lines
#define MIN_SCROLL_SPEED 90.0f // Pixels per second
#define MAX_SCROLL_SPEED 220.0f // Pixels per second
#define MAX_FETCH_ATTEMPTS 3

#define STATUS_BUFFER 256

// --- Globals ---
const char* fallback_news[] = {
    "HELLO! THIS IS THE DEFAULT NEWS FEED.",
    "PLEASE CHECK YOUR 'config.ini' FILE.",
    "ENSURE YOUR NEWSAPI.ORG API KEY AND COUNTRY CODE ARE CORRECT.",
    "YOU CAN PAUSE THIS TICKER BY PRESSING THE SPACEBAR.",
    NULL
};

// --- Function Prototypes ---
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
bool parse_config(struct Config* config, char *error_message, size_t message_len);
void render_text(SDL_Renderer* renderer, TTF_Font* font, struct NewsLine* line);
void trim_whitespace(char *str);
char* sanitize_headline(const char *title);
void release_news_line(struct NewsLine *line);
bool init_news_line(struct NewsLine *line, SDL_Renderer *renderer, TTF_Font *font, char *text, bool owns_text, SDL_Color color, int screen_width, int screen_height, int *y_cursor);
void append_message(char *buffer, size_t len, const char *message);
char normalize_ascii_char(unsigned char c);
size_t utf8_sequence_length(unsigned char lead_byte);


// --- Main Function ---
int main(int argc, char* argv[]) {
    srand(time(NULL));

    // --- Load Configuration ---
    struct Config config;
    char config_error[STATUS_BUFFER] = {0};
    bool config_ok = parse_config(&config, config_error, sizeof(config_error));
    if (!config_ok && config_error[0] != '\0') {
        fprintf(stderr, "%s\n", config_error);
    }

    // --- SDL & TTF Initialization ---
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() == -1) {
        fprintf(stderr, "SDL_ttf could not initialize! TTF_Error: %s\n", TTF_GetError());
        return 1;
    }

    SDL_DisplayMode dm;
    SDL_GetDesktopDisplayMode(0, &dm);
    int SCREEN_WIDTH = dm.w;
    int SCREEN_HEIGHT = dm.h;

    SDL_Window* window = SDL_CreateWindow("News Ticker", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    TTF_Font* font = TTF_OpenFont(config.font_path, config.font_size);
    if (!font) {
        fprintf(stderr, "Failed to load font: %s! TTF_Error: %s\n", config.font_path, TTF_GetError());
        // Try a common system font as a last resort
        #ifdef _WIN32
        font = TTF_OpenFont("C:/Windows/Fonts/Arial.ttf", config.font_size);
        #else
        font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", config.font_size);
        #endif
        if (!font) return 1; // Exit if no font can be loaded
    }

    // --- Data Structures for News ---
    struct NewsLine news_lines[MAX_LINES] = {0};
    int num_headlines = 0;
    bool use_fallback = false;
    int y_cursor = LINE_PADDING;
    char fetch_error[STATUS_BUFFER] = {0};

    // --- Fetch and Parse News ---
    char api_url[512];
    // Use the country_code from the config struct
    snprintf(api_url, sizeof(api_url), "https://newsapi.org/v2/top-headlines?country=%s&pageSize=%d&apiKey=%s", config.country_code, MAX_LINES, config.api_key);

    fprintf(stdout, "Attempting to fetch news from: %s\n", api_url);

    CURL* curl_handle = curl_easy_init();
    if (!curl_handle) {
        snprintf(fetch_error, sizeof(fetch_error), "Unable to initialize network client.");
        use_fallback = true;
    } else {
        curl_easy_setopt(curl_handle, CURLOPT_URL, api_url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "news-ticker/1.0");
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT_MS, 5000L);
        char curl_error[CURL_ERROR_SIZE] = {0};
        curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, curl_error);

        for (int attempt = 0; attempt < MAX_FETCH_ATTEMPTS && num_headlines == 0; ++attempt) {
            struct MemoryStruct chunk = { .memory = malloc(1), .size = 0 };
            curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
            CURLcode res = curl_easy_perform(curl_handle);

            if (res != CURLE_OK) {
                snprintf(fetch_error, sizeof(fetch_error), "Request failed (%s)", curl_error[0] ? curl_error : curl_easy_strerror(res));
            } else {
                cJSON* json = cJSON_Parse(chunk.memory);
                if (json == NULL) {
                    snprintf(fetch_error, sizeof(fetch_error), "NewsAPI returned invalid JSON.");
                } else {
                    cJSON* status = cJSON_GetObjectItemCaseSensitive(json, "status");
                    if (!cJSON_IsString(status) || strcmp(status->valuestring, "ok") != 0) {
                        snprintf(fetch_error, sizeof(fetch_error), "NewsAPI error: status != ok.");
                    } else {
                        cJSON* articles = cJSON_GetObjectItemCaseSensitive(json, "articles");
                        cJSON* article = NULL;
                        cJSON_ArrayForEach(article, articles) {
                            if (!cJSON_IsObject(article)) continue;
                            if (num_headlines >= MAX_LINES) break;
                            cJSON* title_json = cJSON_GetObjectItemCaseSensitive(article, "title");
                            if (!cJSON_IsString(title_json) || !title_json->valuestring) continue;

                            char* sanitized = sanitize_headline(title_json->valuestring);
                            if (!sanitized || sanitized[0] == '\0') {
                                if (sanitized) free(sanitized);
                                continue;
                            }

                            size_t final_len = strlen(sanitized) + 2;
                            char* headline = malloc(final_len);
                            if (!headline) {
                                free(sanitized);
                                snprintf(fetch_error, sizeof(fetch_error), "Out of memory building headline.");
                                break;
                            }
                            snprintf(headline, final_len, "%s ", sanitized);
                            free(sanitized);

                            SDL_Color color = config.colors[rand() % config.num_colors];
                            if (init_news_line(&news_lines[num_headlines], renderer, font, headline, true, color, SCREEN_WIDTH, SCREEN_HEIGHT, &y_cursor)) {
                                num_headlines++;
                            }
                        }
                        if (num_headlines > 0) {
                            fetch_error[0] = '\0';
                        }
                    }
                    cJSON_Delete(json);
                }
            }

            free(chunk.memory);
            if (num_headlines > 0) {
                break;
            }
            if (attempt < MAX_FETCH_ATTEMPTS - 1) {
                SDL_Delay(250 * (attempt + 1));
            }
        }
        curl_easy_cleanup(curl_handle);
        if (num_headlines == 0) {
            use_fallback = true;
        }
    }

    if (use_fallback || num_headlines == 0) {
        fprintf(stderr, "Using fallback headlines.\n");
        for (int i = 0; i < MAX_LINES; ++i) {
            release_news_line(&news_lines[i]);
        }
        num_headlines = 0;
        y_cursor = LINE_PADDING;

        if (!config_ok && config_error[0]) {
            size_t len = strlen(config_error) + 2;
            char* error_line = malloc(len);
            if (error_line) {
                snprintf(error_line, len, "%s", config_error);
                if (init_news_line(&news_lines[num_headlines], renderer, font, error_line, true, (SDL_Color){255, 80, 80, 255}, SCREEN_WIDTH, SCREEN_HEIGHT, &y_cursor)) {
                    num_headlines++;
                }
            }
        }

        if (fetch_error[0]) {
            size_t len = strlen(fetch_error) + 32;
            char* status_line = malloc(len);
            if (status_line) {
                snprintf(status_line, len, "Falling back: %s", fetch_error);
                if (init_news_line(&news_lines[num_headlines], renderer, font, status_line, true, (SDL_Color){255, 160, 0, 255}, SCREEN_WIDTH, SCREEN_HEIGHT, &y_cursor)) {
                    num_headlines++;
                }
            }
        }

        for (int i = 0; fallback_news[i] != NULL && i < MAX_LINES; ++i) {
            if (num_headlines >= MAX_LINES) break;
            char* fallback_copy = NULL;
            size_t len = strlen(fallback_news[i]) + 1;
            fallback_copy = malloc(len);
            if (!fallback_copy) {
                continue;
            }
            snprintf(fallback_copy, len, "%s", fallback_news[i]);
            SDL_Color color = config.colors[rand() % config.num_colors];
            if (init_news_line(&news_lines[num_headlines], renderer, font, fallback_copy, true, color, SCREEN_WIDTH, SCREEN_HEIGHT, &y_cursor)) {
                num_headlines++;
            }
        }
    }

    // --- Main Loop ---
    bool is_running = true;
    bool is_paused = false;
    Uint32 last_ticks = SDL_GetTicks();
    while (is_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) is_running = false;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) is_running = false;
                if (e.key.keysym.sym == SDLK_SPACE) is_paused = !is_paused;
            }
        }

        Uint32 current_ticks = SDL_GetTicks();
        float delta_seconds = (current_ticks - last_ticks) / 1000.0f;
        last_ticks = current_ticks;

        // --- Update ---
        if (!is_paused) {
            for (int i = 0; i < num_headlines; ++i) {
                if (!news_lines[i].texture) continue;
                news_lines[i].scroll_x -= news_lines[i].scroll_speed * delta_seconds;
                if (news_lines[i].scroll_x < -news_lines[i].texture_width) {
                    news_lines[i].scroll_x = SCREEN_WIDTH + (rand() % 500);
                }
            }
        } else {
            last_ticks = current_ticks;
        }

        // --- Drawing ---
        SDL_SetRenderDrawColor(renderer, config.background_color.r, config.background_color.g, config.background_color.b, 255);
        SDL_RenderClear(renderer);

        for (int i = 0; i < num_headlines; ++i) {
            if (!news_lines[i].texture) continue;
            SDL_Rect dstRect = { (int)news_lines[i].scroll_x, news_lines[i].y_position, news_lines[i].texture_width, news_lines[i].texture_height };
            SDL_RenderCopy(renderer, news_lines[i].texture, NULL, &dstRect);
        }

        SDL_RenderPresent(renderer);
    }

    // --- Cleanup ---
    for (int i = 0; i < num_headlines; ++i) {
        release_news_line(&news_lines[i]);
    }
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}

// --- Function Implementations ---

void render_text(SDL_Renderer* renderer, TTF_Font* font, struct NewsLine* line) {
    if (line->texture) {
        SDL_DestroyTexture(line->texture);
    }
    SDL_Surface* surface = TTF_RenderText_Blended(font, line->text, line->color);
    if (surface) {
        line->texture = SDL_CreateTextureFromSurface(renderer, surface);
        line->texture_width = surface->w;
        line->texture_height = surface->h;
        SDL_FreeSurface(surface);
    } else {
        line->texture = NULL;
        line->texture_width = 0;
        line->texture_height = 0;
    }
}

void trim_whitespace(char *str) {
    if (!str) return;
    char *start = str;
    while (isspace((unsigned char)*start)) {
        start++;
    }

    memmove(str, start, strlen(start) + 1);

    char *end = str + strlen(str) - 1;
    while (end >= str && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';
}


bool parse_config(struct Config* config, char *error_message, size_t message_len) {
    if (error_message && message_len > 0) {
        error_message[0] = '\0';
    }

    // Set defaults first
    strcpy(config->api_key, "YOUR_API_KEY");
    strcpy(config->font_path, "font.ttf");
    strcpy(config->country_code, "us"); // Default to US
    config->font_size = 28;
    config->background_color = (SDL_Color){20, 20, 20, 255};
    config->colors[0] = (SDL_Color){255, 165, 0, 255}; // Amber
    config->colors[1] = (SDL_Color){0, 255, 255, 255};   // Cyan
    config->colors[2] = (SDL_Color){255, 255, 0, 255};   // Yellow
    config->colors[3] = (SDL_Color){0, 255, 0, 255};     // Green
    config->colors[4] = (SDL_Color){255, 0, 255, 255};   // Magenta
    config->num_colors = 5;

    bool valid = true;
    FILE* file = fopen("config.ini", "r");
    if (!file) {
        append_message(error_message, message_len, "config.ini missing; using defaults.");
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r' || line[0] == 0) {
            continue;
        }

        char *key = strtok(line, "=");
        char *value = strtok(NULL, "\n");

        if (key && value) {
            trim_whitespace(key);
            trim_whitespace(value);

            if (strcmp(key, "api_key") == 0) strcpy(config->api_key, value);
            else if (strcmp(key, "font_path") == 0) strcpy(config->font_path, value);
            else if (strcmp(key, "font_size") == 0) config->font_size = atoi(value);
            else if (strcmp(key, "country_code") == 0) strncpy(config->country_code, value, sizeof(config->country_code) - 1);
        }
    }
    fclose(file);

    // Normalize country code to lowercase
    for (size_t i = 0; config->country_code[i] != '\0'; ++i) {
        config->country_code[i] = (char)tolower((unsigned char)config->country_code[i]);
    }

    if (strcmp(config->api_key, "YOUR_API_KEY") == 0 || strlen(config->api_key) < 8) {
        append_message(error_message, message_len, "Set a valid api_key in config.ini.");
        valid = false;
    }

    if (config->font_size <= 0) {
        append_message(error_message, message_len, "font_size must be positive; fallback to 28.");
        config->font_size = 28;
        valid = false;
    }

    size_t code_len = strlen(config->country_code);
    if (code_len != 2) {
        append_message(error_message, message_len, "country_code must be a 2-letter ISO code.");
        strcpy(config->country_code, "us");
        valid = false;
    }

    FILE *font_check = fopen(config->font_path, "r");
    if (!font_check) {
        append_message(error_message, message_len, "Configured font not found; attempting system fallback.");
        valid = false;
    } else {
        fclose(font_check);
    }

    fprintf(stdout, "Config loaded: Country='%s', Font='%s', Size=%d\n", config->country_code, config->font_path, config->font_size);

    return valid;
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "not enough memory (realloc returned NULL)\n");
        return 0;
    }
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

char* sanitize_headline(const char *title) {
    if (!title) return NULL;
    size_t len = strlen(title);
    if (len == 0) return NULL;

    char *buffer = malloc(len + 1);
    if (!buffer) return NULL;

    size_t out = 0;
    bool last_was_space = false;
    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)title[i];
        if (c == '\0') break;

        if (c < 0x80) {
            char normalized = normalize_ascii_char(c);
            if (normalized == '\0') {
                ++i;
                continue;
            }
            if (normalized == ' ') {
                if (out == 0 || last_was_space) {
                    ++i;
                    continue;
                }
                buffer[out++] = ' ';
                last_was_space = true;
            } else {
                buffer[out++] = normalized;
                last_was_space = false;
            }
            ++i;
        } else {
            if (out > 0 && !last_was_space) {
                buffer[out++] = ' ';
                last_was_space = true;
            }
            size_t advance = utf8_sequence_length(c);
            if (advance == 0) {
                advance = 1;
            }
            i += advance;
        }
    }

    while (out > 0 && buffer[out - 1] == ' ') {
        --out;
    }

    buffer[out] = '\0';

    return buffer;
}

void release_news_line(struct NewsLine *line) {
    if (!line) return;
    if (line->texture) {
        SDL_DestroyTexture(line->texture);
        line->texture = NULL;
    }
    if (line->owns_text && line->text) {
        free(line->text);
    }
    line->text = NULL;
    line->owns_text = false;
    line->scroll_x = 0.0f;
    line->scroll_speed = 0.0f;
    line->texture_width = 0;
    line->texture_height = 0;
    line->y_position = 0;
}

bool init_news_line(struct NewsLine *line, SDL_Renderer *renderer, TTF_Font *font, char *text, bool owns_text, SDL_Color color, int screen_width, int screen_height, int *y_cursor) {
    if (!line || !renderer || !font || !text || !y_cursor) {
        if (owns_text) free(text);
        return false;
    }

    release_news_line(line);
    line->text = text;
    line->owns_text = owns_text;
    line->color = color;

    render_text(renderer, font, line);
    if (!line->texture || line->texture_height == 0) {
        release_news_line(line);
        return false;
    }

    if (*y_cursor + line->texture_height > screen_height - LINE_PADDING) {
        release_news_line(line);
        return false;
    }

    line->y_position = *y_cursor;
    *y_cursor += line->texture_height + LINE_PADDING;
    float random_factor = rand() / (float)RAND_MAX;
    line->scroll_x = (float)(screen_width + (rand() % 500));
    line->scroll_speed = MIN_SCROLL_SPEED + (MAX_SCROLL_SPEED - MIN_SCROLL_SPEED) * random_factor;

    return true;
}

void append_message(char *buffer, size_t len, const char *message) {
    if (!buffer || len == 0 || !message || message[0] == '\0') {
        return;
    }

    size_t current = strlen(buffer);
    if (current > 0 && current < len - 1) {
        strncat(buffer, " ", len - current - 1);
        current = strlen(buffer);
    }
    strncat(buffer, message, len - current - 1);
}

char normalize_ascii_char(unsigned char c) {
    if (c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == '\f') {
        return ' ';
    }
    if (c == '\0') {
        return '\0';
    }
    if (isalnum(c)) {
        return (char)c;
    }
    switch (c) {
        case ' ':
        case '.':
        case ',':
        case ':':
        case ';':
        case '!':
        case '?':
        case '\'':
        case '"':
        case '-':
        case '_':
        case '/':
        case '&':
        case '(': 
        case ')':
        case '[':
        case ']':
        case '#':
        case '$':
        case '+':
            return (char)c;
        default:
            return '\0';
    }
}

size_t utf8_sequence_length(unsigned char lead_byte) {
    if ((lead_byte & 0x80) == 0) return 1;
    if ((lead_byte & 0xE0) == 0xC0) return 2;
    if ((lead_byte & 0xF0) == 0xE0) return 3;
    if ((lead_byte & 0xF8) == 0xF0) return 4;
    return 0;
}
