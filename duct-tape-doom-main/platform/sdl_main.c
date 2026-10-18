#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "d_main.h"
#include "m_argv.h"
#include "i_compat.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static char *selected_wad;
static boolean selection_done;
#ifdef __APPLE__
extern int DOOM_ChooseWads(const char *bundled, char **base, char **addon);
#endif
static void selected(void *userdata, const char * const *files, int filter)
{
    (void)userdata; (void)filter;
    if (files && files[0]) selected_wad = SDL_strdup(files[0]);
    selection_done = true;
}

/* Many releases ship their DEHACKED patch beside the WAD: map.wad + map.deh. */
static char *companion_patch(const char *wad)
{
    static const char *extensions[] = { ".deh", ".DEH", ".bex", ".BEX" };
    size_t length = strlen(wad);
    const char *dot = strrchr(wad, '.');
    const char *slash = strrchr(wad, '/');
    if (dot && (!slash || dot > slash)) length = (size_t)(dot - wad);
    char *path = malloc(length + 5);
    if (!path) return NULL;
    for (int i = 0; i < 4; ++i)
    {
        memcpy(path, wad, length);
        strcpy(path + length, extensions[i]);
        FILE *file = fopen(path, "rb");
        if (file) { fclose(file); return path; }
    }
    free(path);
    return NULL;
}

#ifndef __APPLE__
#include "wad_probe.h"

#define MAX_CHOICES 8

static int compare_paths(const void *a, const void *b)
{
    return SDL_strcasecmp(*(char * const *)a, *(char * const *)b);
}

static const char *file_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\') name = p + 1;
    return name;
}

static void notice(const char *text)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "DOOM", text, NULL);
}

/* Returns an index into paths, -1 for the extra choice, or -2 to quit. */
static int choose(const char *message, char **paths, int count, const char *extra)
{
    SDL_MessageBoxButtonData buttons[MAX_CHOICES + 2];
    int shown = 0, result = -2;
    for (int i = 0; i < count; ++i)
        buttons[shown++] = (SDL_MessageBoxButtonData){ i ? 0 : SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, i, file_name(paths[i]) };
    if (extra) buttons[shown++] = (SDL_MessageBoxButtonData){ 0, -1, extra };
    buttons[shown++] = (SDL_MessageBoxButtonData){ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, -2, "Quit" };
    SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT,
                               NULL, "DOOM", message, shown, buttons, NULL };
    if (!SDL_ShowMessageBox(&box, &result)) return -2;
    return result;
}

/* Windows and Linux: play the WADs dropped into the "wads" folder beside the
 * program, following the same rules as the Mac launcher. Returns 1 when a game
 * was chosen, 0 when the folder holds no WADs, and -1 to quit. */
static int choose_from_folder(const char *base, const char *bundled, char **game, char **addon)
{
    char folder[4096];
    snprintf(folder, sizeof(folder), "%swads", base);
    int count = 0;
    char **names = SDL_GlobDirectory(folder, "*.wad", SDL_GLOB_CASEINSENSITIVE, &count);
    if (!names) return 0;
    char *bases[MAX_CHOICES], *addons[MAX_CHOICES];
    int base_count = 0, addon_count = 0, invalid = 0, other_game = 0;
    for (int i = 0; i < count; ++i)
    {
        char *path = NULL;
        doom_wad_info info;
        if (!SDL_asprintf(&path, "%s/%s", folder, names[i])) continue;
        if (!DOOM_ProbeWad(path, &info)) invalid = 1;
        else if (info.is_iwad && !info.has_palette) other_game = 1;
        else if (info.is_iwad && (info.episode_maps || info.numbered_maps) && base_count < MAX_CHOICES)
        { bases[base_count++] = path; continue; }
        else if (!info.is_iwad && addon_count < MAX_CHOICES)
        { addons[addon_count++] = path; continue; }
        SDL_free(path);
    }
    SDL_free(names);
    qsort(bases, (size_t)base_count, sizeof(*bases), compare_paths);
    qsort(addons, (size_t)addon_count, sizeof(*addons), compare_paths);

    int result = -1;
    const char *chosen_game = NULL, *chosen_addon = NULL;
    FILE *file = fopen(bundled, "rb");
    int has_bundled = file != NULL;
    if (file) fclose(file);
    if (!base_count && !addon_count)
    {
        if (other_game) notice("The wads folder holds a game WAD in another format, such as DOOM 64. Only DOOM, DOOM II, Final DOOM and Freedoom game WADs can be played.");
        else if (invalid) notice("The WAD files in the wads folder are damaged or unsupported.");
        result = invalid || other_game ? -1 : 0;
        goto done;
    }
    if (!base_count)
    {
        if (!has_bundled)
        {
            notice("Custom maps need a base game. Put your DOOM or DOOM II game WAD into the wads folder too.");
            goto done;
        }
        chosen_game = bundled;
    }
    else if (base_count == 1 && !has_bundled) chosen_game = bases[0];
    else
    {
        int pick = choose("Choose the game to play.", bases, base_count, has_bundled ? "Freedoom" : NULL);
        if (pick == -2) goto done;
        chosen_game = pick == -1 ? bundled : bases[pick];
    }
    if (addon_count == 1) chosen_addon = addons[0];
    else if (addon_count > 1)
    {
        int pick = choose("Choose a custom WAD.", addons, addon_count, "Base game only");
        if (pick == -2) goto done;
        if (pick >= 0) chosen_addon = addons[pick];
    }
    if (chosen_addon)
    {
        doom_wad_info game_info, addon_info;
        DOOM_ProbeWad(chosen_game, &game_info);
        DOOM_ProbeWad(chosen_addon, &addon_info);
        if ((addon_info.numbered_maps && !game_info.numbered_maps) ||
            (addon_info.episode_maps && !game_info.episode_maps))
        {
            notice(addon_info.numbered_maps
                ? "This map uses DOOM II levels. Put your DOOM II WAD (or Freedoom Phase 2) into the wads folder."
                : "This map uses DOOM episodes. Put your DOOM WAD (or Freedoom Phase 1) into the wads folder.");
            goto done;
        }
        if (!game_info.numbered_maps && !game_info.full_game)
        {
            notice("The shareware base game cannot load custom WADs. Use the full game or Freedoom instead.");
            goto done;
        }
        *addon = SDL_strdup(chosen_addon);
    }
    *game = SDL_strdup(chosen_game);
    result = 1;
done:
    for (int i = 0; i < base_count; ++i) SDL_free(bases[i]);
    for (int i = 0; i < addon_count; ++i) SDL_free(addons[i]);
    return result;
}
#endif

