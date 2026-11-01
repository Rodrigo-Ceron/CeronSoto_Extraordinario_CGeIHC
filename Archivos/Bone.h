#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <vector>
#include <string>
#include <assimp/scene.h>

#include "AssimpGLMHelpers.h"

struct KeyPosition { glm::vec3 position;    float timeStamp; };
struct KeyRotation { glm::quat orientation; float timeStamp; };
struct KeyScale    { glm::vec3 scale;       float timeStamp; };

class Bone
{
public:
    Bone(const std::string& name, int ID, const aiNodeAnim* channel)
        : m_Name(name), m_ID(ID), m_LocalTransform(1.0f)
    {
        m_NumPositions = channel->mNumPositionKeys;
        for (int i = 0; i < m_NumPositions; i++) {
            KeyPosition d;
            d.position  = AssimpGLMHelpers::GetGLMVec(channel->mPositionKeys[i].mValue);
            d.timeStamp = (float)channel->mPositionKeys[i].mTime;
            m_Positions.push_back(d);
        }
        m_NumRotations = channel->mNumRotationKeys;
        for (int i = 0; i < m_NumRotations; i++) {
            KeyRotation d;
            d.orientation = AssimpGLMHelpers::GetGLMQuat(channel->mRotationKeys[i].mValue);
            d.timeStamp   = (float)channel->mRotationKeys[i].mTime;
            m_Rotations.push_back(d);
        }
        m_NumScalings = channel->mNumScalingKeys;
        for (int i = 0; i < m_NumScalings; i++) {
            KeyScale d;
            d.scale     = AssimpGLMHelpers::GetGLMVec(channel->mScalingKeys[i].mValue);
            d.timeStamp = (float)channel->mScalingKeys[i].mTime;
            m_Scales.push_back(d);
        }
    }

    void Update(float animationTime)
    {
        m_LocalTransform = InterpolatePosition(animationTime)
                         * InterpolateRotation(animationTime)
                         * InterpolateScaling(animationTime);
    }

    glm::mat4   GetLocalTransform() const { return m_LocalTransform; }
    std::string GetBoneName()       const { return m_Name; }
    int         GetBoneID()         const { return m_ID; }

private:
    int GetPositionIndex(float t) {
        for (int i = 0; i < m_NumPositions - 1; i++)
            if (t < m_Positions[i+1].timeStamp) return i;
        return m_NumPositions - 2;
    }
    int GetRotationIndex(float t) {
        for (int i = 0; i < m_NumRotations - 1; i++)
            if (t < m_Rotations[i+1].timeStamp) return i;
        return m_NumRotations - 2;
    }
    int GetScaleIndex(float t) {
        for (int i = 0; i < m_NumScalings - 1; i++)
            if (t < m_Scales[i+1].timeStamp) return i;
        return m_NumScalings - 2;
    }
    float GetScaleFactor(float last, float next, float t) {
        return (t - last) / (next - last);
    }

    glm::mat4 InterpolatePosition(float t) {
        if (m_NumPositions == 1) return glm::translate(glm::mat4(1.0f), m_Positions[0].position);
        int i0 = GetPositionIndex(t), i1 = i0 + 1;
        float f = GetScaleFactor(m_Positions[i0].timeStamp, m_Positions[i1].timeStamp, t);
        return glm::translate(glm::mat4(1.0f), glm::mix(m_Positions[i0].position, m_Positions[i1].position, f));
    }
    glm::mat4 InterpolateRotation(float t) {
        if (m_NumRotations == 1) return glm::toMat4(glm::normalize(m_Rotations[0].orientation));
        int i0 = GetRotationIndex(t), i1 = i0 + 1;
        float f = GetScaleFactor(m_Rotations[i0].timeStamp, m_Rotations[i1].timeStamp, t);
        return glm::toMat4(glm::normalize(glm::slerp(m_Rotations[i0].orientation, m_Rotations[i1].orientation, f)));
    }
    glm::mat4 InterpolateScaling(float t) {
        if (m_NumScalings == 1) return glm::scale(glm::mat4(1.0f), m_Scales[0].scale);
        int i0 = GetScaleIndex(t), i1 = i0 + 1;
        float f = GetScaleFactor(m_Scales[i0].timeStamp, m_Scales[i1].timeStamp, t);
        return glm::scale(glm::mat4(1.0f), glm::mix(m_Scales[i0].scale, m_Scales[i1].scale, f));
    }

    std::vector<KeyPosition> m_Positions;
    std::vector<KeyRotation> m_Rotations;
    std::vector<KeyScale>    m_Scales;
    int m_NumPositions, m_NumRotations, m_NumScalings;
    glm::mat4   m_LocalTransform;
    std::string m_Name;
    int         m_ID;
};
