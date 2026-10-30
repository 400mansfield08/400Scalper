// 400Scalper — CLAP plugin implementation.

#include "Plugin.h"
#include "Settings.h"
#if SCALPER_WITH_GUI
 #include "Gui.h"
#endif
#include <cstring>
#include <cstdlib>
#include <string>
#include <sstream>
#include <charconv>
#include <locale>
#include <vector>

namespace scalper {

#ifndef SCALPER_VERSION
 #define SCALPER_VERSION "0.1.0"
#endif

static const char* const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DISTORTION,
    CLAP_PLUGIN_FEATURE_LIMITER,
    CLAP_PLUGIN_FEATURE_MASTERING,
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_MONO,
    nullptr
};

static const clap_plugin_descriptor kDescriptor = {
    CLAP_VERSION_INIT,
    "com.always400.400scalper",
    "400Scalper",
    "Always 400",
    "",
    "",
    "",
    SCALPER_VERSION,
    "Transparent soft-knee clipper with optional multiband",
    kFeatures
};

static inline Plugin* self (const clap_plugin* p) { return static_cast<Plugin*> (p->plugin_data); }
static inline double dbToLin (double db) { return std::pow (10.0, db / 20.0); }

//==============================================================================
Plugin::Plugin (const clap_host* h) : host (h)
{
    for (int i = 0; i < kNumParams; ++i) values[i].store (paramDefs()[size_t (i)].def);

    clap.desc = &kDescriptor;
    clap.plugin_data = this;
    clap.init = [] (const clap_plugin* p) { return self (p)->init(); };
    clap.destroy = [] (const clap_plugin* p) { delete self (p); };
    clap.activate = [] (const clap_plugin* p, double sr, uint32_t mn, uint32_t mx) { return self (p)->activate (sr, mn, mx); };
    clap.deactivate = [] (const clap_plugin* p) { self (p)->deactivate(); };
    clap.start_processing = [] (const clap_plugin*) { return true; };
    clap.stop_processing = [] (const clap_plugin*) {};
    clap.reset = [] (const clap_plugin* p) { self (p)->reset(); };
    clap.process = [] (const clap_plugin* p, const clap_process* pr) { return self (p)->process (pr); };
    clap.on_main_thread = [] (const clap_plugin*) {};
}

Plugin::~Plugin() = default;

bool Plugin::init()
{
    hostParams  = static_cast<const clap_host_params*>  (host->get_extension (host, CLAP_EXT_PARAMS));
    hostLatency = static_cast<const clap_host_latency*> (host->get_extension (host, CLAP_EXT_LATENCY));
    hostGui     = static_cast<const clap_host_gui*>     (host->get_extension (host, CLAP_EXT_GUI));
    viz.prepare (48000.0);
    return true;
}

bool Plugin::activate (double sr, uint32_t, uint32_t)
{
    sampleRate = sr;
    currentSampleRate.store (sr);
    for (auto& e : engines) e.prepare (sr);
    ceiling.prepare (sr);
    smoothCoef = 1.0 - std::exp (-1.0 / (0.010 * sr)); // 10 ms
    const int L = ceiling.latencySamples();
    for (auto& band : vizDelay) for (auto& d : band) d.init (L);
    viz.prepare (sr);
    vizCount = 0;

    inGain  = dbToLin (getParam (kInGain));
    outGain = dbToLin (getParam (kOutGain));
    for (int b = 0; b < kMaxBands; ++b) thr[b] = dbToLin (getParam (clap_id (kThr1 + b)));
    if (hostLatency) hostLatency->changed (host);
    return true;
}

void Plugin::deactivate() {}

void Plugin::reset()
{
    for (auto& e : engines) e.reset();
    ceiling.reset();
    for (auto& band : vizDelay) for (auto& d : band) d.reset();
}

uint32_t Plugin::latency() const
{
    return uint32_t (ChannelEngine::latencySamples() + ceiling.latencySamples());
}

