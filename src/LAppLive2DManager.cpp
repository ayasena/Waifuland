/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppLive2DManager.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <string>
#include <GL/glew.h>

#include <Rendering/CubismRenderer.hpp>
#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>
#include <cmath>
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "LAppConfig.hpp"
#include "LAppDelegate.hpp"
#include "LAppModel.hpp"
#include "LAppView.hpp"
#include "LAppSprite.hpp"

using namespace Csm;
using namespace LAppDefine;

namespace {
    LAppLive2DManager* s_instance = NULL;

    // Normalized-screen-space spacing between auto-placed characters (see
    // LAppLive2DManager::ReflowAutoLayout). The window's screen space spans
    // roughly [-1, 1] (LAppDefine::ViewLogicalLeft/Right), so this fits a
    // handful of characters side by side without excessive overlap.
    const csmFloat32 CharacterRowSpacing = 0.6f;

    void BeganMotion(ACubismMotion* self)
    {
        LAppPal::PrintLogLn("Motion began: %x", self);
    }

    void FinishedMotion(ACubismMotion* self)
    {
        LAppPal::PrintLogLn("Motion Finished: %x", self);
    }
}

LAppLive2DManager* LAppLive2DManager::GetInstance()
{
    if (s_instance == NULL)
    {
        s_instance = new LAppLive2DManager();
    }

    return s_instance;
}

void LAppLive2DManager::ReleaseInstance()
{
    if (s_instance != NULL)
    {
        delete s_instance;
    }

    s_instance = NULL;
}

LAppLive2DManager::LAppLive2DManager()
    : _viewMatrix(NULL)
    , _nextCharacterId(0)
{
    _viewMatrix = new CubismMatrix44();
    SetUpModel();

    if (_modelDir.GetSize() == 0)
    {
        LAppPal::PrintLogLn("[APP]No models found in: %s", LAppDefine::ModelsDir.c_str());
        LAppPal::PrintLogLn("[APP]Please place your Live2D models in the models directory.");
        LAppPal::PrintLogLn("[APP]Each model should be in its own subfolder with a .model3.json file.");
        LAppPal::PrintLogLn("[APP]Example: %s<ModelName>/<ModelName>.model3.json", LAppDefine::ModelsDir.c_str());
        LAppDelegate::GetInstance()->AppEnd();
        return;
    }

    // The initial character roster is populated by main.cpp from config
    // (LAppConfig::characters, with a fallback to a single default character)
    // once LAppDelegate::Initialize() has finished setting up rendering.
}

LAppLive2DManager::~LAppLive2DManager()
{
    ReleaseAllModel();
    delete _viewMatrix;
    Csm::Rendering::CubismOffscreenManager_OpenGLES2::ReleaseInstance();
}

void LAppLive2DManager::ReleaseAllModel()
{
    for (csmUint32 i = 0; i < _characters.GetSize(); i++)
    {
        delete _characters[i].model;
    }
    _characters.Clear();
}

void LAppLive2DManager::ScanModelsInDir(const csmString& basePath)
{
    struct dirent *entry;
    DIR *pDir = opendir(basePath.GetRawString());
    if (pDir == NULL)
    {
        LAppPal::PrintLogLn("[APP]Cannot open model directory: %s", basePath.GetRawString());
        return;
    }

    while ((entry = readdir(pDir)) != NULL)
    {
        if ((entry->d_type & DT_DIR) && strcmp(entry->d_name, "..") != 0 && strcmp(entry->d_name, ".") != 0)
        {
            struct dirent *entry2;
            csmString modelName(entry->d_name);

            csmString modelPath(basePath);
            modelPath += modelName;
            modelPath.Append(1, '/');

            DIR *pDir2 = opendir(modelPath.GetRawString());
            if (pDir2 == NULL) continue;

            while ((entry2 = readdir(pDir2)) != NULL)
            {
                const char* name = entry2->d_name;
                size_t len = strlen(name);
                const char* suffix = ".model3.json";
                size_t suffixLen = strlen(suffix);
                if (len > suffixLen && strcmp(name + len - suffixLen, suffix) == 0)
                {
                    _modelDir.PushBack(csmString(entry->d_name));
                    _modelBasePath.PushBack(basePath);
                    _modelJsonName.PushBack(csmString(name));
                    break;
                }
            }
            closedir(pDir2);
        }
    }
    closedir(pDir);
}

