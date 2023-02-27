#include "render_system.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <stdexcept>
#include <cassert>
#include <algorithm>

namespace Renderer{
    RenderSystem::RenderSystem(Device& device, VkRenderPass renderPass) 
    : device{device}, renderPass{renderPass}{
        setupScene();

        createDrawIndirectCommands();

        createUniformBuffers();
        setupDescriptorSets();

        createComputePipelineLayout();
        createComputePipeline();

        createGraphicsPipelineLayout();
        createGraphicsPipeline();
    }

    RenderSystem::~RenderSystem(){
        vkDestroyDescriptorSetLayout(device.getDevice(), cullSetLayout->getLayout(), nullptr);
        vkDestroyPipelineLayout(device.getDevice(), cullPipelineLayout, nullptr);

        vkDestroyDescriptorSetLayout(device.getDevice(), renderSetLayout->getLayout(), nullptr);
        vkDestroyPipelineLayout(device.getDevice(), renderPipelineLayout, nullptr);
    }

    void RenderSystem::setupScene(){
        // All of the below is temporary scene setup for testing, these actions should rather be done in a menu by the user.
        // Diffuse texture sampler
        Sampler::SamplerConfig textureSamplerConfig{};
        textureSamplerConfig.anisotropyEnable = VK_TRUE;
        textureSamplerConfig.maxAnisotropy = 16.f;
        textureSamplerConfig.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        textureSamplerConfig.maxLod = 100.f;
        scene.createSampler(device, textureSamplerConfig);
        
        // Load assets
        scene.loadTexturesWithSampler(device, 0);
        scene.loadModels(device);

        // spongebob object
        scene.createObject();
        scene.objects.at(0).objectInfo.modelId = 0; // spongebob model
        scene.objects.at(0).objectInfo.diffuseId = 0; // spongebob texture
        scene.objects.at(0).transform.translation = {1.5f, .5f, 0.f};
        scene.objects.at(0).transform.rotation = {glm::radians(180.f), 0.f, 0.f};

        // sample object
        scene.createObject();
        scene.objects.at(1).objectInfo.modelId = 1; // sample model
        scene.objects.at(1).objectInfo.diffuseId = 1; // sample texture
        scene.objects.at(1).transform.translation = {-.5f, .5f, 0.f};
        scene.objects.at(1).transform.scale = {4.f, 4.f, 4.f};
    }