int main(int argc, char **argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
        return 1;
    }
    const char *base = SDL_GetBasePath();
    char bundled[4096];
    snprintf(bundled, sizeof(bundled), "%sdefault.wad", base ? base : "./");
    int has_iwad = 0;
    char *selected_addon = NULL;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "-iwad")) has_iwad = 1;
#ifdef __APPLE__
    if (argc == 1)
    {
        if (!DOOM_ChooseWads(bundled, &selected_wad, &selected_addon))
        { SDL_Quit(); return 0; }
    }
#else
    if (!has_iwad && choose_from_folder(base ? base : "./", bundled, &selected_wad, &selected_addon) < 0)
    { SDL_Quit(); return 0; }
#endif
    if (!has_iwad && !selected_wad)
    {
        FILE *file = fopen(bundled, "rb");
        if (file) { fclose(file); selected_wad = SDL_strdup(bundled); }
        else
        {
            SDL_DialogFileFilter filter = { "DOOM game data", "wad" };
            SDL_ShowOpenFileDialog(selected, NULL, NULL, &filter, 1, NULL, false);
            while (!selection_done) { SDL_PumpEvents(); SDL_Delay(10); }
            if (!selected_wad) { SDL_Quit(); return 0; }
        }
    }
    char *preferences = SDL_GetPrefPath("Local Games", "DOOM");
    if (!preferences)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DOOM", "Could not create the game save folder.", NULL);
        SDL_Quit(); return 1;
    }
#ifdef _WIN32
    if (_chdir(preferences))
#else
    if (chdir(preferences))
#endif
    { SDL_free(preferences); SDL_Quit(); return 1; }
    SDL_free(preferences);
    if (argc == 1)
    {
        FILE *log = fopen("game.log", "w");
        if (log) fclose(log);
        freopen("game.log", "a", stdout);
        freopen("game.log", "a", stderr);
    }
    FILE *file = fopen("doom.cfg", "r");
    if (file) fclose(file);
    else
    {
        file = fopen("doom.cfg", "w");
        if (file)
        {
            fputs("key_up 119\nkey_down 115\nkey_strafeleft 97\nkey_straferight 100\n"
                  "key_use 32\nkey_fire 157\nmouse_sensitivity 5\n"
                  "sfx_volume 10\nmusic_volume 7\nsnd_channels 16\n", file);
            fclose(file);
        }
    }
    char **args = calloc((size_t)argc + 10, sizeof(char*));
    if (!args) { SDL_Quit(); return 1; }
    int count = 0;
    for (int i = 0; i < argc; ++i) args[count++] = argv[i];
    if (!has_iwad) { args[count++] = "-iwad"; args[count++] = selected_wad; }
    if (selected_addon)
    {
        char *patch = companion_patch(selected_addon);
        args[count++] = "-file"; args[count++] = selected_addon;
        if (patch) { args[count++] = "-deh"; args[count++] = patch; }
    }
    int has_config = 0;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "-config")) has_config = 1;
    if (!has_config) { args[count++] = "-config"; args[count++] = "doom.cfg"; }
    myargc = count;
    myargv = args;
    D_DoomMain();
    return 0;
}
