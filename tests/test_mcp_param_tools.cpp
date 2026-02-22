#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <limits>

#include "mcp_param_handlers.h"
#include "helpers/test_helpers.h"
#include "mocks/mock_vst3.h"
#include "hostedplugin.h"

using namespace VST3MCPWrapper;
using namespace VST3MCPWrapper::Testing;
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace testing;

namespace {

// Helper to create a ParameterInfo with common fields
ParameterInfo makeParamInfo(ParamID id, const char16_t* title, const char16_t* units,
                            ParamValue defaultVal, int32 stepCount, int32 flags) {
    ParameterInfo info = {};
    info.id = id;
    fillTChar(info.title, title);
    fillTChar(info.units, units);
    info.defaultNormalizedValue = defaultVal;
    info.stepCount = stepCount;
    info.flags = flags;
    return info;
}

class MCPParamToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Drain any leftover param changes from previous tests
        std::vector<ParamChange> drain;
        HostedPluginModule::instance().drainParamChanges(drain);
    }

    void TearDown() override {
        std::vector<ParamChange> drain;
        HostedPluginModule::instance().drainParamChanges(drain);
    }
};

// ============================================================
// list_parameters
// ============================================================

TEST_F(MCPParamToolsTest, ListParametersNoPluginLoaded) {
    auto result = handleListParameters(nullptr);
    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());
    auto content = result["content"][0]["text"].get<std::string>();
    EXPECT_EQ(content, "No hosted plugin loaded");
}

TEST_F(MCPParamToolsTest, ListParametersReturnsAllFields) {
    MockEditController mockCtrl;

    ParameterInfo info1 = makeParamInfo(
        100, u"Volume", u"dB", 0.5, 0, ParameterInfo::kCanAutomate);
    ParameterInfo info2 = makeParamInfo(
        200, u"Pan", u"", 0.5, 100, 0); // not automatable, stepCount=100

    EXPECT_CALL(mockCtrl, getParameterCount())
        .WillRepeatedly(Return(2));
    EXPECT_CALL(mockCtrl, getParameterInfo(0, _))
        .WillRepeatedly(DoAll(SetArgReferee<1>(info1), Return(kResultOk)));
    EXPECT_CALL(mockCtrl, getParameterInfo(1, _))
        .WillRepeatedly(DoAll(SetArgReferee<1>(info2), Return(kResultOk)));
    EXPECT_CALL(mockCtrl, getParamNormalized(100))
        .WillRepeatedly(Return(0.75));
    EXPECT_CALL(mockCtrl, getParamNormalized(200))
        .WillRepeatedly(Return(0.3));
    EXPECT_CALL(mockCtrl, getParamStringByValue(_, _, _))
        .WillRepeatedly(Return(kResultFalse));

    auto result = handleListParameters(&mockCtrl);
    EXPECT_FALSE(result.contains("isError"));

    // Parse the embedded JSON text
    auto contentText = result["content"][0]["text"].get<std::string>();
    auto paramList = mcp::json::parse(contentText);

    ASSERT_EQ(paramList.size(), 2u);

    // Check first parameter has all required fields
    auto& p1 = paramList[0];
    EXPECT_EQ(p1["id"].get<uint32>(), 100u);
    EXPECT_EQ(p1["title"].get<std::string>(), "Volume");
    EXPECT_EQ(p1["units"].get<std::string>(), "dB");
    EXPECT_DOUBLE_EQ(p1["normalizedValue"].get<double>(), 0.75);
    EXPECT_TRUE(p1.contains("displayValue"));
    EXPECT_DOUBLE_EQ(p1["defaultNormalizedValue"].get<double>(), 0.5);
    EXPECT_EQ(p1["stepCount"].get<int32>(), 0);
    EXPECT_TRUE(p1["canAutomate"].get<bool>());

    // Check second parameter
    auto& p2 = paramList[1];
    EXPECT_EQ(p2["id"].get<uint32>(), 200u);
    EXPECT_EQ(p2["title"].get<std::string>(), "Pan");
    EXPECT_EQ(p2["units"].get<std::string>(), "");
    EXPECT_DOUBLE_EQ(p2["normalizedValue"].get<double>(), 0.3);
    EXPECT_EQ(p2["stepCount"].get<int32>(), 100);
    EXPECT_FALSE(p2["canAutomate"].get<bool>());
}

