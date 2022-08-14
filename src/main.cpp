#include "CLI/CLI.hpp"

#include "fmt/format.h"
#include "fmt/ostream.h"

#include "nlohmann/json.hpp"

#include "mongoose.h"

#include "ResourceLimits.h"
#include "ShaderLang.h"

#include <cstdint>
#include <experimental/filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "messagebuffer.hpp"
#include "workspace.hpp"
#include "utils.hpp"

#include "lib/appopts.hpp"
#include "lib/appstate.hpp"

using json = nlohmann::json;
namespace fs = std::experimental::filesystem;

struct UserData {
    AppState& state;
    AppOpts& opts;
};

std::string make_response(const json& response)
{
    json content = response;
    content["jsonrpc"] = "2.0";

    std::string header;
    header.append("Content-Length: " + std::to_string(content.dump(4).size()) + "\r\n");
    header.append("Content-Type: application/vscode-jsonrpc;charset=utf-8\r\n");
    header.append("\r\n");
    return header + content.dump(4);
}

EShLanguage find_language(const std::string& name)
{
    auto ext = fs::path(name).extension();
    if (ext == ".vert" || ext == ".vs")
        return EShLangVertex;
    else if (ext == ".tesc")
        return EShLangTessControl;
    else if (ext == ".tese")
        return EShLangTessEvaluation;
    else if (ext == ".geom" || ext == ".gs")
        return EShLangGeometry;
    else if (ext == ".frag" || ext == ".fs")
        return EShLangFragment;
    else if (ext == ".comp")
        return EShLangCompute;
    throw std::invalid_argument("Unknown file extension!");
}

json get_diagnostics(std::string uri, std::string content,
        AppState& appstate)
{
    FILE fp_old = *stdout;
    *stdout = *fopen("/dev/null","w");
    auto document = uri;
    auto shader_cstring = content.c_str();
    auto lang = find_language(document);
    glslang::InitializeProcess();
    glslang::TShader shader(lang);
    shader.setStrings(&shader_cstring, 1);
    TBuiltInResource Resources = glslang::DefaultTBuiltInResource;
    EShMessages messages = EShMsgCascadingErrors;
    shader.parse(&Resources, 110, false, messages);
    std::string debug_log = shader.getInfoLog();
    glslang::FinalizeProcess();
    *stdout = fp_old;

    if (appstate.use_logfile && appstate.verbose) {
        fmt::print(appstate.logfile_stream, "Diagnostics raw output: {}\n" , debug_log);
    }

    std::regex re("(.*): 0:(\\d*): (.*)");
    std::smatch matches;
    auto error_lines = split_string(debug_log, "\n");
    auto content_lines = split_string(content, "\n");

    json diagnostics;
    for (auto error_line : error_lines) {
        std::regex_search(error_line, matches, re);
        if (matches.size() == 4) {
            json diagnostic;
            std::string severity = matches[1];
            int severity_no = -1;
            if (severity == "ERROR") {
                severity_no = 1;
            } else if (severity == "WARNING") {
                severity_no = 2;
            }
            if (severity_no == -1) {
                if (appstate.use_logfile) {
                    fmt::print(appstate.logfile_stream, "Error: Unknown severity '{}'\n", severity);
                }
            }
            std::string message = trim(matches[3], " ");

            // -1 because lines are 0-indexed as per LSP specification.
            int line_no = std::stoi(matches[2]) - 1;
            std::string source_line = content_lines[line_no];

            int start_char = -1;
            int end_char = -1;

            // If this is an undeclared identifier, we can find the exact
            // position of the broken identifier.
            std::smatch message_matches;
            std::regex re("'(.*)' : (.*)");
            std::regex_search(message, message_matches, re);
            if (message_matches.size() == 3) {
                std::string identifier = message_matches[1];
                int identifier_length = message_matches[1].length();
                auto source_pos = source_line.find(identifier);
                start_char = source_pos;
                end_char = source_pos + identifier_length - 1;
            } else {
                // If we can't find a precise position, we'll just use the whole line.
                start_char = 0;
                end_char = source_line.length();
            }

            json range{
                {"start", {
                    { "line", line_no },
                    { "character", start_char },
                }},
                { "end", {
                    { "line", line_no },
                    { "character", end_char },
                }},
            };
            diagnostic["range"] = range;
            diagnostic["severity"] = severity_no;
            diagnostic["source"] = "glslang";
            diagnostic["message"] = message;
            diagnostics.push_back(diagnostic);
        }
    }
    if (appstate.use_logfile && appstate.verbose && !diagnostics.empty()) {
        fmt::print(appstate.logfile_stream, "Sending diagnostics: {}\n" , diagnostics);
    }
    appstate.logfile_stream.flush();
    return diagnostics;
}

