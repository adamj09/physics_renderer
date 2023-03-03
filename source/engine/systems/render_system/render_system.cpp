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

        createVertexBuffer();
        createIndexBuffer();

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
        scene.objects.at(0).objectInfo.modelMatrix = scene.objects.at(0).transform.mat4();
        scene.objects.at(0).objectInfo.normalMatrix = scene.objects.at(0).transform.normalMatrix();

        scene.objects.at(0).transform.translation = {1.5f, .5f, 0.f};
        scene.objects.at(0).transform.rotation = {glm::radians(180.f), 0.f, 0.f};

        // sample object
        /*scene.createObject();
        scene.objects.at(1).objectInfo.modelId = 1; // sample model
        scene.objects.at(1).objectInfo.diffuseId = 1; // sample texture
        scene.objects.at(1).objectInfo.modelMatrix = scene.objects.at(1).transform.mat4();
        scene.objects.at(1).objectInfo.normalMatrix = scene.objects.at(1).transform.normalMatrix();

        scene.objects.at(1).transform.translation = {-.5f, .5f, 0.f};
        scene.objects.at(1).transform.scale = {4.f, 4.f, 4.f};*/
    }

    void RenderSystem::createVertexBuffer(){
        // Merge all vertices into one vector and update total vertex count
        for(size_t i = 0; i < scene.models.size(); i++){
            const auto modelVertices = scene.models.at(i)->getVertices();
            vertices.reserve(modelVertices.size());
            vertices.insert(vertices.end(), modelVertices.begin(), modelVertices.end());
            totalVertexCount += static_cast<uint32_t>(scene.models.at(i)->getVertexCount());
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
            const auto modelIndices = scene.models.at(i)->getIndices();
            if(scene.models.at(i)->getIndexCount() == 0)
                continue;
            indices.reserve(modelIndices.size());
            indices.insert(indices.end(), modelIndices.begin(), modelIndices.end());
            totalIndexCount += static_cast<uint32_t>(scene.models.at(i)->getIndexCount());
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
        // TODO: sort through models that don't have indices and create commands for them and draw them separately.
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
            newIndexedIndirectCommand.firstInstance = i;
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
        objectInfoDynamicAlignment = padUniformBufferSize(sizeof(Object::ObjectInfo));
        size_t bufferSize = objectInfoDynamicAlignment * scene.objects.size();

        objectInfoBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        for(int i =0; i < objectInfoBuffers.size(); i++){
            objectInfoBuffers[i] = std::make_unique<Buffer>(
                device, 
                1, 
                bufferSize, 
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
                VK_SHARING_MODE_EXCLUSIVE, 
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            );
            objectInfoBuffers[i]->map();
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
        uint32_t descriptorCount = 2 * SwapChain::MAX_FRAMES_IN_FLIGHT; // Number of descriptor sets multiplied by frames in flight
        uint32_t textureCount = static_cast<uint32_t>(scene.textures.size());

        // Pool setup
        globalPool = std::make_unique<DescriptorPool>(device);
        globalPool->addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorCount);                // Indirect draw buffers (for gpu-created draw commands)
        globalPool->addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorCount);                // Uniform scene info (for render pipeline)
        globalPool->addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorCount);                // Object info (resource ids)
        globalPool->addPoolSize(VK_DESCRIPTOR_TYPE_SAMPLER, descriptorCount);                       // Diffuse sampler
        globalPool->addPoolSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, textureCount * descriptorCount);  // Array of textures
        globalPool->buildPool(descriptorCount); // Multiply max frames in flight by number of sets

        // Render layout setup
        renderSetLayout = std::make_unique<DescriptorSetLayout>(device);
        renderSetLayout->addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, 0);             // Uniform scene info
        renderSetLayout->addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, 1);             // Object info
        renderSetLayout->addBinding(1, VK_DESCRIPTOR_TYPE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2);                    // Diffuse sampler
        renderSetLayout->addBinding(textureCount, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT, 3);   // Array of textures
        renderSetLayout->buildLayout();

        // Compute cull layout setup
        cullSetLayout = std::make_unique<DescriptorSetLayout>(device);
        cullSetLayout->addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 0);    // Uniform scene info
        cullSetLayout->addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1);    // Object info
        cullSetLayout->addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2);    // Indirect draw data
        cullSetLayout->buildLayout();

        // Descriptor image & buffer infos
        VkDescriptorBufferInfo sceneUniformBufferInfo = sceneUniformBuffers[0]->descriptorInfo();
        VkDescriptorBufferInfo indirectCommandsBufferInfo = indirectCommandsBuffers[0]->descriptorInfo();
        VkDescriptorBufferInfo objectBufferInfo = objectInfoBuffers[0]->descriptorInfo();
        VkDescriptorImageInfo samplerImageInfo{};
        samplerImageInfo.sampler = scene.samplers.at(0)->getSampler();

        std::vector<VkDescriptorImageInfo> textureImageInfos;
        for(int i = 0; i < scene.textures.size(); i++){
            VkDescriptorImageInfo newImageInfo = scene.textures.at(i)->descriptorImageInfo();
            textureImageInfos.push_back(newImageInfo);
        }
        
        // Writes lists
        std::vector<VkWriteDescriptorSet> renderLayoutWrites {
            renderSetLayout->writeBuffer(0, &sceneUniformBufferInfo),
            renderSetLayout->writeBuffer(1, &objectBufferInfo),
            renderSetLayout->writeImage(2, &samplerImageInfo),
            renderSetLayout->writeImage(3, textureImageInfos.data())
        };
        std::vector<VkWriteDescriptorSet> cullLayoutWrites {
            cullSetLayout->writeBuffer(0, &sceneUniformBufferInfo),
            cullSetLayout->writeBuffer(1, &objectBufferInfo),
            cullSetLayout->writeBuffer(2, &indirectCommandsBufferInfo)
        };

        globalPool->addNewSets(renderSetLayout->getLayout(), renderLayoutWrites, 2);
        globalPool->addNewSets(cullSetLayout->getLayout(), cullLayoutWrites, 2);
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
        scene.sceneUniform.enableOcclusionCulling = false;

        if(scene.sceneUniform.enableFrustumCulling)
            scene.sceneUniform.viewBoundingBox = camera.createFrustumViewBounds();

        scene.sceneUniform.instanceCount = totalInstanceCount;

        sceneUniformBuffers[frameIndex]->writeToBuffer(&scene.sceneUniform);
        sceneUniformBuffers[frameIndex]->flush();

         for(int i = 0; i < scene.objects.size(); i++){
            auto obj = scene.objects.at(i);
            objectInfo.diffuseId = obj.getId();
            objectInfo.modelId = obj.getId();
            objectInfo.modelMatrix = obj.transform.mat4();
            objectInfo.normalMatrix = obj.transform.normalMatrix();
            
            objectInfoBuffers[frameIndex]->writeToBuffer(&objectInfo, objectInfoDynamicAlignment, static_cast<uint32_t>(objectInfoDynamicAlignment * i));
            objectInfoBuffers[frameIndex]->flush();
        }
    }

    void RenderSystem::drawScene(VkCommandBuffer commandBuffer, uint32_t frameIndex){
        renderPipeline->bind(commandBuffer);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipelineLayout, 0, 1, &globalPool->getSets()[frameIndex], 0, nullptr);

        VkBuffer vertexBuffer[] = {globalVertexBuffer->getBuffer()};
        VkBuffer objectInfoBuffer[] = {objectInfoBuffers[frameIndex]->getBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffer, offsets);
        vkCmdBindVertexBuffers(commandBuffer, 1, 1, objectInfoBuffer, offsets);

        vkCmdBindIndexBuffer(commandBuffer, globalIndexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexedIndirect(commandBuffer, indirectCommandsBuffers[frameIndex]->getBuffer(), 0, static_cast<uint32_t>(indirectCommands.size()), sizeof(VkDrawIndexedIndirectCommand));
    }

    void RenderSystem::cullScene(VkCommandBuffer commandBuffer, uint32_t frameIndex){
        assert(cullPipeline != nullptr && "Cannot run GPU-based culling without compute pipeline.");

        cullPipeline->bind(commandBuffer);

        uint32_t setIndex = (frameIndex + SwapChain::MAX_FRAMES_IN_FLIGHT) * 1; // Multiply by number of groups of uniform sets that come beforehand
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipelineLayout, 0, 1, &globalPool->getSets()[setIndex], 0, nullptr);
        
        vkCmdDispatch(commandBuffer, indirectCommands.size() / 64, 1, 1);
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