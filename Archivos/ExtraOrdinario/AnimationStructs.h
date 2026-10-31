#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>

// -------------------------------------------------------
// Informacion de cada hueso: id en el shader + offsetMatrix
// Definido UNA SOLA VEZ aqui, todos los demas lo incluyen
// -------------------------------------------------------
struct BoneInfo
{
    int       id;
    glm::mat4 offset;
};

// -------------------------------------------------------
// Nodo de jerarquia (replica aiNode sin depender de Assimp)
// -------------------------------------------------------
struct AssimpNodeData
{
    glm::mat4                    transformation;
    std::string                  name;
    int                          childrenCount;
    std::vector<AssimpNodeData>  children;
};