//==============================================================================
double Plugin::getParam (clap_id id) const
{
    const int i = paramIndex (id);
    return i < 0 ? 0.0 : values[i].load (std::memory_order_relaxed);
}

void Plugin::setParamFromHost (clap_id id, double v)
{
    const int i = paramIndex (id);
    if (i >= 0) values[i].store (clampParam (i, v), std::memory_order_relaxed);
}

void Plugin::handleInEvent (const clap_event_header* hdr)
{
    if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (hdr->type == CLAP_EVENT_PARAM_VALUE)
    {
        const auto* ev = reinterpret_cast<const clap_event_param_value*> (hdr);
        setParamFromHost (ev->param_id, ev->value);
    }
}

void Plugin::uiSetParam (clap_id id, double v)
{
    const int i = paramIndex (id);
    if (i < 0) return;
    v = clampParam (i, v);
    values[i].store (v, std::memory_order_relaxed);
    uiEvents.push ({ UiEvent::Value, id, v });
    if (hostParams) hostParams->request_flush (host);
}

void Plugin::uiGesture (clap_id id, bool begin)
{
    if (paramIndex (id) < 0) return;
    uiEvents.push ({ begin ? UiEvent::Begin : UiEvent::End, id, 0.0 });
    if (hostParams) hostParams->request_flush (host);
}

void Plugin::drainUiEvents (const clap_output_events* out)
{
    UiEvent e;
    while (uiEvents.pop (e))
    {
        if (! out) continue;
        if (e.type == UiEvent::Value)
        {
            clap_event_param_value ev {};
            ev.header.size = sizeof (ev);
            ev.header.time = 0;
            ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.header.type = CLAP_EVENT_PARAM_VALUE;
            ev.header.flags = 0;
            ev.param_id = e.id;
            ev.cookie = nullptr;
            ev.note_id = -1; ev.port_index = -1; ev.channel = -1; ev.key = -1;
            ev.value = e.value;
            out->try_push (out, &ev.header);
        }
        else
        {
            clap_event_param_gesture ev {};
            ev.header.size = sizeof (ev);
            ev.header.time = 0;
            ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.header.type = e.type == UiEvent::Begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
            ev.header.flags = 0;
            ev.param_id = e.id;
            out->try_push (out, &ev.header);
        }
    }
}

void Plugin::flushParams (const clap_input_events* in, const clap_output_events* out)
{
    if (in)
        for (uint32_t i = 0, n = in->size (in); i < n; ++i)
            handleInEvent (in->get (in, i));
    drainUiEvents (out);
}

