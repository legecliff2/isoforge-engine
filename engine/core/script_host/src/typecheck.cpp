// Pack-load strict type check (ARCHITECTURE.md §3.3) using Luau.Analysis.
#include "script_host/script_host.hpp"

#include <Luau/BuiltinDefinitions.h>
#include <Luau/ConfigResolver.h>
#include <Luau/Error.h>
#include <Luau/FileResolver.h>
#include <Luau/Frontend.h>
#include <Luau/Module.h>
#include <Luau/Type.h>
#include <Luau/TypeArena.h>

namespace eng::script {

namespace {

// Forces strict mode: rewrites mode hot comments in place (same line count, so error lines
// stay correct). The config default is Strict as well.
std::string force_strict(std::string src) {
    for (const char* hc : {"--!nonstrict", "--!nocheck"}) {
        const std::string h = hc;
        for (std::size_t pos = src.find(h); pos != std::string::npos; pos = src.find(h, pos)) {
            const bool at_line_start = pos == 0 || src[pos - 1] == '\n';
            if (at_line_start) src.replace(pos, h.size(), "--!strict" + std::string(h.size() - 9, ' '));
            pos += h.size();
        }
    }
    return src;
}

struct MapResolver : Luau::FileResolver {
    const std::map<std::string, std::string>* sources = nullptr;
    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override {
        auto it = sources->find(name);
        if (it == sources->end()) return std::nullopt;
        return Luau::SourceCode{force_strict(it->second), Luau::SourceCode::Module};
    }
};

void remove_global(Luau::GlobalTypes& g, const char* name) {
    Luau::AstName n = g.globalNames.names->get(name);
    if (n.value) g.globalScope->bindings.erase(Luau::Symbol(n));
}

void remove_props(Luau::GlobalTypes& g, const char* table, std::initializer_list<const char*> props) {
    Luau::AstName n = g.globalNames.names->get(table);
    if (!n.value) return;
    auto it = g.globalScope->bindings.find(Luau::Symbol(n));
    if (it == g.globalScope->bindings.end()) return;
    if (auto* tt = Luau::getMutable<Luau::TableType>(Luau::follow(it->second.typeId)))
        for (const char* p : props) tt->props.erase(p);
}

} // namespace

std::vector<std::string> typecheck(uint32_t api_level, const std::map<std::string, std::string>& sources) {
    std::vector<std::string> errors;
    const std::string defs = api_definitions(api_level);
    if (defs.empty()) {
        errors.push_back("no API definitions for api_level " + std::to_string(api_level));
        return errors;
    }
    MapResolver files;
    files.sources = &sources;
    Luau::NullConfigResolver config;
    config.defaultConfig.mode = Luau::Mode::Strict;
    Luau::FrontendOptions opts;
    opts.runLintChecks = false;
    Luau::Frontend frontend(&files, &config, opts);

    Luau::GlobalTypes& g = frontend.globals;
    Luau::registerBuiltinGlobals(frontend, g);
    // Mirror the runtime sandbox so removed symbols are type errors, not runtime nils.
    for (const char* name : {"os", "loadstring", "getfenv", "setfenv", "require", "gcinfo", "newproxy"})
        remove_global(g, name);
    remove_props(g, "math", {"random", "randomseed", "noise", "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
                             "sinh", "cosh", "tanh", "exp", "log", "log10", "pow", "sqrt", "fmod", "frexp",
                             "ldexp", "modf"});
    remove_props(g, "debug", {"info", "getinfo", "loadmodule", "dumpheap", "resetmemcategory", "setmemcategory"});

    const std::string pkg = "@engine/level" + std::to_string(api_level);
    Luau::LoadDefinitionFileResult lr = frontend.loadDefinitionFile(g, g.globalScope, defs, pkg, false, false);
    if (!lr.success) {
        for (const auto& pe : lr.parseResult.errors)
            errors.push_back(pkg + ":" + std::to_string(pe.getLocation().begin.line + 1) + ": " + pe.getMessage());
        if (lr.module)
            for (const auto& te : lr.module->errors)
                errors.push_back(pkg + ":" + std::to_string(te.location.begin.line + 1) + ": " + Luau::toString(te));
        if (errors.empty()) errors.push_back(pkg + ": definitions failed to load");
        return errors;
    }
    Luau::freeze(g.globalTypes);

    for (const auto& [name, src] : sources) {
        Luau::CheckResult cr = frontend.check(name);
        for (const Luau::TypeError& e : cr.errors)
            errors.push_back(e.moduleName + ":" + std::to_string(e.location.begin.line + 1) + ": " + Luau::toString(e));
    }
    return errors;
}

} // namespace eng::script