std::optional<std::string> handle_message(const MessageBuffer& message_buffer, AppState& appstate, AppOpts& opts)
{
    json body = message_buffer.body();

    if (body["method"] == "initialized") {
        return std::nullopt;
    }

    if (body["method"] == "initialize") {
        appstate.workspace.set_initialized(true);

        json text_document_sync{
            { "openClose", true },
            { "change", 1 }, // Full sync
            { "willSave", false },
            { "willSaveWaitUntil", false },
            { "save", { { "includeText", false } } },
        };

        json completion_provider{
            { "resolveProvider", false },
            { "triggerCharacters", {} },
        };
        json signature_help_provider{
            { "triggerCharacters", "" }
        };
        json code_lens_provider{
            { "resolveProvider", false }
        };
        json document_on_type_formatting_provider{
            { "firstTriggerCharacter", "" },
            { "moreTriggerCharacter", "" },
        };
        json document_link_provider{
            { "resolveProvider", false }
        };
        json execute_command_provider{
            { "commands", {} }
        };
        json result{
            {
                "capabilities",
                {
                { "textDocumentSync", text_document_sync },
                { "hoverProvider", false },
                { "completionProvider", completion_provider },
                { "signatureHelpProvider", signature_help_provider },
                { "definitionProvider", false },
                { "referencesProvider", false },
                { "documentHighlightProvider", false },
                { "documentSymbolProvider", false },
                { "workspaceSymbolProvider", false },
                { "codeActionProvider", false },
                { "codeLensProvider", code_lens_provider },
                { "documentFormattingProvider", false },
                { "documentRangeFormattingProvider", false },
                { "documentOnTypeFormattingProvider", document_on_type_formatting_provider },
                { "renameProvider", false },
                { "documentLinkProvider", document_link_provider },
                { "executeCommandProvider", execute_command_provider },
                { "experimental", {} }, }
            }
        };

        json result_body{
            { "id", body["id"] },
            { "result", result }
        };
        return make_response(result_body);
    } else if (body["method"] == "textDocument/didOpen") {
        auto uri = body["params"]["textDocument"]["uri"];
        auto text = body["params"]["textDocument"]["text"];
        appstate.workspace.add_document(uri, text);

        json diagnostics = get_diagnostics(uri, text, appstate);
        if (diagnostics.empty()) {
            diagnostics = json::array();
        }
        json result_body{
            { "method", "textDocument/publishDiagnostics" },
            { "params", {
                            { "uri", uri },
                            { "diagnostics", diagnostics },
                        } }
        };
        return make_response(result_body);
    } else if (body["method"] == "textDocument/didChange") {
        auto uri = body["params"]["textDocument"]["uri"];
        auto change = body["params"]["contentChanges"][0]["text"];
        appstate.workspace.change_document(uri, change);

        std::string document = appstate.workspace.documents()[uri];
        json diagnostics = get_diagnostics(uri, document, appstate);
        if (diagnostics.empty()) {
            diagnostics = json::array();
        }
        json result_body{
            { "method", "textDocument/publishDiagnostics" },
            { "params", {
                            { "uri", uri },
                            { "diagnostics", diagnostics },
                        } }
        };
        return make_response(result_body);
    }

    // If the workspace has not yet been initialized but the client sends a
    // message that doesn't have method "initialize" then we'll return an error
    // as per LSP spec.
    if (body["method"] != "initialize" && !appstate.workspace.is_initialized()) {
        json error{
            { "code", -32002 },
            { "message", "Server not yet initialized." },
        };
        json result_body{
            { "error", error }
        };
        return make_response(result_body);
    }

    // If we don't know the method requested, we end up here.
    if (body.count("method") == 1) {
        json error{
            { "code", -32601 },
            { "message", fmt::format("Method '{}' not supported.", body["method"].get<std::string>()) },
        };
        json result_body{
            { "error", error }
        };
        return make_response(result_body);
    }

    // If we couldn't parse anything we end up here.
    json error{
        { "code", -32700 },
        { "message", "Couldn't parse message." },
    };
    json result_body{
        { "error", error }
    };
    return make_response(result_body);
}

