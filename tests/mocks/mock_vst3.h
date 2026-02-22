#pragma once

#include <atomic>
#include <gmock/gmock.h>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstattributes.h"

namespace VST3MCPWrapper {
namespace Testing {

using namespace Steinberg;
using namespace Steinberg::Vst;

//------------------------------------------------------------------------
// MockComponent - implements IComponent (extends IPluginBase extends FUnknown)
//------------------------------------------------------------------------
class MockComponent : public IComponent
{
public:
    MockComponent () : refCount_ (1) {}

    // --- FUnknown ---
    uint32 PLUGIN_API addRef () override { return ++refCount_; }
    uint32 PLUGIN_API release () override
    {
        auto r = --refCount_;
        return r;
    }
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid) ||
            FUnknownPrivate::iidEqual (_iid, IPluginBase::iid) ||
            FUnknownPrivate::iidEqual (_iid, IComponent::iid))
        {
            addRef ();
            *obj = static_cast<IComponent*> (this);
            return kResultOk;
        }
        if (obj) *obj = nullptr;
        return kNoInterface;
    }

    // --- IPluginBase ---
    MOCK_METHOD (tresult, initialize, (FUnknown*), (override));
    MOCK_METHOD (tresult, terminate, (), (override));

    // --- IComponent ---
    MOCK_METHOD (tresult, getControllerClassId, (TUID), (override));
    MOCK_METHOD (tresult, setIoMode, (IoMode), (override));
    MOCK_METHOD (int32, getBusCount, (MediaType, BusDirection), (override));
    MOCK_METHOD (tresult, getBusInfo, (MediaType, BusDirection, int32, BusInfo&), (override));
    MOCK_METHOD (tresult, getRoutingInfo, (RoutingInfo&, RoutingInfo&), (override));
    MOCK_METHOD (tresult, activateBus, (MediaType, BusDirection, int32, TBool), (override));
    MOCK_METHOD (tresult, setActive, (TBool), (override));
    MOCK_METHOD (tresult, setState, (IBStream*), (override));
    MOCK_METHOD (tresult, getState, (IBStream*), (override));

private:
    std::atomic<uint32> refCount_;
};

//------------------------------------------------------------------------
// MockAudioProcessor - implements IAudioProcessor (extends FUnknown)
//------------------------------------------------------------------------
class MockAudioProcessor : public IAudioProcessor
{
public:
    MockAudioProcessor () : refCount_ (1) {}

    // --- FUnknown ---
    uint32 PLUGIN_API addRef () override { return ++refCount_; }
    uint32 PLUGIN_API release () override
    {
        auto r = --refCount_;
        return r;
    }
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid) ||
            FUnknownPrivate::iidEqual (_iid, IAudioProcessor::iid))
        {
            addRef ();
            *obj = static_cast<IAudioProcessor*> (this);
            return kResultOk;
        }
        if (obj) *obj = nullptr;
        return kNoInterface;
    }

    // --- IAudioProcessor ---
    MOCK_METHOD (tresult, setBusArrangements,
                 (SpeakerArrangement*, int32, SpeakerArrangement*, int32), (override));
    MOCK_METHOD (tresult, getBusArrangement, (BusDirection, int32, SpeakerArrangement&), (override));
    MOCK_METHOD (tresult, canProcessSampleSize, (int32), (override));
    MOCK_METHOD (uint32, getLatencySamples, (), (override));
    MOCK_METHOD (tresult, setupProcessing, (ProcessSetup&), (override));
    MOCK_METHOD (tresult, setProcessing, (TBool), (override));
    MOCK_METHOD (tresult, process, (ProcessData&), (override));
    MOCK_METHOD (uint32, getTailSamples, (), (override));

private:
    std::atomic<uint32> refCount_;
};

//------------------------------------------------------------------------
// MockEditController - implements IEditController (extends IPluginBase extends FUnknown)
//------------------------------------------------------------------------
class MockEditController : public IEditController
{
public:
    MockEditController () : refCount_ (1) {}

