#include <stdio.h>
#include <string.h>
#include "d_main.h"
#include "m_argv.h"

int main(int argc, char **argv)
{
    myargc = argc;
    myargv = argv;
    if (M_CheckParm("--help") || M_CheckParm("-h"))
    {
        puts("DOOM 1.10 portable headless runner\n"
             "Usage: doom-headless -playdemo <demo> [-config <file>]\n"
             "Set DOOMWADDIR to a directory containing your original game WAD.\n"
             "This verification backend has no graphics, sound, or interactive input.");
        return 0;
    }
    int demo = M_CheckParm("-playdemo");
    if (!demo || demo + 1 >= argc || argv[demo + 1][0] == '-')
    {
        fputs("A -playdemo <demo> argument is required. Use --help for details.\n", stderr);
        return 1;
    }
    if (M_CheckParm("-timedemo"))
    {
        fputs("Use -playdemo; the historical timedemo path terminates as an error.\n", stderr);
        return 1;
    }
    D_DoomMain();
    return 0;
}
