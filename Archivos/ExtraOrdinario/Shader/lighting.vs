#version 330 core

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 texCoords;

// --- Nuevos atributos para skeletal animation ---
layout (location = 3) in ivec4 boneIds;
layout (location = 4) in vec4  weights;

out vec3 Normal;
out vec3 FragPos;
out vec2 TexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// --- Nuevos uniforms para skeletal animation ---
uniform int  useBones;                // 1 = animacion activa, 0 = modelo estatico
uniform mat4 finalBonesMatrices[100]; // transformaciones finales de cada hueso

void main()
{
    vec4 skinnedPosition = vec4(0.0);
    vec3 skinnedNormal   = vec3(0.0);

    if (useBones == 1)
    {
        for (int i = 0; i < 4; i++)
        {
            if (boneIds[i] == -1) continue;

            if (boneIds[i] >= 100)
            {
                skinnedPosition = vec4(position, 1.0);
                skinnedNormal   = normal;
                break;
            }

            skinnedPosition += finalBonesMatrices[boneIds[i]] * vec4(position, 1.0) * weights[i];
            skinnedNormal   += mat3(finalBonesMatrices[boneIds[i]]) * normal * weights[i];
        }
    }
    else
    {
        // Comportamiento original sin animacion
        skinnedPosition = vec4(position, 1.0);
        skinnedNormal   = normal;
    }

    gl_Position = projection * view * model * skinnedPosition;
    FragPos     = vec3(model * skinnedPosition);
    Normal      = mat3(transpose(inverse(model))) * skinnedNormal;
    TexCoords   = texCoords;
}
