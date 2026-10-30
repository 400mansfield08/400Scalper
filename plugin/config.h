// 400Scalper — iPlug2 project configuration.
// CLAP is the only format built for now; the VST3 / AU / AAX fields below are filled in so
// those formats can be switched on later in plugin/CMakeLists.txt (FORMATS) without changes here.

#define PLUG_NAME "400Scalper"
#define PLUG_MFR "Always 400"
#ifndef SCALPER_VERSION_HEX
 #define SCALPER_VERSION_HEX 0x00000300
#endif
#ifndef SCALPER_VERSION_STR
 #define SCALPER_VERSION_STR "0.3.0"
#endif
#define PLUG_VERSION_HEX SCALPER_VERSION_HEX
#define PLUG_VERSION_STR SCALPER_VERSION_STR
#define PLUG_UNIQUE_ID 'S4cl'
#define PLUG_MFR_ID 'A400'
#define PLUG_URL_STR "https://always400.com"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Always 400"
#define PLUG_CLASS_NAME Scalper

// The CLAP id is BUNDLE_DOMAIN.BUNDLE_MFR.BUNDLE_NAME -> "com.always400.400scalper",
// the same id as v0.1/v0.2, so hosts treat this as an update of the same plugin.
#define BUNDLE_NAME "400scalper"
#define BUNDLE_MFR "always400"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "400Scalper"

#define PLUG_CHANNEL_IO "2-2"

#define PLUG_LATENCY 68
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 900
#define PLUG_HEIGHT 600
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 1
#define PLUG_MIN_WIDTH 640
#define PLUG_MIN_HEIGHT 440
#define PLUG_MAX_WIDTH 4000
#define PLUG_MAX_HEIGHT 3000

#define AUV2_ENTRY Scalper_Entry
#define AUV2_ENTRY_STR "Scalper_Entry"
#define AUV2_FACTORY Scalper_Factory
#define AUV2_VIEW_CLASS Scalper_View
#define AUV2_VIEW_CLASS_STR "Scalper_View"

#define AAX_TYPE_IDS 'S4C1', 'S4C2'
#define AAX_TYPE_IDS_AUDIOSUITE 'S4A1', 'S4A2'
#define AAX_PLUG_MFR_STR "Always 400"
#define AAX_PLUG_NAME_STR "400Scalper\nScalp"
#define AAX_PLUG_CATEGORY_STR "Dynamics"
#define AAX_DOES_AUDIOSUITE 0

#define VST3_SUBCATEGORY "Fx|Distortion|Dynamics"

#define CLAP_MANUAL_URL "https://always400.com"
#define CLAP_SUPPORT_URL "https://always400.com"
#define CLAP_DESCRIPTION "Transparent soft-knee clipper with multiband and spectral modes"
#define CLAP_FEATURES "audio-effect", "distortion", "limiter", "mastering", "stereo"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64
