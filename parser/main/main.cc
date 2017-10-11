#include "ast/ast.h"
#include "parser/Node.h"
#include "parser/Result.h"
#include "spdlog/spdlog.h"
#include <ctime>
#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace spd = spdlog;
using namespace std;

void parse_and_print(ruby_typer::ast::ContextBase &ctx, cxxopts::Options &opts, const std::string &src) {
    auto r = ruby_typer::parser::parse_ruby(ctx, src);
    auto ast = r.ast();
    if (ast) {
        if (!opts["q"].as<bool>()) {
            cout << ast->toString(ctx, 0) << endl;
        }
    } else {
        cout << " got null" << endl;
    }
}

int main(int argc, char **argv) {
    std::vector<std::string> files;
    //    spd::set_async_mode(1024);
    auto color_sink = std::make_shared<spdlog::sinks::ansicolor_stdout_sink_st>();
    color_sink->set_color(spd::level::info, color_sink->white);
    color_sink->set_color(spd::level::debug, color_sink->magenta);
    std::shared_ptr<spd::logger> console = spd::details::registry::instance().create("console", color_sink);
    std::shared_ptr<spd::logger> console_err = spd::stderr_color_st("");

    cxxopts::Options options("parse_ruby", "Parse ruby code and print it");
    options.add_options()("l,long", "Show long detailed output")("v,verbose", "Verbosity level [0-3]");
    options.add_options()("h,help", "Show help");
    options.add_options()("q,quiet", "Be quiet");
    options.add_options()("e", "Parse an inline ruby fragment", cxxopts::value<std::string>());
    options.add_options()("files", "Input files", cxxopts::value<std::vector<std::string>>(files));
    options.parse_positional("files");

    try {
        options.parse(argc, argv);
    } catch (cxxopts::option_not_exists_exception e) {
        console->info("{} \n {} \n", e.what(), options.help());
        return 0;
    }

    if (options["h"].as<bool>() || (options.count("e") == 0 && files.empty())) {
        console->info("{}\n", options.help());
        return 0;
    }

    switch (options.count("v")) {
        case 0:
            break;
        case 1:
            spd::set_level(spd::level::debug);
            console->debug("Debug logging enabled");
            break;
        default:
            spd::set_level(spd::level::trace);
            console->trace("Trace logging enabled");
            break;
    }

    ruby_typer::ast::ContextBase ctx(*console);

    clock_t begin = clock();
    int count = 0;
    if (options.count("e")) {
        count = 1;
        parse_and_print(ctx, options, options["e"].as<string>());
    } else {
        count = files.size();
        for (auto &fileName : files) {
            parse_and_print(ctx, options, ruby_typer::File::read(fileName.c_str()));
        }
    }
    clock_t end = clock();
    double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC * 1000;

    console_err->info("Total {} files. Done in {} ms, nt size: {}, st size: {}\n", count, elapsed_secs, -1, -1);

    return 0;
}