void ev_handler(struct mg_connection* c, int ev, void* p) {
    UserData& data = *static_cast<UserData*>(c->mgr->user_data);
    AppState& state = data.state;
    AppOpts& opts = data.opts;

    if (ev == MG_EV_HTTP_REQUEST) {
        struct http_message* hm = (struct http_message*)p;

        std::string content = hm->message.p;

        MessageBuffer message_buffer;
        message_buffer.handle_string(content);

        if (message_buffer.message_completed()) {
            json body = message_buffer.body();
            if (opts.use_logfile()) {
                fmt::print(opts.logfile_stream, ">>> Received message of type '{}'\n", body["method"].get<std::string>());
                if (opts.verbose) {
                    fmt::print(opts.logfile_stream, "Headers:\n");
                    for (auto elem : message_buffer.headers()) {
                        auto pretty_header = fmt::format("{}: {}\n", elem.first, elem.second);
                        opts.logfile_stream << pretty_header;
                    }
                    fmt::print(opts.logfile_stream, "Body: \n{}\n\n", body.dump(4));
                    fmt::print(opts.logfile_stream, "Raw: \n{}\n\n", message_buffer.raw());
                }
            }

            auto message = handle_message(message_buffer, state, opts);
            if (message.has_value()) {
                std::string response = message.value();
                mg_send_head(c, 200, response.length(), "Content-Type: text/plain");
                mg_printf(c, "%.*s", static_cast<int>(response.length()), response.c_str());
                if (opts.use_logfile() && opts.verbose) {
                    fmt::print(opts.logfile_stream, "<<< Sending message: \n{}\n\n", message.value());
                }
            }
            opts.logfile_stream.flush();
            message_buffer.clear();
        }
    }
}

int main(int argc, char* argv[])
{
    AppOpts opts = AppOpts::from_args(argc, argv, "GLSL Language Server");
    AppState state;


    if (!opts.use_stdin) {
        // Create a web server to listen for HTTP messages
        struct mg_mgr mgr;
        struct mg_connection* nc;
        struct mg_bind_opts bind_opts;
        UserData user_data{ state, opts };
        std::memset(&bind_opts, 0, sizeof(bind_opts));
        bind_opts.user_data = &user_data;

        mg_mgr_init(&mgr, NULL);
        fmt::print("Starting web server on port {}\n", opts.port);
        nc = mg_bind_opt(&mgr, std::to_string(opts.port).c_str(), ev_handler, bind_opts);
        if (nc == NULL) {
            return 1;
        }

        // Set up HTTP server parameters
        mg_set_protocol_http_websocket(nc);

        while (true) {
            mg_mgr_poll(&mgr, 1000);
        }
        mg_mgr_free(&mgr);
    } else {
        // Continuously read in from stdin. When a complete message has been
        // received, handle it, print any resulting output, flush everything,
        // and continue
        char c;
        MessageBuffer message_buffer;

        while (std::cin.get(c)) {
            message_buffer.handle_char(c);

            if (message_buffer.message_completed()) {
                json body = message_buffer.body();
                // Log the message, to stdout and/or the logfile
                if (opts.use_logfile()) {
                    fmt::print(opts.logfile_stream, ">>> Received message of type '{}'\n", body["method"].get<std::string>());
                    if (opts.verbose) {
                        fmt::print(opts.logfile_stream, "Headers:\n");
                        for (auto elem : message_buffer.headers()) {
                            auto pretty_header = fmt::format("{}: {}\n", elem.first, elem.second);
                            opts.logfile_stream << pretty_header;
                        }
                        fmt::print(opts.logfile_stream, "Body: \n{}\n\n", body.dump(4));
                        fmt::print(opts.logfile_stream, "Raw: \n{}\n\n", message_buffer.raw());
                    }
                }

                auto message = handle_message(message_buffer, state, opts);
                if (message.has_value()) {
                    fmt::print("{}", message.value());
                    std::cout << std::flush;

                    if (opts.use_logfile() && opts.verbose) {
                        fmt::print(opts.logfile_stream, "<<< Sending message: \n{}\n\n", message.value());
                    }
                }
                opts.logfile_stream.flush();
                message_buffer.clear();
            }
        }
    }

    return 0;
}