void LAppLive2DManager::SetUpModel()
{
    _modelDir.Clear();
    _modelBasePath.Clear();
    _modelJsonName.Clear();

    // Scan default models directory
    csmString defaultPath(LAppDefine::ModelsDir.c_str());
    ScanModelsInDir(defaultPath);

    // Scan additional model directories from config
    const LAppConfig& config = LAppConfig::GetInstance();
    for (size_t i = 0; i < config.additionalModelDirs.size(); i++)
    {
        std::string dir = config.additionalModelDirs[i];
        if (!dir.empty() && dir.back() != '/') dir += "/";

        // Resolve absolute path
        char resolved[PATH_MAX];
        if (realpath(dir.c_str(), resolved) != NULL)
        {
            dir = std::string(resolved) + "/";
        }

        csmString additionalPath(dir.c_str());
        LAppPal::PrintLogLn("[APP]Scanning additional model dir: %s", additionalPath.GetRawString());
        ScanModelsInDir(additionalPath);
    }

    // Sort model list (and keep base paths in sync) - simple bubble sort
    for (csmInt32 i = 0; i < (csmInt32)_modelDir.GetSize() - 1; i++)
    {
        for (csmInt32 j = 0; j < (csmInt32)_modelDir.GetSize() - 1 - i; j++)
        {
            if (strcmp(_modelDir[j].GetRawString(), _modelDir[j + 1].GetRawString()) > 0)
            {
                // Swap modelDir
                csmString tmpDir = _modelDir[j];
                _modelDir[j] = _modelDir[j + 1];
                _modelDir[j + 1] = tmpDir;
                // Swap basePath
                csmString tmpBase = _modelBasePath[j];
                _modelBasePath[j] = _modelBasePath[j + 1];
                _modelBasePath[j + 1] = tmpBase;
                // Swap jsonName
                csmString tmpJson = _modelJsonName[j];
                _modelJsonName[j] = _modelJsonName[j + 1];
                _modelJsonName[j + 1] = tmpJson;
            }
        }
    }
}

csmVector<csmString> LAppLive2DManager::GetModelDir() const
{
    return _modelDir;
}

csmInt32 LAppLive2DManager::GetModelDirSize() const
{
    return _modelDir.GetSize();
}

LAppModel* LAppLive2DManager::CreateModelInstance(csmInt32 modelDirIndex) const
{
    if (modelDirIndex < 0 || modelDirIndex >= (csmInt32)_modelDir.GetSize()) return NULL;

    const csmString& modelName = _modelDir[modelDirIndex];
    LAppPal::PrintLogLn("[APP]loading model: %s", modelName.GetRawString());

    csmString modelPath(_modelBasePath[modelDirIndex]);
    modelPath += modelName;
    modelPath.Append(1, '/');

    const csmString& modelJsonName = _modelJsonName[modelDirIndex];

    LAppModel* instance = new LAppModel();
    instance->LoadAssets(modelPath.GetRawString(), modelJsonName.GetRawString());
    return instance;
}

LAppLive2DManager::CharacterSlot* LAppLive2DManager::FindSlot(int characterId)
{
    for (csmUint32 i = 0; i < _characters.GetSize(); i++)
    {
        if (_characters[i].id == characterId) return &_characters[i];
    }
    return NULL;
}

const LAppLive2DManager::CharacterSlot* LAppLive2DManager::FindSlot(int characterId) const
{
    for (csmUint32 i = 0; i < _characters.GetSize(); i++)
    {
        if (_characters[i].id == characterId) return &_characters[i];
    }
    return NULL;
}

void LAppLive2DManager::ReflowAutoLayout()
{
    csmInt32 count = (csmInt32)_characters.GetSize();
    for (csmInt32 i = 0; i < count; i++)
    {
        CharacterSlot& slot = _characters[i];
        if (!slot.autoPositioned) continue;

        slot.posX = (static_cast<float>(i) - (count - 1) * 0.5f) * CharacterRowSpacing;
        slot.posY = 0.0f;

        if (slot.model)
        {
            slot.model->SetCharacterOffset(slot.posX, slot.posY);
        }
    }
}

int LAppLive2DManager::AddCharacter(csmInt32 modelDirIndex, bool hasPosition, csmFloat32 x, csmFloat32 y, csmFloat32 scale)
{
    LAppModel* model = CreateModelInstance(modelDirIndex);
    if (model == NULL) return -1;

    CharacterSlot slot;
    slot.id = _nextCharacterId++;
    slot.modelDirIndex = modelDirIndex;
    slot.model = model;
    slot.posX = hasPosition ? x : 0.0f;
    slot.posY = hasPosition ? y : 0.0f;
    slot.scale = scale;
    slot.targetScale = scale;
    slot.autoPositioned = !hasPosition;

    _characters.PushBack(slot);

    if (hasPosition)
    {
        model->SetCharacterOffset(x, y);
    }

    ReflowAutoLayout();

    return _characters[_characters.GetSize() - 1].id;
}

bool LAppLive2DManager::RemoveCharacter(int characterId)
{
    for (csmUint32 i = 0; i < _characters.GetSize(); i++)
    {
        if (_characters[i].id == characterId)
        {
            delete _characters[i].model;
            _characters.Remove(i);
            ReflowAutoLayout();
            return true;
        }
    }
    return false;
}

