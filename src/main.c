/*
 * main.c - a tiny C host that embeds Wren and exposes a bit of SDL3 to it.
 *
 * Flow:
 *   1. C creates an SDL window + renderer.
 *   2. C starts a Wren VM and runs scripts/main.wren.
 *   3. Every frame, C calls `Game.update(dt)` (game logic, dt = seconds since
 *      the last frame) and then `Game.draw()` (rendering only).
 *   4. Inside those, Wren calls back into C through the `foreign` methods
 *      declared in the "sdl" module below (Draw.clear, Input.keyDown, ...).
 *      The module also has one foreign class, Image, which wraps an
 *      SDL_Texture loaded from a PNG file.
 *
 * Scripts can `import "name"` to load scripts/name.wren.
 */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <wren.h>

#define WINDOW_TITLE  "Wren + SDL3"
#define WINDOW_WIDTH  640
#define WINDOW_HEIGHT 480

/* Longest step passed to Game.update. Pausing in a debugger or dragging the
 * window can stall a frame for seconds; without a cap, objects would jump. */
#define MAX_DT 0.1

/* Frame time used to pace the loop when vsync isn't available (~60 FPS). */
#define FALLBACK_FRAME_NS (SDL_NS_PER_SECOND / 60)

/* ------------------------------------------------------------------------- */
/* Host state                                                                */
/* ------------------------------------------------------------------------- */

static SDL_Window *window;
static SDL_Renderer *renderer;
static char *baseDir;      /* "<executable dir>/" */
static char *scriptsDir;   /* "<executable dir>/scripts/" */
static bool running = true;

/* ------------------------------------------------------------------------- */
/* The "sdl" Wren module: source lives here, implementation below.           */
/* ------------------------------------------------------------------------- */

static const char *sdlModuleSource =
    "class App {\n"
    "  foreign static quit()\n"
    "  foreign static width\n"
    "  foreign static height\n"
    "}\n"
    "class Draw {\n"
    "  foreign static clear(r, g, b)\n"
    "  foreign static color(r, g, b)\n"
    "  foreign static text(x, y, string)\n"
    "  foreign static image(image, x, y)\n"
    "  foreign static image(image, x, y, scale)\n"
    "}\n"
    "foreign class Image {\n"
    "  construct load(path) {}\n"
    "  foreign width\n"
    "  foreign height\n"
    "}\n"
    "class Input {\n"
    "  foreign static keyDown(name)\n"
    "}\n";

/*
 * Wren doesn't type-check foreign method arguments, and reading a slot as the
 * wrong type is undefined behavior. These helpers check first and, on a
 * mismatch, turn the call into a normal Wren runtime error such as
 * "Expected a number for 'y', got a string."
 */
static const char *typeName(WrenType type)
{
    switch (type) {
    case WREN_TYPE_BOOL:    return "a bool";
    case WREN_TYPE_NUM:     return "a number";
    case WREN_TYPE_FOREIGN: return "a foreign object";
    case WREN_TYPE_LIST:    return "a list";
    case WREN_TYPE_MAP:     return "a map";
    case WREN_TYPE_NULL:    return "null";
    case WREN_TYPE_STRING:  return "a string";
    default:                return "an object";
    }
}

static bool checkType(WrenVM *vm, int slot, WrenType expected, const char *arg)
{
    WrenType actual = wrenGetSlotType(vm, slot);
    if (actual == expected)
        return true;

    char message[128];
    SDL_snprintf(message, sizeof message, "Expected %s for '%s', got %s.",
                 typeName(expected), arg, typeName(actual));
    wrenSetSlotString(vm, 0, message);
    wrenAbortFiber(vm, 0);
    return false;
}

static bool getNum(WrenVM *vm, int slot, const char *arg, double *out)
{
    if (!checkType(vm, slot, WREN_TYPE_NUM, arg))
        return false;
    *out = wrenGetSlotDouble(vm, slot);
    return true;
}

static bool getString(WrenVM *vm, int slot, const char *arg, const char **out)
{
    if (!checkType(vm, slot, WREN_TYPE_STRING, arg))
        return false;
    *out = wrenGetSlotString(vm, slot);
    return true;
}

/* Reads an (r, g, b) triple from slots 1-3, clamping each to 0-255 (NaN -> 0). */
static bool getColor(WrenVM *vm, Uint8 rgb[3])
{
    static const char *names[3] = { "r", "g", "b" };
    for (int i = 0; i < 3; i++) {
        double c;
        if (!getNum(vm, i + 1, names[i], &c))
            return false;
        rgb[i] = (Uint8)(c > 255 ? 255 : c >= 0 ? c : 0);
    }
    return true;
}

