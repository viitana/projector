#include "stats.hpp"

#include <stdexcept>
#include <iostream>

void DeviceOpTimer::Init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t maxFramesInFlight, uint32_t historySize)
{
    device_ = device;
    maxFramesInFlight_ = maxFramesInFlight;
    lastStampedRenderFrameIndex_ = maxFramesInFlight - 1;

    renderTimes_ = std::vector<float>(historySize, 0);
    awaitingTiming_ = std::vector<bool>(maxFramesInFlight, false);

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    timestampPeriodNs_ = deviceProperties.limits.timestampPeriod;

    VkQueryPoolCreateInfo renderQueryPoolInfo =
    {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = maxFramesInFlight_ * 2, // 2 timestamps (before & after) for each pass
    };
    if (vkCreateQueryPool(device_, &renderQueryPoolInfo, nullptr, &renderQueryPool_) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create create render query pool");
    }

    VkQueryPoolCreateInfo warpQueryPoolInfo =
    {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = 2,
    };
    if (vkCreateQueryPool(device_, &warpQueryPoolInfo, nullptr, &warpQueryPool_) != VK_SUCCESS)
    {
        
        throw std::runtime_error("failed to create create warp query pool");
    }
}

void DeviceOpTimer::RecordStartTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits stage)
{
    uint32_t frameIndex = (lastStampedRenderFrameIndex_ + 1) % maxFramesInFlight_;
    if (awaitingTiming_[frameIndex]) Update();
    if (awaitingTiming_[frameIndex])
    {
        throw std::runtime_error("render timing queue overflow");
    }
    
    vkCmdResetQueryPool(commandBuffer, renderQueryPool_, frameIndex * 2, 2);
    vkCmdWriteTimestamp(commandBuffer, stage, renderQueryPool_, frameIndex * 2 + 0);

    lastStampedRenderFrameIndex_ = frameIndex;
    awaitingTiming_[frameIndex] = true;
}

void DeviceOpTimer::RecordEndTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits stage)
{
    vkCmdWriteTimestamp(commandBuffer, stage, renderQueryPool_, lastStampedRenderFrameIndex_ * 2 + 1);
}

void DeviceOpTimer::Update()
{
    // Render pass
    {
        std::vector<uint64_t> results(maxFramesInFlight_ * 2 * 2, 0);
        VkResult result = vkGetQueryPoolResults(
            device_,
            renderQueryPool_,
            0,
            maxFramesInFlight_ * 2, // 2 queries/timestamps per frame (start & end)
            results.size() * sizeof(uint64_t), // 2 uint64_t entries per query/timestamp (result & availability value)
            results.data(),
            2 * sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT
        );
        if (result != VK_SUCCESS && result != VK_NOT_READY)
        {
            throw std::runtime_error("failed to get render query pool results");
        }

        for (int i = 0; i < maxFramesInFlight_; i++)
        {
            if (!awaitingTiming_[i]) continue;
            if (results[4 * i + 1] == 0) continue; // Start stamp availability
            if (results[4 * i + 3] == 0) continue; // End stamp availability

            uint64_t startTimeStamp = results[4 * i + 0];
            uint64_t endTimeStamp = results[4 * i + 2];
            float timeMs = (endTimeStamp - startTimeStamp) * 0.000001f * timestampPeriodNs_ ;

            renderTimesOffset_ = (renderTimesOffset_ + 1) % renderTimes_.size();
            sum_ -= renderTimes_[renderTimesOffset_];
            sum_ += timeMs;
            renderTimes_[renderTimesOffset_] = timeMs;

            awaitingTiming_[i] = false;
        }
    }
}
