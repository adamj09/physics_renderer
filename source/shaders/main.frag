#version 460

layout(location = 0) in vec3 inFragColor;
layout(location = 1) in vec3 inFragPosWorld;
layout(location = 2) in vec3 inFragNormalWorld;
layout(location = 3) in vec2 inFragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sceneUbo{
  mat4 projection;
  mat4 view;
  mat4 inverseView;
} globalUBO;

layout(set = 0, binding = 1) uniform modelDubo{
  mat4 modelMatrix;
  mat4 normalMatrix;
  uint textureIndex;
} model;

layout(set = 0, binding = 2) uniform sampler texSampler;
layout(set = 0, binding = 3) uniform texture2D textures[1];

void main(){
    vec3 cameraPosWorld = globalUBO.inverseView[3].xyz;
    vec3 viewDirection = normalize(cameraPosWorld - inFragPosWorld);

    outColor = texture(sampler2D(textures[model.textureIndex], texSampler), inFragTexCoord);
}