    void RenderSystem::createVertexBuffer(){
        // Merge all vertices into one vector and update total vertex count
        for(size_t i = 0; i < scene.models.size(); i++){
            auto model = scene.models.at(i);
            vertices.reserve(model->getVertices().size());
            vertices.insert(vertices.end(), model->getVertices().begin(), model->getVertices().end());
            totalVertexCount += static_cast<uint32_t>(model->getVertexCount());
        }
        if(totalVertexCount == 0)
            return;

        assert(totalVertexCount >= 3 && "Vertex count must be at least 3.");
        VkDeviceSize bufferSize = sizeof(Model::Vertex) * totalVertexCount;

        globalVertexBuffer = std::make_unique<Buffer>(
            device,
            totalVertexCount,
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        globalVertexBuffer->writeDeviceLocalBuffer((void *)vertices.data());
    }

    void RenderSystem::createIndexBuffer(){
        // Merge all indices into one vector and update total index count
        for(size_t i = 0; i < scene.models.size(); i++){
            auto model = scene.models.at(i);
            if(model->getIndexCount() == 0)
                continue;
            indices.reserve(model->getIndices().size());
            indices.insert(indices.end(), model->getIndices().begin(), model->getIndices().end());
            totalIndexCount += static_cast<uint32_t>(model->getIndexCount());
        }
        if(totalIndexCount == 0)
            return;

        VkDeviceSize bufferSize = sizeof(indices[0]) * totalIndexCount;

        globalIndexBuffer = std::make_unique<Buffer>(
            device,
            totalIndexCount,
            bufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        globalIndexBuffer->writeDeviceLocalBuffer((void *)indices.data());
    }

    void RenderSystem::createDrawIndirectCommands(){
        indirectCommands.clear();
        totalInstanceCount = 0;
        // TODO: sort through models that don't have indices and create commands for them and draw them seperately.
        // TODO: glTF models may have multiple nodes with different meshes; may need to have multiple commands per object.
        for(size_t i = 0; i < scene.models.size(); i++){
            uint32_t instanceCount = 0;
            // Get the number of objects that use this model, this will become the number of instances of this model.
            for(size_t j = 0; j < scene.objects.size(); j++)
                if(scene.objects.at(j).objectInfo.modelId == scene.models.at(i)->getId())
                    instanceCount++;
                    
            // Create a new indexedIndirectCommand for each unique model
            VkDrawIndexedIndirectCommand newIndexedIndirectCommand;
            newIndexedIndirectCommand.firstIndex = 0; // Currently there's one mesh per object so this will always be 0.
            newIndexedIndirectCommand.instanceCount = instanceCount; // Number of objects that use this unique model
            newIndexedIndirectCommand.firstInstance = 0; // Should always be 0 at the moment (start draw command at first instance of this model)
            newIndexedIndirectCommand.vertexOffset = 0; // Should always be 0
            newIndexedIndirectCommand.indexCount = scene.models.at(scene.objects.at(i).objectInfo.modelId)->getIndexCount(); // Number of indices the unique model has
            indirectCommands.push_back(newIndexedIndirectCommand); // Add the new command to the vector

            totalInstanceCount += instanceCount; // Add to the total instance count to the current number of instances
        }

        // Send indirect commands to GPU memory (2 buffers are need for double-buffering)
        indirectCommandsBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        for(int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++){
            indirectCommandsBuffers[i] = std::make_unique<Buffer>(
                device,
                1,
                indirectCommands.size() * sizeof(VkDrawIndexedIndirectCommand),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_SHARING_MODE_EXCLUSIVE,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            indirectCommandsBuffers[i]->writeDeviceLocalBuffer(indirectCommands.data());
        }
    }

    void RenderSystem::createUniformBuffers(){
        // Per-object info buffers
        objectInfos.resize(totalInstanceCount);
        for(size_t i = 0 ; i < totalInstanceCount; i++)
            objectInfos.push_back(scene.objects.at(i).objectInfo);

        objectInfoDynamicAlignment = padUniformBufferSize(sizeof(Object::ObjectInfo));
        size_t bufferSize = objectInfoDynamicAlignment * scene.objects.size();

        objectInfoBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        for(int i =0; i < objectInfoBuffers.size(); i++){
            objectInfoBuffers[i] = std::make_unique<Buffer>(
                device, 
                1, 
                bufferSize, 
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
                VK_SHARING_MODE_EXCLUSIVE, 
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            );
            objectInfoBuffers[i]->map();
            for(size_t j = 0; j < scene.objects.size(); j++){
                objectInfoBuffers[i]->writeToBuffer(&scene.objects.at(j).objectInfo, objectInfoDynamicAlignment, static_cast<uint32_t>(objectInfoDynamicAlignment * j));
                objectInfoBuffers[i]->flush();
            }
        }

        // Uniform scene buffers
        sceneUniformBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < sceneUniformBuffers.size(); i++) {
            sceneUniformBuffers[i] = std::make_unique<Buffer>(
                device, 
                1, 
                sizeof(Scene::SceneUniform), 
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
                VK_SHARING_MODE_EXCLUSIVE, 
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            );
            sceneUniformBuffers[i]->map();
        } 
    }

    void RenderSystem::setupDescriptorSets(){
        // Pool Setup
        globalPool = std::make_unique<DescriptorPool>(device);
        globalPool->addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT);                // Indirect draw buffers (for gpu-created draw commands)
        globalPool->addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT);                // Uniform scene info (for render pipeline)
        globalPool->addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, SwapChain::MAX_FRAMES_IN_FLIGHT);        // Object info (resource ids)
        globalPool->buildPool(SwapChain::MAX_FRAMES_IN_FLIGHT * 2); // Multiply max frames in flight by number of sets