TEST_F(MCPParamToolsTest, ListParametersWithDisplayValue) {
    MockEditController mockCtrl;

    ParameterInfo info = makeParamInfo(
        42, u"Gain", u"dB", 0.0, 0, ParameterInfo::kCanAutomate);

    EXPECT_CALL(mockCtrl, getParameterCount())
        .WillRepeatedly(Return(1));
    EXPECT_CALL(mockCtrl, getParameterInfo(0, _))
        .WillRepeatedly(DoAll(SetArgReferee<1>(info), Return(kResultOk)));
    EXPECT_CALL(mockCtrl, getParamNormalized(42))
        .WillRepeatedly(Return(0.5));
    EXPECT_CALL(mockCtrl, getParamStringByValue(42, 0.5, _))
        .WillRepeatedly(Invoke([](ParamID, ParamValue, TChar* string) -> tresult {
            fillTChar(string, u"-6.0 dB");
            return kResultOk;
        }));

    auto result = handleListParameters(&mockCtrl);
    auto contentText = result["content"][0]["text"].get<std::string>();
    auto paramList = mcp::json::parse(contentText);

    ASSERT_EQ(paramList.size(), 1u);
    EXPECT_EQ(paramList[0]["displayValue"].get<std::string>(), "-6.0 dB");
}

// ============================================================
// get_parameter
// ============================================================

TEST_F(MCPParamToolsTest, GetParameterNoPluginLoaded) {
    auto result = handleGetParameter(nullptr, 100);
    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());
    auto content = result["content"][0]["text"].get<std::string>();
    EXPECT_EQ(content, "No hosted plugin loaded");
}

TEST_F(MCPParamToolsTest, GetParameterValidId) {
    MockEditController mockCtrl;

    ParameterInfo info = makeParamInfo(
        42, u"Gain", u"dB", 0.0, 0, ParameterInfo::kCanAutomate);

    EXPECT_CALL(mockCtrl, getParameterCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(mockCtrl, getParameterInfo(0, _))
        .WillRepeatedly(DoAll(SetArgReferee<1>(info), Return(kResultOk)));
    EXPECT_CALL(mockCtrl, getParamNormalized(42))
        .WillRepeatedly(Return(0.5));
    EXPECT_CALL(mockCtrl, getParamStringByValue(42, 0.5, _))
        .WillRepeatedly(Invoke([](ParamID, ParamValue, TChar* string) -> tresult {
            fillTChar(string, u"-6.0 dB");
            return kResultOk;
        }));

    auto result = handleGetParameter(&mockCtrl, 42);
    EXPECT_FALSE(result.contains("isError"));

    auto contentText = result["content"][0]["text"].get<std::string>();
    auto data = mcp::json::parse(contentText);
    EXPECT_EQ(data["id"].get<uint32>(), 42u);
    EXPECT_DOUBLE_EQ(data["normalizedValue"].get<double>(), 0.5);
    EXPECT_EQ(data["displayValue"].get<std::string>(), "-6.0 dB");
}

TEST_F(MCPParamToolsTest, GetParameterInvalidId) {
    MockEditController mockCtrl;

    EXPECT_CALL(mockCtrl, getParameterCount()).WillRepeatedly(Return(0));

    auto result = handleGetParameter(&mockCtrl, 999);
    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());
    auto content = result["content"][0]["text"].get<std::string>();
    EXPECT_NE(content.find("999"), std::string::npos);
}

// ============================================================
// set_parameter
// ============================================================

TEST_F(MCPParamToolsTest, SetParameterNoPluginLoaded) {
    auto result = handleSetParameter(nullptr, 100, 0.5);
    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());
    auto content = result["content"][0]["text"].get<std::string>();
    EXPECT_EQ(content, "No hosted plugin loaded");
}

TEST_F(MCPParamToolsTest, SetParameterValidIdUpdates) {
    MockEditController mockCtrl;

    ParameterInfo info = makeParamInfo(
        50, u"Freq", u"Hz", 0.5, 0, ParameterInfo::kCanAutomate);

    EXPECT_CALL(mockCtrl, getParameterCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(mockCtrl, getParameterInfo(0, _))
        .WillRepeatedly(DoAll(SetArgReferee<1>(info), Return(kResultOk)));
    EXPECT_CALL(mockCtrl, setParamNormalized(50, 0.75))
        .WillOnce(Return(kResultOk));
    EXPECT_CALL(mockCtrl, getParamNormalized(50))
        .WillRepeatedly(Return(0.75));
    EXPECT_CALL(mockCtrl, getParamStringByValue(50, 0.75, _))
        .WillRepeatedly(Return(kResultFalse));

    auto result = handleSetParameter(&mockCtrl, 50, 0.75);
    EXPECT_FALSE(result.contains("isError"));

    auto contentText = result["content"][0]["text"].get<std::string>();
    auto data = mcp::json::parse(contentText);
    EXPECT_EQ(data["id"].get<uint32>(), 50u);
    EXPECT_DOUBLE_EQ(data["normalizedValue"].get<double>(), 0.75);

    // Verify param change was queued in the singleton
    std::vector<ParamChange> changes;
    HostedPluginModule::instance().drainParamChanges(changes);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].id, 50u);
    EXPECT_DOUBLE_EQ(changes[0].value, 0.75);
}