/* App.quit(): stop the game loop after the current frame. */
static void appQuit(WrenVM *vm)
{
    (void)vm;
    running = false;
}

/* App.width / App.height: size of the drawing area in pixels. */
static void appWidth(WrenVM *vm)
{
    int w = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &w, NULL);
    wrenSetSlotDouble(vm, 0, w);
}

static void appHeight(WrenVM *vm)
{
    int h = 0;
    SDL_GetCurrentRenderOutputSize(renderer, NULL, &h);
    wrenSetSlotDouble(vm, 0, h);
}

/* Draw.clear(r, g, b): fill the screen with a color, leaving Draw.color as is. */
static void drawClear(WrenVM *vm)
{
    Uint8 rgb[3], old[4];
    if (!getColor(vm, rgb))
        return;
    SDL_GetRenderDrawColor(renderer, &old[0], &old[1], &old[2], &old[3]);
    SDL_SetRenderDrawColor(renderer, rgb[0], rgb[1], rgb[2], 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, old[0], old[1], old[2], old[3]);
}

/* Draw.color(r, g, b): set the color used by later draw calls (0-255 components). */
static void drawColor(WrenVM *vm)
{
    Uint8 rgb[3];
    if (getColor(vm, rgb))
        SDL_SetRenderDrawColor(renderer, rgb[0], rgb[1], rgb[2], 255);
}

/* Draw.text(x, y, string): draw text with SDL's built-in 8x8 debug font. */
static void drawText(WrenVM *vm)
{
    double x, y;
    const char *text;
    if (getNum(vm, 1, "x", &x) && getNum(vm, 2, "y", &y) && getString(vm, 3, "string", &text))
        SDL_RenderDebugText(renderer, (float)x, (float)y, text);
}

/*
 * Image.load(path): a PNG, with path relative to the executable's directory
 * (e.g. "assets/player.png"). Wren calls this "allocate" function with the
 * constructor's arguments; the Image object's storage is an SDL_Texture *.
 */
static void imageAllocate(WrenVM *vm)
{
    const char *path;
    if (!getString(vm, 1, "path", &path))
        return;

    char *fullPath;
    if (SDL_asprintf(&fullPath, "%s%s", baseDir, path) < 0) {
        wrenSetSlotString(vm, 0, "Out of memory.");
        wrenAbortFiber(vm, 0);
        return;
    }
    SDL_Surface *surface = SDL_LoadPNG(fullPath);
    SDL_free(fullPath);
    SDL_Texture *texture = surface ? SDL_CreateTextureFromSurface(renderer, surface) : NULL;
    SDL_DestroySurface(surface);
    if (!texture) {
        char message[512];
        SDL_snprintf(message, sizeof message, "Could not load image '%s': %s",
                     path, SDL_GetError());
        wrenSetSlotString(vm, 0, message);
        wrenAbortFiber(vm, 0);
        return;
    }
    /* Keep pixel art crisp when scaled up. */
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_PIXELART);

    SDL_Texture **slot = wrenSetSlotNewForeign(vm, 0, 0, sizeof *slot);
    *slot = texture;
}

/* Called when the garbage collector frees an Image (or the VM shuts down). */
static void imageFinalize(void *data)
{
    SDL_DestroyTexture(*(SDL_Texture **)data);
}

/* Reads the texture out of an Image in the given slot. */
static bool getImage(WrenVM *vm, int slot, const char *arg, SDL_Texture **out)
{
    if (!checkType(vm, slot, WREN_TYPE_FOREIGN, arg))
        return false;
    *out = *(SDL_Texture **)wrenGetSlotForeign(vm, slot);
    return true;
}

/* image.width / image.height: size in pixels. */
static void imageWidth(WrenVM *vm)
{
    SDL_Texture *texture = *(SDL_Texture **)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, texture->w);
}

static void imageHeight(WrenVM *vm)
{
    SDL_Texture *texture = *(SDL_Texture **)wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, texture->h);
}

/* Draw.image(image, x, y[, scale]): draw an Image with its top-left at (x, y). */
static void drawImageScaled(WrenVM *vm, double scale)
{
    SDL_Texture *texture;
    double x, y;
    if (!getImage(vm, 1, "image", &texture) || !getNum(vm, 2, "x", &x) || !getNum(vm, 3, "y", &y))
        return;
    SDL_FRect dst = { (float)x, (float)y, (float)(texture->w * scale), (float)(texture->h * scale) };
    SDL_RenderTexture(renderer, texture, NULL, &dst);
}

static void drawImage(WrenVM *vm)
{
    drawImageScaled(vm, 1);
}

static void drawImageScale(WrenVM *vm)
{
    double scale;
    if (getNum(vm, 4, "scale", &scale))
        drawImageScaled(vm, scale);
}