    // --- FUnknown ---
    uint32 PLUGIN_API addRef () override { return ++refCount_; }
    uint32 PLUGIN_API release () override
    {
        auto r = --refCount_;
        return r;
    }
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid) ||
            FUnknownPrivate::iidEqual (_iid, IPluginBase::iid) ||
            FUnknownPrivate::iidEqual (_iid, IEditController::iid))
        {
            addRef ();
            *obj = static_cast<IEditController*> (this);
            return kResultOk;
        }
        if (obj) *obj = nullptr;
        return kNoInterface;
    }

    // --- IPluginBase ---
    MOCK_METHOD (tresult, initialize, (FUnknown*), (override));
    MOCK_METHOD (tresult, terminate, (), (override));

    // --- IEditController ---
    MOCK_METHOD (tresult, setComponentState, (IBStream*), (override));
    MOCK_METHOD (tresult, setState, (IBStream*), (override));
    MOCK_METHOD (tresult, getState, (IBStream*), (override));
    MOCK_METHOD (int32, getParameterCount, (), (override));
    MOCK_METHOD (tresult, getParameterInfo, (int32, ParameterInfo&), (override));
    MOCK_METHOD (tresult, getParamStringByValue, (ParamID, ParamValue, String128), (override));
    MOCK_METHOD (tresult, getParamValueByString, (ParamID, TChar*, ParamValue&), (override));
    MOCK_METHOD (ParamValue, normalizedParamToPlain, (ParamID, ParamValue), (override));
    MOCK_METHOD (ParamValue, plainParamToNormalized, (ParamID, ParamValue), (override));
    MOCK_METHOD (ParamValue, getParamNormalized, (ParamID), (override));
    MOCK_METHOD (tresult, setParamNormalized, (ParamID, ParamValue), (override));
    MOCK_METHOD (tresult, setComponentHandler, (IComponentHandler*), (override));
    MOCK_METHOD (IPlugView*, createView, (FIDString), (override));

private:
    std::atomic<uint32> refCount_;
};

//------------------------------------------------------------------------
// MockAttributeList - implements IAttributeList (extends FUnknown)
//------------------------------------------------------------------------
class MockAttributeList : public IAttributeList
{
public:
    MockAttributeList () : refCount_ (1) {}

    // --- FUnknown ---
    uint32 PLUGIN_API addRef () override { return ++refCount_; }
    uint32 PLUGIN_API release () override
    {
        auto r = --refCount_;
        return r;
    }
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid) ||
            FUnknownPrivate::iidEqual (_iid, IAttributeList::iid))
        {
            addRef ();
            *obj = static_cast<IAttributeList*> (this);
            return kResultOk;
        }
        if (obj) *obj = nullptr;
        return kNoInterface;
    }

    // --- IAttributeList ---
    MOCK_METHOD (tresult, setInt, (AttrID, int64), (override));
    MOCK_METHOD (tresult, getInt, (AttrID, int64&), (override));
    MOCK_METHOD (tresult, setFloat, (AttrID, double), (override));
    MOCK_METHOD (tresult, getFloat, (AttrID, double&), (override));
    MOCK_METHOD (tresult, setString, (AttrID, const TChar*), (override));
    MOCK_METHOD (tresult, getString, (AttrID, TChar*, uint32), (override));
    MOCK_METHOD (tresult, setBinary, (AttrID, const void*, uint32), (override));
    MOCK_METHOD (tresult, getBinary, (AttrID, const void*&, uint32&), (override));

private:
    std::atomic<uint32> refCount_;
};

//------------------------------------------------------------------------
// MockMessage - implements IMessage (extends FUnknown)
//------------------------------------------------------------------------
class MockMessage : public IMessage
{
public:
    MockMessage () : refCount_ (1) {}

    // --- FUnknown ---
    uint32 PLUGIN_API addRef () override { return ++refCount_; }
    uint32 PLUGIN_API release () override
    {
        auto r = --refCount_;
        return r;
    }
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid) ||
            FUnknownPrivate::iidEqual (_iid, IMessage::iid))
        {
            addRef ();
            *obj = static_cast<IMessage*> (this);
            return kResultOk;
        }
        if (obj) *obj = nullptr;
        return kNoInterface;
    }

    // --- IMessage ---
    MOCK_METHOD (FIDString, getMessageID, (), (override));
    MOCK_METHOD (void, setMessageID, (FIDString), (override));
    MOCK_METHOD (IAttributeList*, getAttributes, (), (override));

private:
    std::atomic<uint32> refCount_;
};

//------------------------------------------------------------------------
// MockComponentHandler - implements IComponentHandler (extends FUnknown)
// Used to mock the DAW's component handler for testing forwarding behavior.
//------------------------------------------------------------------------
class MockComponentHandler : public IComponentHandler
{
public:
    MockComponentHandler () : refCount_ (1) {}

    // --- FUnknown ---
    uint32 PLUGIN_API addRef () override { return ++refCount_; }
    uint32 PLUGIN_API release () override
    {
        auto r = --refCount_;
        return r;
    }
    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (_iid, FUnknown::iid) ||
            FUnknownPrivate::iidEqual (_iid, IComponentHandler::iid))
        {
            addRef ();
            *obj = static_cast<IComponentHandler*> (this);
            return kResultOk;
        }
        if (obj) *obj = nullptr;
        return kNoInterface;
    }

    // --- IComponentHandler ---
    MOCK_METHOD (tresult, beginEdit, (ParamID), (override));
    MOCK_METHOD (tresult, performEdit, (ParamID, ParamValue), (override));
    MOCK_METHOD (tresult, endEdit, (ParamID), (override));
    MOCK_METHOD (tresult, restartComponent, (int32), (override));

private:
    std::atomic<uint32> refCount_;
};

//------------------------------------------------------------------------
} // namespace Testing
} // namespace VST3MCPWrapper
