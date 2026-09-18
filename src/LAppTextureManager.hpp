/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <map>
#include <string>
#include <GL/glew.h>
#include <Type/csmVector.hpp>

#include "LAppTextureManager_Common.hpp"

/**
* @brief テクスチャ管理クラス
*
* 画像読み込み、管理を行うクラス。
*/
class LAppTextureManager : public LAppTextureManager_Common
{
public:
    /**
    * @brief コンストラクタ
    */
    LAppTextureManager();

    /**
    * @brief デストラクタ
    *
    */
    ~LAppTextureManager();

    /**
    * @brief 画像読み込み
    *
    * 同じファイルは2回目以降アップロードせず、参照カウントだけ増やす。
    * 呼び出し側（LAppModel）は破棄時に ReleaseTexture(fileName) で
    * 参照を返すこと。カウントが0になったテクスチャだけGLから消える。
    *
    * @param[in] fileName  読み込む画像ファイルパス名
    * @return 画像情報。読み込み失敗時はNULLを返す
    */
    TextureInfo* CreateTextureFromPngFile(std::string fileName);

    /**
    * @brief 画像の解放
    *
    * 配列に存在する画像全てを解放する
    */
    void ReleaseTextures();

    /**
     * @brief 画像の解放
     *
     * 指定したテクスチャIDの画像を解放する
     * @param[in] textureId  解放するテクスチャID
     **/
    void ReleaseTexture(Csm::csmUint32 textureId);

    /**
    * @brief 画像の解放
    *
    * 指定した名前の画像を解放する
    * @param[in] fileName  解放する画像ファイルパス名
    **/
    void ReleaseTexture(std::string fileName);

    /**
     * @brief 現在ロード中のテクスチャ数（参照カウント0のものは含まない）。
     *        モデル切替でのリーク確認用。
     */
    Csm::csmUint32 GetLoadedTextureCount() const { return _texturesInfo.GetSize(); }

private:
    /// ファイル名→参照カウント。CreateTextureFromPngFile で+1、
    /// ReleaseTexture(fileName) で-1。0で実解放。
    std::map<std::string, int> _refCounts;
};