/* Input.keyDown(name): true if the named key ("Space", "A", "Escape", ...) is held. */
static void inputKeyDown(WrenVM *vm)
{
    const char *name;
    if (!getString(vm, 1, "name", &name))
        return;
    SDL_Scancode code = SDL_GetScancodeFromName(name);
    const bool *keys = SDL_GetKeyboardState(NULL);
    wrenSetSlotBool(vm, 0, code != SDL_SCANCODE_UNKNOWN && keys[code]);
}

/* Every foreign method in sdlModuleSource, and the C function behind it.
 * To add one: declare it in sdlModuleSource, write it above, list it here. */
static const struct {
    const char *className;
    const char *signature;   /* Wren signature: "name(_,_)", or "name" for a getter */
    bool isStatic;
    WrenForeignMethodFn fn;
} sdlBindings[] = {
    { "App",   "quit()",         true,  appQuit },
    { "App",   "width",          true,  appWidth },
    { "App",   "height",         true,  appHeight },
    { "Draw",  "clear(_,_,_)",   true,  drawClear },
    { "Draw",  "color(_,_,_)",   true,  drawColor },
    { "Draw",  "text(_,_,_)",    true,  drawText },
    { "Draw",  "image(_,_,_)",   true,  drawImage },
    { "Draw",  "image(_,_,_,_)", true,  drawImageScale },
    { "Image", "width",          false, imageWidth },
    { "Image", "height",         false, imageHeight },
    { "Input", "keyDown(_)",     true,  inputKeyDown },
};

/* Wren asks us for the C function behind each `foreign` method it sees. */
static WrenForeignMethodFn bindForeignMethod(WrenVM *vm, const char *module,
                                             const char *className, bool isStatic,
                                             const char *signature)
{
    (void)vm;
    if (strcmp(module, "sdl") != 0)
        return NULL;

    for (size_t i = 0; i < SDL_arraysize(sdlBindings); i++) {
        if (strcmp(className, sdlBindings[i].className) == 0 &&
            strcmp(signature, sdlBindings[i].signature) == 0 &&
            isStatic == sdlBindings[i].isStatic)
            return sdlBindings[i].fn;
    }
    return NULL;
}

/* Wren asks us how to create and free each `foreign class` it sees. */
static WrenForeignClassMethods bindForeignClass(WrenVM *vm, const char *module,
                                                const char *className)
{
    (void)vm;
    WrenForeignClassMethods methods = {0};
    if (strcmp(module, "sdl") == 0 && strcmp(className, "Image") == 0) {
        methods.allocate = imageAllocate;
        methods.finalize = imageFinalize;
    }
    return methods;
}

/* ------------------------------------------------------------------------- */
/* Loading scripts                                                           */
/* ------------------------------------------------------------------------- */

/* Reads scripts/<name>.wren. Returns NULL on failure; free with SDL_free. */
static char *readScript(const char *name)
{
    char *path;
    if (SDL_asprintf(&path, "%s%s.wren", scriptsDir, name) < 0)
        return NULL;
    char *source = SDL_LoadFile(path, NULL);
    SDL_free(path);
    return source;
}

static void freeModuleSource(WrenVM *vm, const char *name, WrenLoadModuleResult result)
{
    (void)vm;
    (void)name;
    SDL_free((void *)result.source);
}

/*
 * Wren calls this for each `import "name"`. "sdl" is built in; anything else
 * is read from scripts/<name>.wren. If that file doesn't exist, Wren falls back
 * to its own optional modules ("random", "meta") before reporting an error.
 */
static WrenLoadModuleResult loadModule(WrenVM *vm, const char *name)
{
    (void)vm;
    WrenLoadModuleResult result = {0};
    if (strcmp(name, "sdl") == 0) {
        result.source = sdlModuleSource;
    } else {
        result.source = readScript(name);
        result.onComplete = freeModuleSource;
    }
    return result;
}

/* ------------------------------------------------------------------------- */
/* Wren output / errors                                                      */
/* ------------------------------------------------------------------------- */

/* System.print goes to stdout. Flush so it stays in order with errors. */
static void writeFn(WrenVM *vm, const char *text)
{
    (void)vm;
    fputs(text, stdout);
    fflush(stdout);
}

