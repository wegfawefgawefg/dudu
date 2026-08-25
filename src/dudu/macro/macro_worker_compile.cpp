#include "dudu/macro/macro_worker_compile.hpp"

#include "dudu/core/file_io.hpp"
#include "dudu/macro/macro_hash.hpp"
#include "dudu/native/native_build.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace dudu::macro {
namespace {

std::atomic<std::uint64_t> sdk_staging_counter{0};
constexpr std::size_t max_sdk_cache_entries = 8;
constexpr auto sdk_cache_prune_grace = std::chrono::minutes(10);

using Clock = std::chrono::steady_clock;

std::uint64_t elapsed_ns(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

struct CommandUnit {
    std::string command;
    std::filesystem::path log;
    std::string failure;
};

void run_commands_parallel(const std::vector<CommandUnit>& units) {
    std::vector<int> statuses(units.size(), 0);
    std::atomic<std::size_t> next{0};
    const std::size_t worker_count =
        std::min<std::size_t>(std::max(1U, std::thread::hardware_concurrency()), units.size());
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= units.size())
                    return;
                statuses[index] = run_shell_command(units[index].command, units[index].log);
            }
        });
    }
    for (std::thread& worker : workers)
        worker.join();

    for (std::size_t index = 0; index < units.size(); ++index) {
        if (statuses[index] == 0)
            continue;
        const std::optional<std::string> detail = try_read_text_file(units[index].log);
        throw std::runtime_error(units[index].failure + "\ncommand: " + units[index].command +
                                 "\n" + detail.value_or("native compiler failed"));
    }
}

const CppModuleArtifact& require_artifact(const std::vector<CppModuleArtifact>& artifacts,
                                          const std::filesystem::path& path) {
    const auto found = std::find_if(artifacts.begin(), artifacts.end(),
                                    [&](const auto& artifact) { return artifact.path == path; });
    if (found == artifacts.end()) {
        throw std::runtime_error("macro SDK artifact is missing: " + path.generic_string());
    }
    return *found;
}

struct SdkCacheEntry {
    std::filesystem::path path;
    std::filesystem::file_time_type modified;
};

void maintain_sdk_cache(const std::filesystem::path& cache_dir,
                        const std::filesystem::path& current_entry) {
    std::error_code error;
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(current_entry, now, error);
    error.clear();

    std::vector<SdkCacheEntry> removable;
    std::size_t entry_count = 0;
    std::filesystem::directory_iterator iterator(cache_dir, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
        const std::filesystem::directory_entry candidate = *iterator;
        iterator.increment(error);

        std::error_code candidate_error;
        if (!candidate.is_directory(candidate_error) || candidate_error ||
            candidate.path().filename().string().find(".tmp.") != std::string::npos) {
            continue;
        }
        ++entry_count;
        if (candidate.path() == current_entry) {
            continue;
        }

        const auto modified = candidate.last_write_time(candidate_error);
        if (candidate_error || modified > now - sdk_cache_prune_grace) {
            continue;
        }
        removable.push_back({.path = candidate.path(), .modified = modified});
    }
    if (entry_count <= max_sdk_cache_entries) {
        return;
    }

    std::ranges::sort(removable, {}, &SdkCacheEntry::modified);
    std::size_t remove_count = entry_count - max_sdk_cache_entries;
    for (const SdkCacheEntry& candidate : removable) {
        if (remove_count == 0) {
            break;
        }
        error.clear();
        if (std::filesystem::remove_all(candidate.path, error) > 0 && !error) {
            --remove_count;
        }
    }
}

std::string sdk_identity(const std::vector<CppModuleArtifact>& artifacts,
                         const WorkerBuildOptions& options) {
    StableHash hash;
    hash.add("dudu-macro-sdk-v4-final-path-pch");
    hash.add(options.compiler);
    hash.add(options.cpp_standard);
    hash.add(options.toolchain_identity);
    hash.add(options.dudu_toolchain_identity);
    hash.add(options.sdk_bridge_source.generic_string());
    hash.add(try_read_text_file(options.sdk_bridge_source).value_or(""));
    for (const std::filesystem::path& path :
         {std::filesystem::path("dudu_runtime.hpp"), std::filesystem::path("dudu/ast.hpp"),
          std::filesystem::path("dudu/ast.cpp")}) {
        const CppModuleArtifact& artifact = require_artifact(artifacts, path);
        hash.add(artifact.path.generic_string());
        hash.add(artifact.content);
    }
    for (const std::filesystem::path& include : options.runtime_include_dirs) {
        hash.add(include.generic_string());
    }
    for (const std::filesystem::path& include : options.include_dirs) {
        hash.add(include.generic_string());
    }
    for (const std::string& define : options.defines) {
        hash.add(define);
    }
    for (const std::string& flag : options.compiler_flags) {
        hash.add(flag);
    }
    return hash.finish();
}

