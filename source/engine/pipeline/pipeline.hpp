#pragma once

#include "engine/device/device.hpp"

#include <vector>
#include <memory>

namespace Renderer{
    struct GraphicsPipelineConfigInfo {
        GraphicsPipelineConfigInfo(const GraphicsPipelineConfigInfo&) = delete;
        GraphicsPipelineConfigInfo& operator=(const GraphicsPipelineConfigInfo&) = delete;

        std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
        VkPipelineViewportStateCreateInfo viewportInfo;
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
        VkPipelineRasterizationStateCreateInfo rasterizationInfo;
        VkPipelineColorBlendAttachmentState colorBlendAttachment;
        VkPipelineColorBlendStateCreateInfo colorBlendInfo;
        VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
        std::vector<VkDynamicState> dynamicStateEnables;
        VkPipelineDynamicStateCreateInfo dynamicStateInfo;
        VkPipelineLayout pipelineLayout = nullptr;
        VkRenderPass renderPass = nullptr;
        uint32_t subpass = 0;
    };

    static std::vector<char> readFile(const std::string& filepath);

    class GraphicsPipeline{
        public:
            GraphicsPipeline(Device& device, const std::string& vertFilepath, const std::string& fragFilepath, const GraphicsPipelineConfigInfo& configInfo);
            ~GraphicsPipeline();

            static void defaultPipelineConfigInfo(GraphicsPipelineConfigInfo& configInfo);
            void bind(VkCommandBuffer commandBuffer);

        private:
            void createGraphicsPipeline(const std::string& vertFilepath, const std::string& fragFilepath, const GraphicsPipelineConfigInfo& configInfo);

            Device& device;
            VkPipeline graphicsPipeline;
            VkShaderModule vertShaderModule, fragShaderModule;
    };

    class ComputePipeline{
        public:
            ComputePipeline(Device& device, const std::string& compFilepath, VkPipelineLayout layout);
            ~ComputePipeline();

            void bind(VkCommandBuffer commandBuffer);
        
        private:
            void createComputePipeline(const std::string& compFilepath, VkPipelineLayout layout);

            Device& device;
            VkPipeline computePipeline;
            VkShaderModule compShaderModule;
    };
}