        // Layout Setup (Bindings are set in order of when they are added)
        // Render set layout (vert and frag shaders)
        renderSetLayout = std::make_unique<DescriptorSetLayout>(device);
        renderSetLayout->addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS);            // binding 0 (Uniform scene info)
        renderSetLayout->addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_ALL_GRAPHICS);    // binding 1 (Object info)
        renderSetLayout->buildLayout();

        // Cull set layout (cull compute shader)
        cullSetLayout = std::make_unique<DescriptorSetLayout>(device);
        cullSetLayout->addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);               // binding 0 (Uniform scene info)
        cullSetLayout->addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);               // binding 1 (Indirect draw data)
        cullSetLayout->addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_COMPUTE_BIT);       // binding 2 (Object info)
        cullSetLayout->buildLayout();

        // Render Set
        for(int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++){
            uint32_t setIndex = i * SwapChain::MAX_FRAMES_IN_FLIGHT;
            VkDescriptorBufferInfo indirectBufferInfo = indirectCommandsBuffers[i]->descriptorInfo();
            VkDescriptorBufferInfo sceneUniformBufferInfo = sceneUniformBuffers[i]->descriptorInfo();
            VkDescriptorBufferInfo objectBufferInfo = objectInfoBuffers[i]->descriptorInfo();
            objectBufferInfo.range = objectInfoDynamicAlignment;

            std::vector<VkWriteDescriptorSet> renderLayoutWrites {
                renderSetLayout->writeBuffer(0, &sceneUniformBufferInfo),
                renderSetLayout->writeBuffer(1, &objectBufferInfo)
            };
            std::vector<VkWriteDescriptorSet> cullLayoutWrites {
                cullSetLayout->writeBuffer(0, &indirectBufferInfo),
                cullSetLayout->writeBuffer(1, &sceneUniformBufferInfo),
                cullSetLayout->writeBuffer(2, &objectBufferInfo)
            };

            globalPool->allocateSet(renderSetLayout->getLayout());
            globalPool->updateSet(setIndex, renderLayoutWrites);

            globalPool->allocateSet(cullSetLayout->getLayout());
            globalPool->updateSet(setIndex + 1, cullLayoutWrites);
        }
    }

    void RenderSystem::createComputePipelineLayout(){
        auto layout = cullSetLayout->getLayout();
        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &layout;
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        if(vkCreatePipelineLayout(device.getDevice(), &layoutInfo, nullptr, &cullPipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create compute pipeline layout.");
    }

    void RenderSystem::createComputePipeline(){
        assert(cullPipelineLayout != nullptr && "Cannot create compute pipeline before compute pipeline layout.");
        cullPipeline = std::make_unique<ComputePipeline>(
            device,
            "../source/spirv_shaders/indirect_cull.comp.spv",
            cullPipelineLayout
        );
    }

    void RenderSystem::createGraphicsPipelineLayout(){
        auto layout = renderSetLayout->getLayout();
        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &layout;
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        if(vkCreatePipelineLayout(device.getDevice(), &layoutInfo, nullptr, &renderPipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create graphics pipeline layout.");
    }

    void RenderSystem::createGraphicsPipeline(){
        assert(renderPipelineLayout != nullptr && "Cannot create graphics pipeline before graphics pipeline layout.");
        GraphicsPipelineConfigInfo configInfo = {};
        GraphicsPipeline::defaultPipelineConfigInfo(configInfo);
        configInfo.pipelineLayout = renderPipelineLayout;
        configInfo.renderPass = renderPass;
        renderPipeline = std::make_unique<GraphicsPipeline>(
            device,
            "../source/spirv_shaders/main.vert.spv",
            "../source/spirv_shaders/main.frag.spv",
            configInfo
        );
    }

    void RenderSystem::updateSceneUniform(Camera camera, uint32_t frameIndex){
        // TODO: add check to see if camera view changed so needless updates are not performed
        scene.sceneUniform.projection = camera.getProjection();
        scene.sceneUniform.view = camera.getView();
        scene.sceneUniform.inverseView = camera.getInverseView();

        scene.sceneUniform.enableFrustumCulling = camera.enableFrustumCulling;
        scene.sceneUniform.enableOcclusionCulling = true;

        if(scene.sceneUniform.enableFrustumCulling)
            scene.sceneUniform.viewBoundingBox = camera.createFrustumViewBounds();

        scene.sceneUniform.instanceCount = totalInstanceCount;

        sceneUniformBuffers[frameIndex]->writeToBuffer(&scene.sceneUniform);
        sceneUniformBuffers[frameIndex]->flush();
    }

    void RenderSystem::drawScene(VkCommandBuffer commandBuffer, uint32_t frameIndex){
        renderPipeline->bind(commandBuffer);
        vkCmdDrawIndexedIndirect(commandBuffer, indirectCommandsBuffers[frameIndex]->getBuffer(), 0, static_cast<uint32_t>(indirectCommands.size()), sizeof(VkDrawIndexedIndirectCommand));
    }

    void RenderSystem::cullScene(VkCommandBuffer commandBuffer, uint32_t frameIndex){
        assert(cullPipeline != nullptr && "Cannot run GPU-based culling without compute pipeline.");

        cullPipeline->bind(commandBuffer);

        for(size_t i = 0; i < scene.objects.size(); i++){
            uint32_t dynamicOffset = i * static_cast<uint32_t>(objectInfoDynamicAlignment);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cullPipelineLayout, 0, 1, &globalPool->getSets()[frameIndex], 1, &dynamicOffset);
        }
        // LEFT OFF HERE, CONTINUE BY SUBMITTING COMPUTE WORK (VIA VKQUEUESUBMIT)
        vkCmdDispatch(commandBuffer, indirectCommands.size() / 256, 1, 1);
    }

    size_t RenderSystem::padUniformBufferSize(size_t originalSize){
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device.getPhysicalDevice(), &properties);

        size_t minUboAlignment = properties.limits.minUniformBufferOffsetAlignment;
	    size_t alignedSize = originalSize;
	    if (minUboAlignment > 0) 
            alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	    return alignedSize;
    }
}