TEST_F(MCPParamToolsTest, SetParameterInvalidId) {
    MockEditController mockCtrl;

    EXPECT_CALL(mockCtrl, getParameterCount()).WillRepeatedly(Return(0));

    auto result = handleSetParameter(&mockCtrl, 999, 0.5);
    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());
    auto content = result["content"][0]["text"].get<std::string>();
    EXPECT_NE(content.find("999"), std::string::npos);

    // Verify nothing was queued
    std::vector<ParamChange> changes;
    HostedPluginModule::instance().drainParamChanges(changes);
    EXPECT_TRUE(changes.empty());
}

TEST_F(MCPParamToolsTest, SetParameterClampsValue) {
    MockEditController mockCtrl;

    ParameterInfo info = makeParamInfo(
        10, u"Level", u"", 0.5, 0, ParameterInfo::kCanAutomate);

    EXPECT_CALL(mockCtrl, getParameterCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(mockCtrl, getParameterInfo(0, _))
        .WillRepeatedly(DoAll(SetArgReferee<1>(info), Return(kResultOk)));
    // Value 1.5 should be clamped to 1.0
    EXPECT_CALL(mockCtrl, setParamNormalized(10, 1.0))
        .WillOnce(Return(kResultOk));
    EXPECT_CALL(mockCtrl, getParamNormalized(10))
        .WillRepeatedly(Return(1.0));
    EXPECT_CALL(mockCtrl, getParamStringByValue(10, 1.0, _))
        .WillRepeatedly(Return(kResultFalse));

    auto result = handleSetParameter(&mockCtrl, 10, 1.5);
    EXPECT_FALSE(result.contains("isError"));

    // Verify clamped value was queued
    std::vector<ParamChange> changes;
    HostedPluginModule::instance().drainParamChanges(changes);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_DOUBLE_EQ(changes[0].value, 1.0);
}

// ============================================================
// set_parameter — NaN / Inf rejection
// ============================================================

TEST_F(MCPParamToolsTest, SetParameterRejectsNaN) {
    MockEditController mockCtrl;

    ParameterInfo info = makeParamInfo(
        10, u"Level", u"", 0.5, 0, ParameterInfo::kCanAutomate);

    EXPECT_CALL(mockCtrl, getParameterCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(mockCtrl, getParameterInfo(0, _))
        .WillRepeatedly(DoAll(SetArgReferee<1>(info), Return(kResultOk)));

    auto result = handleSetParameter(&mockCtrl, 10, std::numeric_limits<double>::quiet_NaN());
    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());
    auto content = result["content"][0]["text"].get<std::string>();
    EXPECT_NE(content.find("finite"), std::string::npos);

    // Verify nothing was queued
    std::vector<ParamChange> changes;
    HostedPluginModule::instance().drainParamChanges(changes);
    EXPECT_TRUE(changes.empty());
}

TEST_F(MCPParamToolsTest, SetParameterRejectsPosInf) {
    MockEditController mockCtrl;

    ParameterInfo info = makeParamInfo(
        10, u"Level", u"", 0.5, 0, ParameterInfo::kCanAutomate);

    EXPECT_CALL(mockCtrl, getParameterCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(mockCtrl, getParameterInfo(0, _))
        .WillRepeatedly(DoAll(SetArgReferee<1>(info), Return(kResultOk)));

    auto result = handleSetParameter(&mockCtrl, 10, std::numeric_limits<double>::infinity());
    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());

    // Verify nothing was queued
    std::vector<ParamChange> changes;
    HostedPluginModule::instance().drainParamChanges(changes);
    EXPECT_TRUE(changes.empty());
}

TEST_F(MCPParamToolsTest, SetParameterRejectsNegInf) {
    MockEditController mockCtrl;

    ParameterInfo info = makeParamInfo(
        10, u"Level", u"", 0.5, 0, ParameterInfo::kCanAutomate);

    EXPECT_CALL(mockCtrl, getParameterCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(mockCtrl, getParameterInfo(0, _))
        .WillRepeatedly(DoAll(SetArgReferee<1>(info), Return(kResultOk)));

    auto result = handleSetParameter(&mockCtrl, 10, -std::numeric_limits<double>::infinity());
    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());

    // Verify nothing was queued
    std::vector<ParamChange> changes;
    HostedPluginModule::instance().drainParamChanges(changes);
    EXPECT_TRUE(changes.empty());
}

} // anonymous namespace