csmInt32 LAppLive2DManager::GetCharacterCount() const
{
    return _characters.GetSize();
}

int LAppLive2DManager::GetCharacterIdAt(csmInt32 index) const
{
    if (index < 0 || index >= (csmInt32)_characters.GetSize()) return -1;
    return _characters[index].id;
}

LAppModel* LAppLive2DManager::GetCharacterModel(int characterId) const
{
    const CharacterSlot* slot = FindSlot(characterId);
    return slot ? slot->model : NULL;
}

csmInt32 LAppLive2DManager::GetCharacterModelDirIndex(int characterId) const
{
    const CharacterSlot* slot = FindSlot(characterId);
    return slot ? slot->modelDirIndex : -1;
}

csmFloat32 LAppLive2DManager::GetCharacterX(int characterId) const
{
    const CharacterSlot* slot = FindSlot(characterId);
    return slot ? slot->posX : 0.0f;
}

csmFloat32 LAppLive2DManager::GetCharacterY(int characterId) const
{
    const CharacterSlot* slot = FindSlot(characterId);
    return slot ? slot->posY : 0.0f;
}

csmFloat32 LAppLive2DManager::GetCharacterZoom(int characterId) const
{
    const CharacterSlot* slot = FindSlot(characterId);
    return slot ? slot->targetScale : 1.0f;
}

void LAppLive2DManager::SetCharacterModel(int characterId, csmInt32 modelDirIndex)
{
    CharacterSlot* slot = FindSlot(characterId);
    if (slot == NULL) return;

    LAppModel* newModel = CreateModelInstance(modelDirIndex);
    if (newModel == NULL) return;

    delete slot->model;
    slot->model = newModel;
    slot->modelDirIndex = modelDirIndex;
    newModel->SetCharacterOffset(slot->posX, slot->posY);
}

void LAppLive2DManager::NextCharacterModel(int characterId)
{
    csmInt32 size = GetModelDirSize();
    if (size == 0) return;

    CharacterSlot* slot = FindSlot(characterId);
    if (slot == NULL) return;

    csmInt32 next = (slot->modelDirIndex + 1) % size;
    SetCharacterModel(characterId, next);
}

void LAppLive2DManager::PrevCharacterModel(int characterId)
{
    csmInt32 size = GetModelDirSize();
    if (size == 0) return;

    CharacterSlot* slot = FindSlot(characterId);
    if (slot == NULL) return;

    csmInt32 prev = (slot->modelDirIndex - 1 + size) % size;
    SetCharacterModel(characterId, prev);
}

void LAppLive2DManager::SetCharacterPosition(int characterId, csmFloat32 x, csmFloat32 y)
{
    CharacterSlot* slot = FindSlot(characterId);
    if (slot == NULL) return;

    slot->posX = x;
    slot->posY = y;
    slot->autoPositioned = false;

    if (slot->model)
    {
        slot->model->SetCharacterOffset(x, y);
    }
}

void LAppLive2DManager::SetCharacterZoom(int characterId, csmFloat32 scale)
{
    CharacterSlot* slot = FindSlot(characterId);
    if (slot == NULL) return;

    // Eased toward each frame in OnUpdate(), same as the old single-character
    // _targetModelScale/_modelScale pair — avoids a "pop" when a trackpad
    // fires many small scroll deltas in a row.
    slot->targetScale = scale;
}

void LAppLive2DManager::SwitchSkin(int characterId)
{
    CharacterSlot* slot = FindSlot(characterId);
    if (slot && slot->model)
    {
        slot->model->SwitchSkin();
    }
}

int LAppLive2DManager::HitTestCharacter(csmFloat32 x, csmFloat32 y) const
{
    for (csmUint32 i = 0; i < _characters.GetSize(); i++)
    {
        LAppModel* model = _characters[i].model;
        if (model == NULL) continue;

        // HitTest inverts only _modelMatrix; zoom is applied outside it, in
        // OnUpdate()'s per-frame projection (see SetCharacterOffset comment).
        // Undo that scale here so hit-testing matches what's actually drawn
        // at any zoom level, instead of drifting off the model as it grows.
        csmFloat32 scale = _characters[i].scale;
        if (scale <= 0.0f) scale = 1.0f;
        csmFloat32 sx = x / scale;
        csmFloat32 sy = y / scale;

        if (model->HitTest(HitAreaNameHead, sx, sy) || model->HitTest(HitAreaNameBody, sx, sy))
        {
            return _characters[i].id;
        }
    }
    return -1;
}

LAppModel* LAppLive2DManager::GetModel(csmUint32 no) const
{
    if (no < _characters.GetSize())
    {
        return _characters[no].model;
    }

    return NULL;
}