//==============================================================================
clap_process_status Plugin::process (const clap_process* p)
{
    drainUiEvents (p->out_events);

    const uint32_t frames = p->frames_count;
    const uint32_t nEv = p->in_events ? p->in_events->size (p->in_events) : 0;
    uint32_t evIdx = 0;
    uint32_t nextEvTime = nEv > 0 ? p->in_events->get (p->in_events, 0)->time : frames;

    if (p->audio_inputs_count < 1 || p->audio_outputs_count < 1)
    {
        for (; evIdx < nEv; ++evIdx) handleInEvent (p->in_events->get (p->in_events, evIdx));
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& ain = p->audio_inputs[0];
    const auto& aout = p->audio_outputs[0];
    const uint32_t chIn = ain.channel_count, chOut = aout.channel_count;
    float* const* inBuf = ain.data32;
    float* const* outBuf = aout.data32;
    if (! inBuf || ! outBuf || chIn == 0 || chOut == 0) return CLAP_PROCESS_ERROR;

    // parameter snapshot (refreshed whenever events arrive)
    double tIn = 0, tOut = 0, tThr[kMaxBands] {};
    ClipCurve curves[kMaxBands];
    BandSettings settings[kMaxBands];
    double knee[kMaxBands] {};
    int numBands = 1, slopeDb = 24;
    bool anySolo = false;

    auto refresh = [&]
    {
        tIn  = dbToLin (getParam (kInGain));
        tOut = dbToLin (getParam (kOutGain));
        numBands = std::clamp (int (std::lround (getParam (kBands))), 1, kMaxBands);
        slopeDb = slopeDbFromParam (getParam (kSlope));
        for (int i = 0; i < kMaxXovers; ++i)
        {
            const double f = getParam (clap_id (kXover1 + i));
            engines[0].setCrossover (i, f);
            engines[1].setCrossover (i, f);
        }
        anySolo = false;
        for (int b = 0; b < kMaxBands; ++b)
        {
            tThr[b] = dbToLin (getParam (clap_id (kThr1 + b)));
            knee[b] = getParam (clap_id (kKnee1 + b)) / 100.0;
            if (b < numBands && getParam (clap_id (kSolo1 + b)) >= 0.5) anySolo = true;
        }
        for (int b = 0; b < kMaxBands; ++b)
            settings[b].audible = numBands == 1 || ! anySolo || getParam (clap_id (kSolo1 + b)) >= 0.5;
    };
    refresh();

    const bool stereoIn = chIn >= 2;
    float bi[2][kMaxBands], bo[2][kMaxBands];

    for (uint32_t i = 0; i < frames; ++i)
    {
        if (i == nextEvTime)
        {
            while (evIdx < nEv)
            {
                const auto* hdr = p->in_events->get (p->in_events, evIdx);
                if (hdr->time != i) break;
                handleInEvent (hdr);
                ++evIdx;
            }
            nextEvTime = evIdx < nEv ? p->in_events->get (p->in_events, evIdx)->time : frames;
            refresh();
        }

        inGain  += (tIn  - inGain)  * smoothCoef;
        outGain += (tOut - outGain) * smoothCoef;
        for (int b = 0; b < numBands; ++b)
        {
            thr[b] += (tThr[b] - thr[b]) * smoothCoef;
            curves[b].set (thr[b], knee[b]);
        }

        const double xL = double (inBuf[0][i]) * inGain;
        const double xR = (stereoIn ? double (inBuf[1][i]) : double (inBuf[0][i])) * inGain;

        double yL = engines[0].process (xL, numBands, slopeDb, curves, settings, bi[0], bo[0]);
        double yR = stereoIn ? engines[1].process (xR, numBands, slopeDb, curves, settings, bi[1], bo[1]) : yL;
        if (! stereoIn) { for (int b = 0; b < numBands; ++b) { bi[1][b] = bi[0][b]; bo[1][b] = bo[0][b]; } }

        ceiling.process (yL, yR, thr[0], numBands == 1);

        // ---- visualiser (pre out-gain), aligned to the final output ----
        for (int b = 0; b < numBands; ++b)
        {
            float iMn = vizDelay[b][0].process (std::min (bi[0][b], bi[1][b]));
            float iMx = vizDelay[b][1].process (std::max (bi[0][b], bi[1][b]));
            float oMn = vizDelay[b][2].process (std::min (bo[0][b], bo[1][b]));
            float oMx = vizDelay[b][3].process (std::max (bo[0][b], bo[1][b]));
            if (numBands == 1) { oMn = float (std::min (yL, yR)); oMx = float (std::max (yL, yR)); }
            auto& a = vizAcc[b];
            if (vizCount == 0) a = { iMn, iMx, oMn, oMx };
            else { a.inMin = std::min (a.inMin, iMn); a.inMax = std::max (a.inMax, iMx); a.outMin = std::min (a.outMin, oMn); a.outMax = std::max (a.outMax, oMx); }
        }
        if (++vizCount >= VizHistory::kDecim)
        {
            for (int b = numBands; b < kMaxBands; ++b) vizAcc[b] = { 0, 0, 0, 0 };
            viz.push (vizAcc);
            vizCount = 0;
        }

        const double oL = yL * outGain, oR = yR * outGain;
        meterIn.raise (float (std::max (std::fabs (xL), std::fabs (xR))));
        meterOut.raise (float (std::max (std::fabs (oL), std::fabs (oR))));
        spectrum.push (float (0.5 * (xL + xR)));

        outBuf[0][i] = float (oL);
        if (chOut >= 2) outBuf[1][i] = float (oR);
        for (uint32_t c = 2; c < chOut; ++c) outBuf[c][i] = 0.0f;
    }

    // any events stamped past the block end
    for (; evIdx < nEv; ++evIdx) handleInEvent (p->in_events->get (p->in_events, evIdx));

    return CLAP_PROCESS_CONTINUE;
}

//==============================================================================
// State: plain text "id=value" lines. Solo is a monitoring control and is not saved.
bool Plugin::saveState (const clap_ostream* stream)
{
    std::ostringstream s;
    s.imbue (std::locale::classic());   // '.' decimal point whatever the host's locale
    s << "400Scalper 1\n";
    s.precision (17);
    for (int i = 0; i < kNumParams; ++i)
        if (paramDefs()[size_t (i)].saved)
            s << paramDefs()[size_t (i)].id << "=" << values[i].load() << "\n";
    s << "sel=" << selectedBand.load() << "\n";
    const std::string data = s.str();
    size_t written = 0;
    while (written < data.size())
    {
        const int64_t n = stream->write (stream, data.data() + written, data.size() - written);
        if (n <= 0) return false;
        written += size_t (n);
    }
    return true;
}

bool Plugin::loadState (const clap_istream* stream)
{
    std::string data;
    char buf[1024];
    for (;;)
    {
        const int64_t n = stream->read (stream, buf, sizeof (buf));
        if (n < 0) return false;
        if (n == 0) break;
        data.append (buf, size_t (n));
    }
    std::istringstream s (data);
    std::string line;
    if (! std::getline (s, line) || line.rfind ("400Scalper", 0) != 0) return false;
    while (std::getline (s, line))
    {
        const auto eq = line.find ('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr (0, eq);
        double v = 0.0;
        const char* first = line.c_str() + eq + 1;
        const char* last = line.c_str() + line.size();
        while (last > first && (last[-1] == '\r' || last[-1] == ' ')) --last;
        if (std::from_chars (first, last, v).ec != std::errc()) continue;   // locale-independent
        if (key == "sel") { selectedBand.store (std::clamp (int (v), 0, kMaxBands - 1)); continue; }
        const int idx = paramIndex (clap_id (std::strtoul (key.c_str(), nullptr, 10)));
        if (idx >= 0 && paramDefs()[size_t (idx)].saved) values[idx].store (clampParam (idx, v));
    }
    for (int b = 0; b < kMaxBands; ++b) values[paramIndex (clap_id (kSolo1 + b))].store (0.0);
    if (hostParams) hostParams->rescan (host, CLAP_PARAM_RESCAN_VALUES);
    return true;
}

//==============================================================================
// Extensions

static const clap_plugin_audio_ports kAudioPorts = {
    [] (const clap_plugin*, bool) -> uint32_t { return 1; },
    [] (const clap_plugin*, uint32_t index, bool isInput, clap_audio_port_info* info) -> bool
    {
        if (index != 0) return false;
        info->id = 0;
        std::snprintf (info->name, sizeof (info->name), "%s", isInput ? "Main In" : "Main Out");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = 0;
        return true;
    }
};

static const clap_plugin_params kParams = {
    [] (const clap_plugin*) -> uint32_t { return uint32_t (kNumParams); },
    [] (const clap_plugin*, uint32_t index, clap_param_info* info) -> bool
    {
        if (index >= uint32_t (kNumParams)) return false;
        const auto& d = paramDefs()[index];
        std::memset (info, 0, sizeof (*info));
        info->id = d.id;
        info->flags = (d.stepped ? uint32_t (CLAP_PARAM_IS_STEPPED) : 0u)
                    | (d.automatable ? uint32_t (CLAP_PARAM_IS_AUTOMATABLE) : 0u)
                    | (d.unit == Unit::Slope ? uint32_t (CLAP_PARAM_IS_ENUM) : 0u);
        info->cookie = nullptr;
        std::snprintf (info->name, sizeof (info->name), "%s", d.name);
        std::snprintf (info->module, sizeof (info->module), "%s", d.module);
        info->min_value = d.min;
        info->max_value = d.max;
        info->default_value = d.def;
        return true;
    },
    [] (const clap_plugin* p, clap_id id, double* out) -> bool
    {
        if (paramIndex (id) < 0) return false;
        *out = self (p)->getParam (id);
        return true;
    },
    [] (const clap_plugin*, clap_id id, double v, char* out, uint32_t size) -> bool
    {
        const int i = paramIndex (id);
        if (i < 0) return false;
        formatParam (i, v, out, size);
        return true;
    },
    [] (const clap_plugin*, clap_id id, const char* text, double* out) -> bool
    {
        const int i = paramIndex (id);
        if (i < 0 || ! text) return false;
        const auto& d = paramDefs()[size_t (i)];
        std::string t (text);
        for (auto& c : t) c = char (std::tolower ((unsigned char) c));
        double v;
        if (d.unit == Unit::Slope)
            v = t.find ("24") != std::string::npos ? 2 : t.find ("12") != std::string::npos ? 1 : 0;
        else if (d.unit == Unit::Toggle)
            v = (t == "on" || t == "1") ? 1 : 0;
        else
        {
            char* end = nullptr;
            v = std::strtod (t.c_str(), &end);
            if (end == t.c_str()) return false;
            if (d.unit == Unit::Hz && t.find ('k') != std::string::npos) v *= 1000.0;
        }
        *out = clampParam (i, v);
        return true;
    },
    [] (const clap_plugin* p, const clap_input_events* in, const clap_output_events* out)
    {
        self (p)->flushParams (in, out);
    }
};

static const clap_plugin_state kState = {
    [] (const clap_plugin* p, const clap_ostream* s) -> bool { return self (p)->saveState (s); },
    [] (const clap_plugin* p, const clap_istream* s) -> bool { return self (p)->loadState (s); }
};

static const clap_plugin_latency kLatency = {
    [] (const clap_plugin* p) -> uint32_t { return self (p)->latency(); }
};

static const void* getExtension (const clap_plugin*, const char* id)
{
    if (! std::strcmp (id, CLAP_EXT_AUDIO_PORTS)) return &kAudioPorts;
    if (! std::strcmp (id, CLAP_EXT_PARAMS))      return &kParams;
    if (! std::strcmp (id, CLAP_EXT_STATE))       return &kState;
    if (! std::strcmp (id, CLAP_EXT_LATENCY))     return &kLatency;
#if SCALPER_WITH_GUI
    if (! std::strcmp (id, CLAP_EXT_GUI))         return getGuiExtension();
#endif
    return nullptr;
}

} // namespace scalper

//==============================================================================
// Factory + entry

static const clap_plugin_factory kFactory = {
    [] (const clap_plugin_factory*) -> uint32_t { return 1; },
    [] (const clap_plugin_factory*, uint32_t index) -> const clap_plugin_descriptor*
    {
        return index == 0 ? &scalper::kDescriptor : nullptr;
    },
    [] (const clap_plugin_factory*, const clap_host* host, const char* id) -> const clap_plugin*
    {
        if (! clap_version_is_compatible (host->clap_version) || std::strcmp (id, scalper::kDescriptor.id) != 0)
            return nullptr;
        auto* p = new scalper::Plugin (host);
        p->clap.get_extension = scalper::getExtension;
        return &p->clap;
    }
};

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry = {
    CLAP_VERSION_INIT,
    [] (const char*) -> bool { return true; },
    [] () {},
    [] (const char* factoryId) -> const void*
    {
        return std::strcmp (factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
    }
};