std::string compile_prefix(const WorkerBuildOptions& options,
                           const std::filesystem::path& generated_include,
                           std::string_view optimization) {
    std::string command = shell_quote_arg(options.compiler) +
                          " -std=" + shell_quote_arg(options.cpp_standard) + " " +
                          std::string(optimization);
    command += " -I" + shell_quote_path(generated_include);
    for (const std::filesystem::path& include : options.runtime_include_dirs) {
        command += " -I" + shell_quote_path(include);
    }
    for (const std::filesystem::path& include : options.include_dirs) {
        command += " -I" + shell_quote_path(include);
    }
    for (const std::string& define : options.defines) {
        command += " -D" + shell_quote_arg(define);
    }
    for (const std::string& flag : options.compiler_flags) {
        command += " " + shell_quote_arg(flag);
    }
    return command;
}

std::filesystem::path prepare_sdk(const std::vector<CppModuleArtifact>& artifacts,
                                  const WorkerBuildOptions& options) {
    if (options.sdk_cache_dir.empty()) {
        throw std::invalid_argument("macro SDK cache directory is required");
    }
    const std::string identity = sdk_identity(artifacts, options);
    const std::filesystem::path entry = options.sdk_cache_dir / identity;
    const std::filesystem::path object = entry / "dudu_ast.o";
    const std::filesystem::path bridge_object = entry / "dudu_sdk_bridge.o";
    const std::filesystem::path precompiled_header = entry / "dudu_macro_sdk.hpp.gch";
    if (std::filesystem::is_regular_file(object) &&
        std::filesystem::is_regular_file(bridge_object) &&
        std::filesystem::is_regular_file(precompiled_header)) {
        maintain_sdk_cache(options.sdk_cache_dir, entry);
        return entry;
    }
    std::filesystem::create_directories(options.sdk_cache_dir);
    const std::filesystem::path staging =
        options.sdk_cache_dir /
        (identity + ".tmp." + std::to_string(::getpid()) + "." +
         std::to_string(sdk_staging_counter.fetch_add(1, std::memory_order_relaxed)));
    std::filesystem::remove_all(staging);
    for (const std::filesystem::path& path :
         {std::filesystem::path("dudu_runtime.hpp"), std::filesystem::path("dudu/ast.hpp"),
          std::filesystem::path("dudu/ast.cpp")}) {
        const CppModuleArtifact& artifact = require_artifact(artifacts, path);
        write_required_text_file(staging / path, artifact.content);
    }
    const std::optional<std::string> bridge_source = try_read_text_file(options.sdk_bridge_source);
    if (!bridge_source) {
        std::filesystem::remove_all(staging);
        throw std::runtime_error("could not read macro SDK bridge source: " +
                                 options.sdk_bridge_source.string());
    }
    write_required_text_file(staging / "macro_sdk_bridge_generated.cpp", *bridge_source);
    write_required_text_file(staging / "dudu_macro_sdk.hpp",
                             "#include \"dudu/macro/macro_capabilities.hpp\"\n"
                             "#include \"dudu/macro/macro_sdk_bridge_generated.hpp\"\n"
                             "#include \"dudu/macro/macro_worker_runtime.hpp\"\n"
                             "#include \"dudu/ast.hpp\"\n");

    const std::vector<CommandUnit> units = {
        {.command = compile_prefix(options, staging, "-O2") +
                    " -include dudu/macro/macro_capabilities.hpp -c " +
                    shell_quote_path(staging / "dudu/ast.cpp") + " -o " +
                    shell_quote_path(staging / "dudu_ast.o"),
         .log = staging / "ast.log",
         .failure = "could not compile macro SDK"},
        {.command = compile_prefix(options, staging, "-O2") +
                    " -include dudu/macro/macro_capabilities.hpp -c " +
                    shell_quote_path(staging / "macro_sdk_bridge_generated.cpp") + " -o " +
                    shell_quote_path(staging / "dudu_sdk_bridge.o"),
         .log = staging / "bridge.log",
         .failure = "could not compile macro SDK bridge"}};
    try {
        run_commands_parallel(units);
    } catch (...) {
        std::filesystem::remove_all(staging);
        throw;
    }

    std::error_code error;
    std::filesystem::rename(staging, entry, error);
    if (error && (!std::filesystem::is_regular_file(object) ||
                  !std::filesystem::is_regular_file(bridge_object))) {
        std::filesystem::remove_all(staging);
        throw std::runtime_error("could not publish macro SDK cache entry: " + error.message());
    }
    if (error) {
        std::filesystem::remove_all(staging);
    }

    if (!std::filesystem::is_regular_file(precompiled_header)) {
        const std::filesystem::path temporary_pch =
            entry / ("dudu_macro_sdk.hpp.gch.tmp." + std::to_string(::getpid()) + "." +
                     std::to_string(sdk_staging_counter.fetch_add(1, std::memory_order_relaxed)));
        const std::filesystem::path pch_log = temporary_pch.string() + ".log";
        const std::string pch_command = compile_prefix(options, entry, "-O0") + " -x c++-header " +
                                        shell_quote_path(entry / "dudu_macro_sdk.hpp") + " -o " +
                                        shell_quote_path(temporary_pch);
        const int pch_status = run_shell_command(pch_command, pch_log);
        if (pch_status != 0) {
            const std::optional<std::string> detail = try_read_text_file(pch_log);
            std::filesystem::remove(temporary_pch);
            std::filesystem::remove(pch_log);
            throw std::runtime_error(
                "could not compile macro SDK precompiled header\ncommand: " + pch_command + "\n" +
                detail.value_or("macro SDK precompiled header compiler failed"));
        }
        std::filesystem::rename(temporary_pch, precompiled_header, error);
        if (error && !std::filesystem::is_regular_file(precompiled_header)) {
            std::filesystem::remove(temporary_pch);
            std::filesystem::remove(pch_log);
            throw std::runtime_error("could not publish macro SDK precompiled header: " +
                                     error.message());
        }
        std::filesystem::remove(temporary_pch);
        std::filesystem::remove(pch_log);
    }
    maintain_sdk_cache(options.sdk_cache_dir, entry);
    return entry;
}

