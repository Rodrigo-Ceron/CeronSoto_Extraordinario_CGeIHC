#pragma once

#include <vector>
#include <string>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

#include "AnimationStructs.h"   // BoneInfo, AssimpNodeData
#include "Animation.h"

class Animator
{
public:
    Animator(Animation* animation)
        : m_CurrentAnimation(animation), m_CurrentTime(0.0f), m_DeltaTime(0.0f)
    {
        m_FinalBoneMatrices.reserve(100);
        for (int i = 0; i < 100; i++)
            m_FinalBoneMatrices.push_back(glm::mat4(1.0f));
    }

    void UpdateAnimation(float dt)
    {
        m_DeltaTime = dt;
        if (m_CurrentAnimation) {
            m_CurrentTime += m_CurrentAnimation->GetTicksPerSecond() * dt;
            m_CurrentTime  = fmod(m_CurrentTime, m_CurrentAnimation->GetDuration());
            CalculateBoneTransform(&m_CurrentAnimation->GetRootNode(), glm::mat4(1.0f));
        }
    }

    void PlayAnimation(Animation* pAnimation)
    {
        m_CurrentAnimation = pAnimation;
        m_CurrentTime      = 0.0f;
    }

    void Reset()
    {
        m_CurrentTime = 0.0f;
        if (m_CurrentAnimation)
            CalculateBoneTransform(&m_CurrentAnimation->GetRootNode(), glm::mat4(1.0f));
    }

    const std::vector<glm::mat4>& GetFinalBoneMatrices() const
    {
        return m_FinalBoneMatrices;
    }

private:
    void CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform)
    {
        const std::string& nodeName     = node->name;
        glm::mat4          nodeTransform = node->transformation;

        Bone* bone = m_CurrentAnimation->FindBone(nodeName);
        if (bone) {
            bone->Update(m_CurrentTime);
            nodeTransform = bone->GetLocalTransform();
        }

        glm::mat4 globalTransform = parentTransform * nodeTransform;

        const auto& boneInfoMap = m_CurrentAnimation->GetBoneIDMap();
        auto it = boneInfoMap.find(nodeName);
        if (it != boneInfoMap.end()) {
            int       index  = it->second.id;
            glm::mat4 offset = it->second.offset;
            if (index < (int)m_FinalBoneMatrices.size())
                m_FinalBoneMatrices[index] = globalTransform * offset;
        }

        for (int i = 0; i < node->childrenCount; i++)
            CalculateBoneTransform(&node->children[i], globalTransform);
    }

    std::vector<glm::mat4> m_FinalBoneMatrices;
    Animation*             m_CurrentAnimation;
    float                  m_CurrentTime;
    float                  m_DeltaTime;
};
