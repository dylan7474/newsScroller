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
    int scroll_x;
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
void parse_config(struct Config* config);
void render_text(SDL_Renderer* renderer, TTF_Font* font, struct NewsLine* line);
void trim_whitespace(char *str);


// --- Main Function ---
int main(int argc, char* argv[]) {
    srand(time(NULL));

    // --- Load Configuration ---
    struct Config config;
    parse_config(&config);

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

    // --- Fetch and Parse News ---
    char api_url[512];
    // Use the country_code from the config struct
    snprintf(api_url, sizeof(api_url), "https://newsapi.org/v2/top-headlines?country=%s&pageSize=%d&apiKey=%s", config.country_code, MAX_LINES, config.api_key);
    
    fprintf(stdout, "Attempting to fetch news from: %s\n", api_url);

    struct MemoryStruct chunk = { .memory = malloc(1), .size = 0 };
    CURL* curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, api_url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "news-ticker/1.0");
    CURLcode res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        use_fallback = true;
    } else {
        cJSON* json = cJSON_Parse(chunk.memory);
        if (json == NULL || cJSON_GetObjectItemCaseSensitive(json, "status") == NULL || strcmp(cJSON_GetObjectItemCaseSensitive(json, "status")->valuestring, "ok") != 0) {
            fprintf(stderr, "API returned an error or JSON is invalid.\n");
            use_fallback = true;
        } else {
            cJSON* articles = cJSON_GetObjectItemCaseSensitive(json, "articles");
            cJSON* article = NULL;
            cJSON_ArrayForEach(article, articles) {
                if (num_headlines >= MAX_LINES) break;
                cJSON* title_json = cJSON_GetObjectItemCaseSensitive(article, "title");
                if (cJSON_IsString(title_json) && (title_json->valuestring != NULL)) {
                    char* title_str = title_json->valuestring;
                    news_lines[num_headlines].text = malloc(strlen(title_str) + 2);
                    sprintf(news_lines[num_headlines].text, "%s ", title_str); // Add space for looping
                    
                    news_lines[num_headlines].color = config.colors[rand() % config.num_colors];
                    render_text(renderer, font, &news_lines[num_headlines]);

                    news_lines[num_headlines].scroll_x = SCREEN_WIDTH + (rand() % 500);
                    news_lines[num_headlines].scroll_speed = 1.5f + (rand() / (float)RAND_MAX) * 2.5f;
                    news_lines[num_headlines].y_position = (num_headlines * (news_lines[num_headlines].texture_height + LINE_PADDING)) + LINE_PADDING;
                    
                    num_headlines++;
                }
            }
        }
        cJSON_Delete(json);
    }
    free(chunk.memory);
    curl_easy_cleanup(curl_handle);

    if (use_fallback || num_headlines == 0) {
        fprintf(stderr, "Using fallback headlines.\n");
        for (int i = 0; fallback_news[i] != NULL && i < MAX_LINES; ++i) {
            news_lines[i].text = (char*)fallback_news[i];
            news_lines[i].color = config.colors[rand() % config.num_colors];
            render_text(renderer, font, &news_lines[i]);
            news_lines[i].scroll_x = SCREEN_WIDTH + (rand() % 500);
            news_lines[i].scroll_speed = 1.5f + (rand() / (float)RAND_MAX) * 2.5f;
            news_lines[i].y_position = (i * (news_lines[i].texture_height + LINE_PADDING)) + LINE_PADDING;
            num_headlines++;
        }
    }

    // --- Main Loop ---
    bool is_running = true;
    bool is_paused = false;
    while (is_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) is_running = false;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) is_running = false;
                if (e.key.keysym.sym == SDLK_SPACE) is_paused = !is_paused;
            }
        }

        // --- Update ---
        if (!is_paused) {
            for (int i = 0; i < num_headlines; ++i) {
                news_lines[i].scroll_x -= news_lines[i].scroll_speed;
                if (news_lines[i].scroll_x < -news_lines[i].texture_width) {
                    news_lines[i].scroll_x = SCREEN_WIDTH;
                }
            }
        }

        // --- Drawing ---
        SDL_SetRenderDrawColor(renderer, config.background_color.r, config.background_color.g, config.background_color.b, 255);
        SDL_RenderClear(renderer);

        for (int i = 0; i < num_headlines; ++i) {
            SDL_Rect dstRect = { news_lines[i].scroll_x, news_lines[i].y_position, news_lines[i].texture_width, news_lines[i].texture_height };
            SDL_RenderCopy(renderer, news_lines[i].texture, NULL, &dstRect);
        }

        SDL_RenderPresent(renderer);
    }

    // --- Cleanup ---
    for (int i = 0; i < num_headlines; ++i) {
        if(news_lines[i].texture) SDL_DestroyTexture(news_lines[i].texture);
        if (!use_fallback) free(news_lines[i].text);
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


void parse_config(struct Config* config) {
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

    FILE* file = fopen("config.ini", "r");
    if (!file) {
        fprintf(stderr, "Could not open config.ini. Using default values.\n");
        return;
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
            if (strcmp(key, "font_path") == 0) strcpy(config->font_path, value);
            if (strcmp(key, "font_size") == 0) config->font_size = atoi(value);
            if (strcmp(key, "country_code") == 0) strncpy(config->country_code, value, sizeof(config->country_code) - 1);
        }
    }
    fclose(file);

    // Add a debug print to confirm loaded settings
    fprintf(stdout, "Config loaded: Country='%s', Font='%s', Size=%d, API_Key='%s'\n", config->country_code, config->font_path, config->font_size, config->api_key);
    if (strcmp(config->api_key, "YOUR_API_KEY") == 0) {
        fprintf(stderr, "WARNING: Default API key is being used. Please edit config.ini\n");
    }
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
