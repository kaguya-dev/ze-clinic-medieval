// Ze Clinic if He Borned in a Mediavel Era - a plague doctor tower-defence brawler.
// Uses SDL3's callback entry points so the same code runs on desktop, Android and the web.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Audio.hpp"
#include "Game.hpp"

struct App {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    Game game;
    Uint64 last = 0;
    const char* autotest = nullptr;
    int frame = 0;
};

SDL_AppResult SDL_AppInit(void** state, int, char**) {
    SDL_SetAppMetadata("Ze Clinic if He Borned in a Mediavel Era", "1.0", "com.plagueworks.zeclinic");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    auto* app = new App();
    *state = app;
    if (!SDL_CreateWindowAndRenderer("Ze Clinic if He Borned in a Mediavel Era", VW * 3, VH * 3, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &app->win, &app->ren)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetWindowMinimumSize(app->win, VW, VH);
    SDL_SetRenderVSync(app->ren, 1);
    audio::init(); // the game still runs silently if audio is unavailable
    if (!app->game.init(app->win, app->ren)) return SDL_APP_FAILURE;
    app->last = SDL_GetTicksNS();
    app->autotest = SDL_getenv("ZECLINIC_AUTOTEST");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* e) {
    auto* app = static_cast<App*>(state);
    if (e->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    SDL_ConvertEventToRenderCoordinates(app->ren, e);
    app->game.event(*e);
    return app->game.quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* state) {
    auto* app = static_cast<App*>(state);
    Uint64 now = SDL_GetTicksNS();
    float dt = float(double(now - app->last) / 1e9);
    app->last = now;
    if (dt > 0.05f) dt = 0.05f;
    if (app->autotest) {
        dt = 1.f / 60.f;
        app->game.autotest(app->frame++, app->autotest);
    }
    app->game.update(dt);
    app->game.render();
    return app->game.quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* state, SDL_AppResult) {
    auto* app = static_cast<App*>(state);
    if (!app) return;
    if (app->ren) app->game.shutdown();
    audio::shutdown();
    if (app->ren) SDL_DestroyRenderer(app->ren);
    if (app->win) SDL_DestroyWindow(app->win);
    delete app;
}
