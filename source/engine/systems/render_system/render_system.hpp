#pragma once
#include "engine/device/device.hpp"

#include "engine/pipeline/pipeline.hpp"
#include "engine/pipeline/descriptors/descriptors.hpp"
#include "engine/swap_chain/swap_chain.hpp"
#include "engine/buffer/buffer.hpp"
#include "engine/camera/camera.hpp"
#include "engine/scene/scene.hpp"

#include <memory>

namespace Renderer{
    class RenderSystem{
        public:
            struct ModelMatrixInfo{            
                glm::mat4 modelMatrix{1.f};
                glm::mat4 normalMatrix{1.f};
            };

            RenderSystem(Device& device, VkRenderPass renderPass);
            ~RenderSystem();

            void updateScene(Camera camera, uint32_t frameIndex);
            void drawScene(VkCommandBuffer commandBuffer, uint32_t frameIndex);

        private:
            void initializeRenderSystem();

            void setupScene();

            void createDrawIndirectCommands();
            void setupBuffers();

            void setupDescriptorSets();

            void createGraphicsPipelineLayout();
            void createGraphicsPipeline();

            void createComputePipelineLayout();
            void createComputePipeline();
            
            size_t padUniformBufferSize(size_t originalSize);

            Device& device;
            VkRenderPass renderPass;

            Scene scene;

            std::unique_ptr<ComputePipeline> cullPipeline;
            VkPipelineLayout cullPipelineLayout;

            std::unique_ptr<GraphicsPipeline> renderPipeline;
            VkPipelineLayout renderPipelineLayout;

            std::vector<std::unique_ptr<Buffer>> objectInfoBuffers;
            std::vector<Object::ObjectInfo> objectInfos;
            uint32_t objectInfoDynamicAlignment;

            std::vector<std::unique_ptr<Buffer>> indirectCommandsBuffers;
            std::vector<VkDrawIndexedIndirectCommand> indirectCommands;

            std::unique_ptr<DescriptorPool> globalPool;

            std::unique_ptr<DescriptorSetLayout> cullSetLayout;
            std::unique_ptr<DescriptorSetLayout> renderSetLayout;

            std::unique_ptr<Buffer> globalIndexBuffer;

            std::vector<std::unique_ptr<Buffer>> sceneUniformBuffers;
            uint32_t latestBinding = 0;

            uint32_t totalInstanceCount = 0;
    };
}