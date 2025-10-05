#include "../headers/logging.h"

static std::ofstream gLog;

void open_log()
{
    gLog.open("maze_runner.log", std::ios::out | std::ios::trunc);
    if (!gLog)
    {
        fprintf(stderr, "Failed to open maze_runner.log\n");
    }
    else
    {
        time_t t = time(nullptr);
        gLog << "Maze Runner log started " << ctime(&t) << "\n";
        gLog.flush();
    }
}

void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    if (gLog)
    {
        char buf[4096];
        va_list ap2;
        va_copy(ap2, ap);
        vsnprintf(buf, sizeof(buf), fmt, ap2);
        va_end(ap2);
        gLog << buf << "\n";
        gLog.flush();
    }
    va_end(ap);
}

[[noreturn]] void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[8192];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "FATAL: %s\n", buf);
    if (gLog)
    {
        gLog << "FATAL: " << buf << "\n";
        gLog.flush();
    }
#ifdef _WIN32
    MessageBoxA(nullptr, buf, "Maze Runner fatal error", MB_OK | MB_ICONERROR);
    ShellExecuteA(nullptr, "open", "notepad.exe", "maze_runner.log", nullptr, SW_SHOWNORMAL);
#endif
    std::exit(1);
}