static void errorFn(WrenVM *vm, WrenErrorType type, const char *module,
                    int line, const char *msg)
{
    (void)vm;
    switch (type) {
    case WREN_ERROR_COMPILE:
        SDL_Log("[%s line %d] %s", module, line, msg);
        break;
    case WREN_ERROR_RUNTIME:
        SDL_Log("Runtime error: %s", msg);
        break;
    case WREN_ERROR_STACK_TRACE:
        SDL_Log("  [%s line %d] in %s", module, line, msg);
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* Startup, game loop, cleanup                                               */
/* ------------------------------------------------------------------------- */

static bool initSDL(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    if (!SDL_CreateWindowAndRenderer(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
                                     &window, &renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        return false;
    }
    /* Not every driver supports vsync; runGameLoop paces itself if it's off. */
    SDL_SetRenderVSync(renderer, 1);
    return true;
}

/* Creates the VM and runs scripts/main.wren, which must define a Game class. */
static WrenVM *startWren(void)
{
    const char *basePath = SDL_GetBasePath();
    if (!basePath || !(baseDir = SDL_strdup(basePath)) ||
        SDL_asprintf(&scriptsDir, "%sscripts/", basePath) < 0) {
        SDL_Log("Could not find the executable's directory: %s", SDL_GetError());
        return NULL;
    }
    char *source = readScript("main");
    if (!source) {
        SDL_Log("Could not load main.wren: %s", SDL_GetError());
        return NULL;
    }

    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.writeFn = writeFn;
    config.errorFn = errorFn;
    config.bindForeignMethodFn = bindForeignMethod;
    config.bindForeignClassFn = bindForeignClass;
    config.loadModuleFn = loadModule;
    WrenVM *vm = wrenNewVM(&config);

    WrenInterpretResult result = wrenInterpret(vm, "main", source);
    SDL_free(source);
    if (result == WREN_RESULT_SUCCESS && !wrenHasVariable(vm, "main", "Game")) {
        SDL_Log("scripts/main.wren must define a Game class.");
        result = WREN_RESULT_RUNTIME_ERROR;
    }
    if (result != WREN_RESULT_SUCCESS) {
        wrenFreeVM(vm);
        return NULL;
    }
    return vm;
}

/* Runs until the window closes or App.quit(). Returns false on a script error. */
static bool runGameLoop(WrenVM *vm)
{
    /* Handles let C call `Game.update(dt)` and `Game.draw()` each frame. */
    wrenEnsureSlots(vm, 1);
    wrenGetVariable(vm, "main", "Game", 0);
    WrenHandle *gameClass = wrenGetSlotHandle(vm, 0);
    WrenHandle *updateMethod = wrenMakeCallHandle(vm, "update(_)");
    WrenHandle *drawMethod = wrenMakeCallHandle(vm, "draw()");

    int vsync = 0;
    SDL_GetRenderVSync(renderer, &vsync);

    bool ok = true;
    Uint64 lastTicks = SDL_GetTicksNS();
    while (running) {
        Uint64 frameStart = SDL_GetTicksNS();

        /* 1. Input: drain SDL's event queue. Key state is read by Input.keyDown. */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
        }

        /* 2. Update: seconds elapsed since the previous frame, capped at MAX_DT. */
        double dt = (double)(frameStart - lastTicks) / SDL_NS_PER_SECOND;
        lastTicks = frameStart;
        if (dt > MAX_DT)
            dt = MAX_DT;

        wrenEnsureSlots(vm, 2);
        wrenSetSlotHandle(vm, 0, gameClass);  /* slot 0 = receiver */
        wrenSetSlotDouble(vm, 1, dt);         /* slot 1 = first argument */
        if (wrenCall(vm, updateMethod) != WREN_RESULT_SUCCESS) {
            ok = false;
            break;
        }

        /* 3. Draw. */
        wrenEnsureSlots(vm, 1);
        wrenSetSlotHandle(vm, 0, gameClass);
        if (wrenCall(vm, drawMethod) != WREN_RESULT_SUCCESS) {
            ok = false;
            break;
        }

        /* 4. Present: show the finished frame. With vsync this waits for the
         *    display; without it, sleep off the rest of the frame instead. */
        SDL_RenderPresent(renderer);
        if (!vsync) {
            Uint64 elapsed = SDL_GetTicksNS() - frameStart;
            if (elapsed < FALLBACK_FRAME_NS)
                SDL_DelayNS(FALLBACK_FRAME_NS - elapsed);
        }
    }

    wrenReleaseHandle(vm, drawMethod);
    wrenReleaseHandle(vm, updateMethod);
    wrenReleaseHandle(vm, gameClass);
    return ok;
}

/* Frees everything, in reverse order of creation. Safe after a partial startup. */
static void cleanup(WrenVM *vm)
{
    if (vm)
        wrenFreeVM(vm);
    SDL_free(scriptsDir);
    SDL_free(baseDir);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    WrenVM *vm = NULL;
    bool ok = initSDL() && (vm = startWren()) != NULL && runGameLoop(vm);
    cleanup(vm);
    return ok ? 0 : 1;
}