csmUint32 LAppLive2DManager::GetModelNum() const
{
    return _characters.GetSize();
}

void LAppLive2DManager::SetRenderTargetSize(csmUint32 width, csmUint32 height)
{
    for (csmUint32 i = 0; i < _characters.GetSize(); i++)
    {
        LAppModel* model = _characters[i].model;
        if (model)
        {
            model->SetRenderTargetSize(width, height);
        }
    }
}

void LAppLive2DManager::OnDrag(csmFloat32 x, csmFloat32 y) const
{
    // Continuous "look at cursor" — applied to every character; each has its
    // own drag state (base class CubismUserModel), so this doesn't fight
    // between characters.
    for (csmUint32 i = 0; i < _characters.GetSize(); i++)
    {
        LAppModel* model = _characters[i].model;
        if (model)
        {
            model->SetDragging(x, y);
        }
    }
}

void LAppLive2DManager::OnTap(csmFloat32 x, csmFloat32 y)
{
    if (DebugLogEnable)
    {
        LAppPal::PrintLogLn("[APP]tap point: {x:%.2f y:%.2f}", x, y);
    }

    for (csmUint32 i = 0; i < _characters.GetSize(); i++)
    {
        LAppModel* model = _characters[i].model;
        if (model == NULL) continue;

        // Same zoom compensation as HitTestCharacter() — see comment there.
        csmFloat32 scale = _characters[i].scale;
        if (scale <= 0.0f) scale = 1.0f;
        csmFloat32 sx = x / scale;
        csmFloat32 sy = y / scale;

        if (model->HitTest(HitAreaNameHead, sx, sy))
        {
            if (DebugLogEnable)
            {
                LAppPal::PrintLogLn("[APP]hit area: [%s]", HitAreaNameHead);
            }
            model->SetRandomExpression();
        }
        else if (model->HitTest(HitAreaNameBody, sx, sy))
        {
            if (DebugLogEnable)
            {
                LAppPal::PrintLogLn("[APP]hit area: [%s]", HitAreaNameBody);
            }
            model->StartRandomMotion(MotionGroupTapBody, PriorityNormal, FinishedMotion, BeganMotion);
            model->SetRandomExpression();
        }
    }
}

void LAppLive2DManager::OnUpdate()
{
    int width, height;
    width = LAppDelegate::GetInstance()->GetWindowWidth(); height = LAppDelegate::GetInstance()->GetWindowHeight();

    // モデルで使用するオフスクリーン管理の開始処理
    Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->BeginFrameProcess();

    csmFloat32 deltaTime = LAppPal::GetDeltaTime();
    csmUint32 characterCount = _characters.GetSize();
    for (csmUint32 i = 0; i < characterCount; ++i)
    {
        CharacterSlot& slot = _characters[i];
        CubismMatrix44 projection;
        LAppModel* model = slot.model;

        if (model == NULL || model->GetModel() == NULL)
        {
            LAppPal::PrintLogLn("Failed to model->GetModel().");
            continue;
        }

        if (model->GetModel()->GetCanvasWidth() > 1.0f && width < height)
        {
            model->GetModelMatrix()->SetWidth(2.0f);
            projection.Scale(1.0f, static_cast<float>(width) / static_cast<float>(height));
        }
        else
        {
            projection.Scale(static_cast<float>(height) / static_cast<float>(width), 1.0f);
        }

        // Per-character zoom, eased toward targetScale and applied to the
        // per-frame projection (not to the model's own persistent matrix —
        // see LAppModel::SetCharacterOffset). Position is NOT applied here:
        // it's already baked into the model's own matrix via
        // SetCharacterOffset, which keeps hit-testing (which only inverts
        // through that matrix) in sync with what's drawn.
        slot.scale += (slot.targetScale - slot.scale) * (1.0f - std::exp(-15.0f * deltaTime));
        projection.ScaleRelative(slot.scale, slot.scale);

        if (_viewMatrix != NULL)
        {
            projection.MultiplyByMatrix(_viewMatrix);
        }

        LAppDelegate::GetInstance()->GetView()->PreModelDraw(*model);

        model->Update();
        model->Draw(projection);///< 参照渡しなのでprojectionは変質する

        LAppDelegate::GetInstance()->GetView()->PostModelDraw(*model);
    }

    // モデルで使用するオフスクリーン管理の終了処理
    Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->EndFrameProcess();
    // もし余っているオフスクリーンのリソースを解放したい場合行う処理
    Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->ReleaseStaleRenderTextures();
}

void LAppLive2DManager::SetViewMatrix(CubismMatrix44* m)
{
    for (int i = 0; i < 16; i++) {
        _viewMatrix->GetArray()[i] = m->GetArray()[i];
    }
}
