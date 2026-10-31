#pragma once

#include <string>
#include <vector>
#include <map>
#include <cassert>
#include <algorithm>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "AnimationStructs.h"   // BoneInfo, AssimpNodeData
#include "AssimpGLMHelpers.h"
#include "Bone.h"

// Forward declaration — evita incluir Model.h aqui
class Model;

class Animation
{
public:
    Animation() = default;

    Animation(const std::string& animationPath, Model* model);

    ~Animation() {}

    Bone* FindBone(const std::string& name)
    {
        auto it = std::find_if(m_Bones.begin(), m_Bones.end(),
            [&](const Bone& b){ return b.GetBoneName() == name; });
        return (it == m_Bones.end()) ? nullptr : &(*it);
    }

    float GetTicksPerSecond() const { return (float)m_TicksPerSecond; }
    float GetDuration()       const { return m_Duration; }
    const AssimpNodeData&                  GetRootNode()  const { return m_RootNode; }
    const std::map<std::string, BoneInfo>& GetBoneIDMap() const { return m_BoneInfoMap; }

private:
    void ReadHierarchyData(AssimpNodeData& dest, const aiNode* src)
    {
        assert(src);
        dest.name           = src->mName.data;
        dest.transformation = AssimpGLMHelpers::ConvertMatrixToGLMFormat(src->mTransformation);
        dest.childrenCount  = src->mNumChildren;
        for (unsigned int i = 0; i < src->mNumChildren; i++) {
            AssimpNodeData child;
            ReadHierarchyData(child, src->mChildren[i]);
            dest.children.push_back(child);
        }
    }

    void ReadMissingBones(const aiAnimation* animation, Model& model);

    float  m_Duration       = 0.f;
    double m_TicksPerSecond = 0.0;
    std::vector<Bone>               m_Bones;
    AssimpNodeData                  m_RootNode;
    std::map<std::string, BoneInfo> m_BoneInfoMap;
};

// Incluimos Model.h DESPUES de declarar Animation
// para que Model pueda exponer GetBoneInfoMap/GetBoneCount
#include "Model.h"

inline Animation::Animation(const std::string& animationPath, Model* model)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(animationPath, aiProcess_Triangulate);
    assert(scene && scene->mRootNode && scene->mNumAnimations > 0);

    auto* anim       = scene->mAnimations[0];
    m_Duration       = (float)anim->mDuration;
    m_TicksPerSecond = (anim->mTicksPerSecond != 0.0) ? anim->mTicksPerSecond : 25.0;

    ReadHierarchyData(m_RootNode, scene->mRootNode);
    ReadMissingBones(anim, *model);
}

inline void Animation::ReadMissingBones(const aiAnimation* animation, Model& model)
{
    auto& boneInfoMap = model.GetBoneInfoMap();
    int&  boneCount   = model.GetBoneCount();

    for (unsigned int i = 0; i < animation->mNumChannels; i++) {
        auto*       channel  = animation->mChannels[i];
        std::string boneName = channel->mNodeName.data;

        if (boneInfoMap.find(boneName) == boneInfoMap.end()) {
            BoneInfo bi;
            bi.id     = boneCount++;
            bi.offset = glm::mat4(1.0f);
            boneInfoMap[boneName] = bi;
        }
        m_Bones.emplace_back(boneName, boneInfoMap[boneName].id, channel);
    }
    m_BoneInfoMap = boneInfoMap;
}
