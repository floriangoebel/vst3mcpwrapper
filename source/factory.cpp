#include "version.h"
#include "pluginids.h"
#include "processor.h"
#include "controller.h"

#include "public.sdk/source/main/pluginfactory.h"

BEGIN_FACTORY_DEF(stringCompanyName, "", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(VST3MCPWrapper::kProcessorUID),
    PClassInfo::kManyInstances,
    kVstAudioEffectClass,
    stringPluginName,
    0, // Not distributable — processor and controller share state via singleton
    Steinberg::Vst::PlugType::kFx,
    FULL_VERSION_STR,
    kVstVersionString,
    VST3MCPWrapper::Processor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(VST3MCPWrapper::kControllerUID),
    PClassInfo::kManyInstances,
    kVstComponentControllerClass,
    stringPluginName "Controller",
    0,
    "",
    FULL_VERSION_STR,
    kVstVersionString,
    VST3MCPWrapper::Controller::createInstance)

END_FACTORY
