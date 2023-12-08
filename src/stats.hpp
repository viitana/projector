#pragma once

#include <vector>

#include <vulkan/vulkan.h>

class DeviceOpTimer
{
public:
    DeviceOpTimer() {}

    void Init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t maxFramesInFlight, uint32_t historySize);

    void RecordStartTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits stage);
    void RecordEndTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits stage);

    const float *GetRenderTimes() const { return renderTimes_.data(); }
    const uint32_t GetRenderTimesCount() const { return renderTimes_.size(); }
    const uint32_t GetRenderTimesOffset() const { return renderTimesOffset_; }
    const float GetRenderTimesAverage() const { return sum_ / renderTimes_.size(); }

    // const VkQueryPool GetWarpQueryPool() const;

    void Update();

private:
    // Query pools
    VkQueryPool renderQueryPool_;
    VkQueryPool warpQueryPool_;

    VkDevice device_;
    uint32_t maxFramesInFlight_;

    float timestampPeriodNs_;

    uint32_t lastStampedRenderFrameIndex_;

    std::vector<bool> awaitingTiming_;
    std::vector<float> renderTimes_;
    uint32_t renderTimesOffset_;
    float sum_ = 0;
};
