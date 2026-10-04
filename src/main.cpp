// limitbook: replay a synthetic market-event stream through the matching engine and
// report measured throughput and latency.
//
//   ./limitbook --events 1000000
//   ./limitbook --events 1000000 --mode spsc --book pooled
//   ./limitbook --events 200000 --mode mutex --rate 500000     # paced offered load
//   ./limitbook --events 1000000 --csv ../data/runs.csv        # append a result row

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "event_generator.hpp"
#include "matching_engine.hpp"
#include "pipeline.hpp"

using namespace limitbook;

namespace {

struct Options {
    std::size_t events = 1'000'000;
    std::uint64_t seed = 42;
    std::string mode = "single";  // single | mutex | spsc
    std::string book = "pooled";  // baseline | pooled
    double rate = 0;              // events/sec offered by the producer; 0 = unpaced
    std::size_t maxLive = 10'000;
    std::size_t queueCapacity = 65'536;
    std::string csv;
    std::string dump;
};

[[noreturn]] void usage(int code) {
    std::cout <<
        "usage: limitbook [options]\n"
        "  --events N        number of events to generate and process (default 1000000)\n"
        "  --seed S          RNG seed; same seed = same event stream (default 42)\n"
        "  --mode M          single | mutex | spsc (default single)\n"
        "  --book B          baseline | pooled order book (default pooled)\n"
        "  --rate R          producer pacing in events/sec for mutex/spsc (default: unpaced)\n"
        "  --max-live N      generator bound on live orders (default 10000)\n"
        "  --queue-cap N     queue capacity for mutex/spsc (default 65536)\n"
        "  --csv PATH        append a result row to a CSV file\n"
        "  --dump PATH       write the generated events as text and exit\n";
    std::exit(code);
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) usage(2);
            return argv[++i];
        };
        if (a == "--events") o.events = std::stoull(value());
        else if (a == "--seed") o.seed = std::stoull(value());
        else if (a == "--mode") o.mode = value();
        else if (a == "--book") o.book = value();
        else if (a == "--rate") o.rate = std::stod(value());
        else if (a == "--max-live") o.maxLive = std::stoull(value());
        else if (a == "--queue-cap") o.queueCapacity = std::stoull(value());
        else if (a == "--csv") o.csv = value();
        else if (a == "--dump") o.dump = value();
        else if (a == "--help" || a == "-h") usage(0);
        else usage(2);
    }
    if (o.mode != "single" && o.mode != "mutex" && o.mode != "spsc") usage(2);
    if (o.book != "baseline" && o.book != "pooled") usage(2);
    return o;
}

std::string withCommas(std::uint64_t n) {
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, ",");
    return s;
}

std::string fmtNs(double ns) {
    char buf[32];
    if (ns < 1e3) std::snprintf(buf, sizeof buf, "%.0f ns", ns);
    else if (ns < 1e6) std::snprintf(buf, sizeof buf, "%.2f us", ns / 1e3);
    else std::snprintf(buf, sizeof buf, "%.2f ms", ns / 1e6);
    return buf;
}

template <typename Engine>
RunResult run(const Options& o, const std::vector<Event>& events) {
    Engine engine;
    engine.book().reserve(o.maxLive * 2);
    if (o.mode == "mutex")
        return runPipeline<MutexQueue<TimedEvent>>(engine, events, o.queueCapacity, o.rate);
    if (o.mode == "spsc")
        return runPipeline<SpscRingBuffer<TimedEvent>>(engine, events, o.queueCapacity, o.rate);
    // Two passes over identical input: an untimed pass for throughput, then a fresh
    // engine with per-event timing for the latency distribution.
    RunResult r = runSingleThreaded(engine, events, /*measureLatency=*/false);
    Engine timed;
    timed.book().reserve(o.maxLive * 2);
    r.latency = runSingleThreaded(timed, events, /*measureLatency=*/true).latency;
    return r;
}

void appendCsv(const Options& o, const RunResult& r) {
    const bool fresh = !std::filesystem::exists(o.csv);
    std::ofstream out(o.csv, std::ios::app);
    if (fresh)
        out << "mode,book,events,seed,rate,trades,volume,seconds,events_per_sec,"
               "mean_ns,p50_ns,p95_ns,p99_ns,p999_ns,max_ns\n";
    const auto& l = r.latency;
    out << o.mode << ',' << o.book << ',' << r.events << ',' << o.seed << ',' << o.rate << ','
        << r.trades << ',' << r.volume << ',' << r.seconds << ',' << r.eventsPerSec() << ','
        << l.meanNs << ',' << l.p50Ns << ',' << l.p95Ns << ',' << l.p99Ns << ',' << l.p999Ns
        << ',' << l.maxNs << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    const Options o = parse(argc, argv);

    GeneratorConfig cfg;
    cfg.seed = o.seed;
    cfg.maxLiveOrders = o.maxLive;
    EventGenerator gen(cfg);
    const std::vector<Event> events = gen.generate(o.events);  // generated before timing

    if (!o.dump.empty()) {
        std::ofstream out(o.dump);
        for (const Event& e : events) out << toString(e) << '\n';
        std::cout << "wrote " << events.size() << " events to " << o.dump << '\n';
        return 0;
    }

    const RunResult r = o.book == "baseline" ? run<BaselineEngine>(o, events)
                                             : run<PooledEngine>(o, events);
    const auto& l = r.latency;
    const char* latencyKind = o.mode == "single" ? "engine service time per event"
                                                 : "enqueue -> processed, incl. queueing";

    std::cout << "Mode:              " << o.mode << " (" << o.book << " book)\n"
              << "Events processed:  " << withCommas(r.events) << '\n'
              << "Trades executed:   " << withCommas(r.trades) << '\n'
              << "Shares executed:   " << withCommas(r.volume) << '\n'
              << "Wall time:         " << r.seconds << " s\n"
              << "Throughput:        " << withCommas(static_cast<std::uint64_t>(r.eventsPerSec()))
              << " events/sec\n"
              << "Latency (" << latencyKind << "):\n"
              << "  average:         " << fmtNs(l.meanNs) << '\n'
              << "  p50:             " << fmtNs(static_cast<double>(l.p50Ns)) << '\n'
              << "  p95:             " << fmtNs(static_cast<double>(l.p95Ns)) << '\n'
              << "  p99:             " << fmtNs(static_cast<double>(l.p99Ns)) << '\n'
              << "  p99.9:           " << fmtNs(static_cast<double>(l.p999Ns)) << '\n'
              << "  max:             " << fmtNs(static_cast<double>(l.maxNs)) << '\n';

    if (!o.csv.empty()) appendCsv(o, r);
    return 0;
}