struct CompileUnit {
    std::filesystem::path source;
    std::filesystem::path object;
    std::filesystem::path log;
    std::string_view optimization;
};

std::vector<CompileUnit> compile_units(const std::filesystem::path& dir,
                                       const std::vector<CppModuleArtifact>& artifacts,
                                       const WorkerBuildOptions& options) {
    std::vector<std::filesystem::path> sources = {dir / "worker.cpp"};
    for (const CppModuleArtifact& artifact : artifacts) {
        if (artifact.kind == CppModuleArtifactKind::Source && artifact.module_path != "dudu.ast") {
            sources.push_back(dir / artifact.path);
        }
    }
    sources.insert(sources.end(), options.cpp_sources.begin(), options.cpp_sources.end());

    const std::filesystem::path object_dir = dir / "objects";
    std::filesystem::create_directories(object_dir);
    std::vector<CompileUnit> units;
    units.reserve(sources.size());
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const std::string stem = "unit_" + std::to_string(index);
        units.push_back({.source = sources[index],
                         .object = object_dir / (stem + ".o"),
                         .log = object_dir / (stem + ".log"),
                         .optimization = "-O0"});
    }
    return units;
}

void compile_units_parallel(const std::filesystem::path& dir, const std::filesystem::path& sdk,
                            const WorkerBuildOptions& options,
                            const std::vector<CompileUnit>& units) {
    std::vector<CommandUnit> commands;
    commands.reserve(units.size());
    for (const CompileUnit& unit : units) {
        commands.push_back({.command = compile_prefix(options, dir, unit.optimization) + " -I" +
                                       shell_quote_path(sdk) + " -include " +
                                       shell_quote_path(sdk / "dudu_macro_sdk.hpp") + " -c " +
                                       shell_quote_path(unit.source) + " -o " +
                                       shell_quote_path(unit.object),
                            .log = unit.log,
                            .failure = "could not compile macro worker source"});
    }
    run_commands_parallel(commands);
}

void link_worker(const std::filesystem::path& dir, const std::filesystem::path& sdk,
                 const WorkerBuildOptions& options, const std::vector<CompileUnit>& units) {
    std::string command = shell_quote_arg(options.compiler);
    for (const CompileUnit& unit : units) {
        command += " " + shell_quote_path(unit.object);
    }
    command += " " + shell_quote_path(sdk / "dudu_ast.o");
    command += " " + shell_quote_path(sdk / "dudu_sdk_bridge.o");
    if (!options.runtime_library.empty()) {
        command += " " + shell_quote_path(options.runtime_library);
    }
    for (const std::filesystem::path& library_dir : options.library_dirs) {
        command += " -L" + shell_quote_path(library_dir);
    }
    for (const std::string& flag : options.compiler_flags) {
        command += " " + shell_quote_arg(flag);
    }
    for (const std::string& library : options.libraries) {
        command += " -l" + shell_quote_arg(library);
    }
    for (const std::string& flag : options.linker_flags) {
        command += " " + shell_quote_arg(flag);
    }
    command += " -o " + shell_quote_path(dir / "worker");
    const std::filesystem::path log = dir / "link.log";
    if (run_shell_command(command, log) != 0) {
        const std::optional<std::string> detail = try_read_text_file(log);
        throw std::runtime_error("could not link macro worker\ncommand: " + command + "\n" +
                                 detail.value_or("macro worker linker failed"));
    }
}

} // namespace

WorkerBinary::Timings compile_worker(const std::filesystem::path& dir,
                                     const std::vector<CppModuleArtifact>& artifacts,
                                     const WorkerBuildOptions& options) {
    WorkerBinary::Timings timings;
    const Clock::time_point sdk_start = Clock::now();
    const std::filesystem::path sdk = prepare_sdk(artifacts, options);
    timings.sdk_prepare_ns = elapsed_ns(sdk_start);
    const std::vector<CompileUnit> units = compile_units(dir, artifacts, options);
    const Clock::time_point compile_start = Clock::now();
    compile_units_parallel(dir, sdk, options, units);
    timings.compile_ns = elapsed_ns(compile_start);
    const Clock::time_point link_start = Clock::now();
    link_worker(dir, sdk, options, units);
    timings.link_ns = elapsed_ns(link_start);
    return timings;
}

} // namespace dudu::macro
