// 400Scalper — minimal CLAP host test: lifecycle, params, processing, state, latency, UI JSON.
#include <clap/clap.h>
#include "Gui.h"
#include "Plugin.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

extern "C" const clap_plugin_entry clap_entry;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); ++failures; } else { std::printf ("ok:   " __VA_ARGS__); std::printf ("\n"); } } while (0)

// ---- tiny host ----
static int flushRequests = 0, restartRequests = 0;
static const clap_host_params kHostParams = {
    [] (const clap_host*, clap_param_rescan_flags) {},
    [] (const clap_host*, clap_id, clap_param_clear_flags) {},
    [] (const clap_host*) { ++flushRequests; }
};
static const void* hostGetExt (const clap_host*, const char* id)
{
    return std::strcmp (id, CLAP_EXT_PARAMS) == 0 ? &kHostParams : nullptr;
}
static const clap_host kHost = {
    CLAP_VERSION_INIT, nullptr, "test-host", "400", "", "1",
    hostGetExt,
    [] (const clap_host*) { ++restartRequests; }, [] (const clap_host*) {}, [] (const clap_host*) {}
};

struct EventList
{
    std::vector<clap_event_param_value> evs;
    clap_input_events in { this,
        [] (const clap_input_events* l) -> uint32_t { return uint32_t (static_cast<const EventList*> (l->ctx)->evs.size()); },
        [] (const clap_input_events* l, uint32_t i) -> const clap_event_header* { return &static_cast<const EventList*> (l->ctx)->evs[i].header; } };
    void add (uint32_t time, clap_id id, double v)
    {
        clap_event_param_value e {};
        e.header = { sizeof (e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
        e.param_id = id; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v;
        evs.push_back (e);
    }
};

struct OutEvents
{
    std::vector<std::vector<uint8_t>> evs;
    clap_output_events out { this, [] (const clap_output_events* l, const clap_event_header* h) -> bool {
        auto* self = static_cast<OutEvents*> (l->ctx);
        self->evs.emplace_back ((const uint8_t*) h, (const uint8_t*) h + h->size);
        return true; } };
};

struct MemStream
{
    std::string data; size_t pos = 0;
    clap_ostream os { this, [] (const clap_ostream* s, const void* b, uint64_t n) -> int64_t {
        static_cast<MemStream*> (s->ctx)->data.append ((const char*) b, size_t (n)); return int64_t (n); } };
    clap_istream is { this, [] (const clap_istream* s, void* b, uint64_t n) -> int64_t {
        auto* m = static_cast<MemStream*> (s->ctx);
        const size_t k = std::min (size_t (n), m->data.size() - m->pos);
        std::memcpy (b, m->data.data() + m->pos, k); m->pos += k; return int64_t (k); } };
};

int main()
{
    const double fs = 48000.0;
    CHECK (clap_entry.init ("."), "entry init");
    auto* factory = static_cast<const clap_plugin_factory*> (clap_entry.get_factory (CLAP_PLUGIN_FACTORY_ID));
    CHECK (factory && factory->get_plugin_count (factory) == 1, "factory has 1 plugin");
    const auto* desc = factory->get_plugin_descriptor (factory, 0);
    CHECK (std::strcmp (desc->id, "com.always400.400scalper") == 0, "plugin id %s", desc->id);
    const clap_plugin* p = factory->create_plugin (factory, &kHost, desc->id);
    CHECK (p && p->init (p), "create + init");

    auto* params = static_cast<const clap_plugin_params*> (p->get_extension (p, CLAP_EXT_PARAMS));
    auto* state  = static_cast<const clap_plugin_state*>  (p->get_extension (p, CLAP_EXT_STATE));
    auto* lat    = static_cast<const clap_plugin_latency*> (p->get_extension (p, CLAP_EXT_LATENCY));
    auto* ports  = static_cast<const clap_plugin_audio_ports*> (p->get_extension (p, CLAP_EXT_AUDIO_PORTS));
    CHECK (params && state && lat && ports, "extensions present");
    CHECK (params->count (p) == 22, "22 params");

    char txt[64]; double back = 0;
    params->value_to_text (p, scalper::kXover2, 1500.0, txt, sizeof (txt));
    CHECK (std::string (txt) == "1.50 kHz", "value_to_text crossover: %s", txt);
    CHECK (params->text_to_value (p, scalper::kXover2, "2.5k", &back) && std::fabs (back - 2500) < 1e-9, "text_to_value 2.5k -> %.0f", back);

    CHECK (p->activate (p, fs, 32, 1024), "activate at 48 kHz");
    const uint32_t L = lat->get (p);
    CHECK (L == 34 + 34, "latency %u samples (%.2f ms)", L, 1000.0 * L / fs);
    CHECK (p->start_processing (p), "start processing");

    // Process: hot sine, threshold -6 dB -> output peak must stay <= 0.5012.
    const uint32_t N = 512;
    std::vector<float> inL (N), inR (N), outL (N), outR (N);
    float* ins[2] = { inL.data(), inR.data() };
    float* outs[2] = { outL.data(), outR.data() };
    clap_audio_buffer ain {}, aout {};
    ain.data32 = ins; ain.channel_count = 2;
    aout.data32 = outs; aout.channel_count = 2;
    OutEvents oe;
    double peak = 0.0, phase = 0.0;
    for (int block = 0; block < 400; ++block)
    {
        EventList el;
        if (block == 0) el.add (0, scalper::kInGain, 12.0);   // drive +12 dB
        for (uint32_t i = 0; i < N; ++i)
        {
            const double x = 0.9 * std::sin (phase) + 0.1 * std::sin (7.3 * phase);
            phase += 2 * 3.14159265358979 * 220.0 / fs;
            inL[i] = float (x); inR[i] = float (0.8 * x);
        }
        clap_process pr {};
        pr.frames_count = N; pr.steady_time = -1;
        pr.audio_inputs = &ain; pr.audio_outputs = &aout;
        pr.audio_inputs_count = 1; pr.audio_outputs_count = 1;
        pr.in_events = &el.in; pr.out_events = &oe.out;
        const auto st = p->process (p, &pr);
        if (block == 0) CHECK (st == CLAP_PROCESS_CONTINUE, "process returns continue");
        if (block > 20) for (uint32_t i = 0; i < N; ++i) peak = std::max (peak, double (std::max (std::fabs (outL[i]), std::fabs (outR[i]))));
    }
    CHECK (peak <= 0.50119 && peak > 0.45, "driven output peak %.5f stays at the -6 dB threshold (0.50119)", peak);

    // UI -> host parameter path: uiSetParam must produce an out event on next process.
    auto* plug = static_cast<scalper::Plugin*> (p->plugin_data);
    flushRequests = 0;
    plug->uiGesture (scalper::kOutGain, true);
    plug->uiSetParam (scalper::kOutGain, -3.0);
    plug->uiGesture (scalper::kOutGain, false);
    CHECK (flushRequests == 3, "UI changes request a host flush (%d)", flushRequests);
    OutEvents oe2;
    EventList empty;
    params->flush (p, &empty.in, &oe2.out);
    CHECK (oe2.evs.size() == 3, "flush emitted gesture-begin, value, gesture-end (%zu events)", oe2.evs.size());
    double ov = 0; params->get_value (p, scalper::kOutGain, &ov);
    CHECK (ov == -3.0, "out gain now %.1f dB", ov);

    // State round trip (solo must not be restored).
    plug->uiSetParam (scalper::kBands, 3);
    plug->uiSetParam (scalper::kXover1, 250);
    plug->uiSetParam (scalper::kSolo1 + 1, 1);
    plug->selectedBand.store (2);
    MemStream ms;
    CHECK (state->save (p, &ms.os), "state save (%zu bytes)", ms.data.size());
    plug->uiSetParam (scalper::kBands, 1);
    plug->uiSetParam (scalper::kXover1, 120);
    plug->selectedBand.store (0);
    CHECK (state->load (p, &ms.is), "state load");
    double b = 0, x1 = 0, s2 = 1;
    params->get_value (p, scalper::kBands, &b);
    params->get_value (p, scalper::kXover1, &x1);
    params->get_value (p, scalper::kSolo1 + 1, &s2);
    CHECK (b == 3 && x1 == 250 && s2 == 0 && plug->selectedBand.load() == 2, "state restored bands=%.0f xover1=%.0f solo2=%.0f sel=%d", b, x1, s2, plug->selectedBand.load());

    // UI JSON
    const std::string init = scalper::Gui::initJson (*plug);
    const std::string poll = scalper::Gui::pollJson (*plug, 0, 0, 1000, true);
    CHECK (init.find ("\"params\":[") != std::string::npos && init.find ("\"accent\":\"#") != std::string::npos, "init json (%zu bytes)", init.size());
    CHECK (poll.find ("\"v\":[") != std::string::npos && poll.find ("\"f\":[") != std::string::npos, "poll json (%zu bytes)", poll.size());

    // ---- Spectral mode ----
    restartRequests = 0;
    plug->uiSetParam (scalper::kBands, 1);
    plug->uiSetParam (scalper::kMode, 1);
    {
        OutEvents o; EventList e;
        params->flush (p, &e.in, &o.out);
    }
    CHECK (restartRequests == 1, "switching to Spectral requests a host restart (%d)", restartRequests);

    // The audio must switch even if the host never restarts us: measure how much of a quiet
    // 5 kHz tone survives under a hard-clipped 80 Hz tone, Sample vs Spectral, same settings.
    auto hfKept = [&] (int mode) {
        plug->uiSetParam (scalper::kMode, mode);
        plug->uiSetParam (scalper::kInGain, 6.0);
        double a = 0, b = 0, ph = 0; long cnt = 0;
        for (int block = 0; block < 200; ++block)
        {
            EventList el;
            for (uint32_t i = 0; i < N; ++i)
            {
                const double x = 0.9 * std::sin (ph) + 0.03 * std::sin (62.5 * ph);   // 80 Hz + 5 kHz
                ph += 2 * 3.14159265358979 * 80.0 / fs;
                inL[i] = float (x); inR[i] = float (x);
            }
            clap_process pr {};
            pr.frames_count = N; pr.steady_time = -1;
            pr.audio_inputs = &ain; pr.audio_outputs = &aout;
            pr.audio_inputs_count = 1; pr.audio_outputs_count = 1;
            pr.in_events = &el.in; pr.out_events = &oe.out;
            p->process (p, &pr);
            if (block >= 100)
                for (uint32_t i = 0; i < N; ++i, ++cnt)
                {
                    const double t = 2 * 3.14159265358979 * 5000.0 * cnt / fs;
                    a += outL[i] * std::cos (t); b += outL[i] * std::sin (t);
                }
        }
        return 2.0 * std::hypot (a, b) / double (cnt);
    };
    const double kSample = hfKept (0);
    const int pmS = plug->procMode.load();
    const double kSpec = hfKept (1);
    const int pmP = plug->procMode.load();
    CHECK (pmS == 0 && pmP == 1, "audio engine follows the Mode switch without a host restart (sample=%d, spectral=%d)", pmS, pmP);
    CHECK (kSpec > 2.0 * kSample, "quiet 5 kHz detail under heavy clipping: Sample %.4f vs Spectral %.4f (%.1f dB more)", kSample, kSpec, 20 * std::log10 (kSpec / kSample));
    plug->uiSetParam (scalper::kInGain, 12.0);
    plug->uiSetParam (scalper::kMode, 1);

    p->stop_processing (p); p->deactivate (p);
    CHECK (p->activate (p, fs, 32, 1024), "re-activate in Spectral mode");
    const uint32_t L2 = lat->get (p);
    CHECK (L2 == L + 511, "spectral latency %u samples (%.2f ms)", L2, 1000.0 * L2 / fs);
    p->start_processing (p);
    double peak2 = 0.0;
    for (int block = 0; block < 300; ++block)
    {
        EventList el;
        for (uint32_t i = 0; i < N; ++i)
        {
            const double x = 0.9 * std::sin (phase) + 0.05 * std::sin (40.0 * phase);
            phase += 2 * 3.14159265358979 * 110.0 / fs;
            inL[i] = float (x); inR[i] = float (x);
        }
        clap_process pr {};
        pr.frames_count = N; pr.steady_time = -1;
        pr.audio_inputs = &ain; pr.audio_outputs = &aout;
        pr.audio_inputs_count = 1; pr.audio_outputs_count = 1;
        pr.in_events = &el.in; pr.out_events = &oe.out;
        p->process (p, &pr);
        if (block > 20) for (uint32_t i = 0; i < N; ++i) peak2 = std::max (peak2, double (std::fabs (outL[i])));
    }
    double og = 0; params->get_value (p, scalper::kOutGain, &og);
    const double ceilLin = 0.50119 * std::pow (10.0, og / 20.0);
    CHECK (peak2 <= ceilLin + 1e-5 && peak2 > 0.3 * ceilLin, "spectral output peak %.5f within the threshold (%.5f incl. out gain)", peak2, ceilLin);
    const std::string poll2 = scalper::Gui::pollJson (*plug, 0, 0, 100, false);
    CHECK (poll2.find ("\"am\":1") != std::string::npos && poll2.find ("\"sm\":[") != std::string::npos, "UI receives spectral mode + clip mask");
    // ---- DELTA: silent when nothing clips, carries the removed part when it does ----
    auto deltaPeak = [&] (int mode, double amp) {
        plug->uiSetParam (scalper::kMode, mode);
        plug->uiSetParam (scalper::kInGain, 0.0);
        plug->uiSetParam (scalper::kOutGain, 0.0);
        plug->uiSetParam (scalper::kDelta, 1);
        double pk = 0, ph = 0;
        for (int block = 0; block < 200; ++block)
        {
            EventList el;
            for (uint32_t i = 0; i < N; ++i)
            {
                const double x = amp * (0.7 * std::sin (ph) + 0.3 * std::sin (37.0 * ph));   // 80 Hz + 2.96 kHz
                ph += 2 * 3.14159265358979 * 80.0 / fs;
                inL[i] = float (x); inR[i] = float (x);
            }
            clap_process pr {};
            pr.frames_count = N; pr.steady_time = -1;
            pr.audio_inputs = &ain; pr.audio_outputs = &aout;
            pr.audio_inputs_count = 1; pr.audio_outputs_count = 1;
            pr.in_events = &el.in; pr.out_events = &oe.out;
            p->process (p, &pr);
            if (block >= 100) for (uint32_t i = 0; i < N; ++i) pk = std::max (pk, double (std::fabs (outL[i])));
        }
        plug->uiSetParam (scalper::kDelta, 0);
        return pk;
    };
    restartRequests = 0;
    for (int mode : { 0, 1 })
    {
        const double quiet = deltaPeak (mode, 0.3), hot = deltaPeak (mode, 1.0);   // threshold is -6 dB (0.501)
        CHECK (quiet < 1e-4 && hot > 0.1, "%s DELTA: below threshold %.1f dBFS (silent), clipping %.1f dBFS", mode ? "Spectral" : "Sample", 20 * std::log10 (quiet + 1e-12), 20 * std::log10 (hot));
    }

    plug->uiSetParam (scalper::kMode, 0);
    { OutEvents o; EventList e; params->flush (p, &e.in, &o.out); }
    CHECK (restartRequests == 1, "switching away from Spectral requests one restart (%d)", restartRequests);

    p->stop_processing (p);
    p->deactivate (p);
    p->destroy (p);
    clap_entry.deinit();

    std::printf (failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
