#ifndef APPOPTS_H
#define APPOPTS_H

#include "CLI/CLI.hpp"

#include <cstdint>
#include <string>
#include <fstream>

struct AppOpts
{

    /**
     * @brief Whether to log verbosely. Applies to both stdout and logfile logs.
     */
    bool verbose{false};

    /**
     * @brief Path to a file where logs will be written. If empty, logs will be
     * written to stdout only.
     */
    std::string logfile_path;

    /**
     * @brief A stream to the log file, if one was specified.
     */
    std::ofstream logfile_stream;

    /**
     * @brief Whether the LSP should communicate over stdin/stdout. When false,
     * the LSP communicates over an HTTP server.
     */
    bool use_stdin{false};

    /**
     * @brief The port to listen on for incoming connections when using HTTP.
     * Not used when using stdin.
     */
    uint16_t port{61313};

    bool use_logfile() const noexcept
    {
        return !logfile_path.empty();
    };

    static AppOpts from_args(int argc, const char *const argv[], const std::string app_name)
    {

        // AppOpts* opts = new AppOpts;
        AppOpts opts;
        CLI::App app{app_name};

        auto stdin_option = app.add_flag("--stdin", opts.use_stdin, "Don't launch an HTTP server and instead accept input on stdin");
        app.add_flag("-v,--verbose", opts.verbose, "Enable verbose logging");
        app.add_option("-l,--log", opts.logfile_path, "Log file");
        app.add_option("-p,--port", opts.port, "Port", true)->excludes(stdin_option);

        try
        {
            app.parse(argc, argv);
        }
        catch (const CLI::ParseError &e)
        {
            exit(app.exit(e));
        }

        if (opts.use_logfile())
        {
            opts.logfile_stream.open(opts.logfile_path);
        }

        return opts;
    }

    ~AppOpts()
    {
        if (logfile_stream.is_open())
        {
            logfile_stream.close();
        }
    }
};
#endif
