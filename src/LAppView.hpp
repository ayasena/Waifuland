/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <Math/CubismMatrix44.hpp>
#include <Math/CubismViewMatrix.hpp>
#include "CubismFramework.hpp"

#include "LAppView_Common.hpp"

class TouchManager_Common;

/**
* @brief 描画クラス。実描画は LAppLive2DManager::OnUpdate() が担い、
*        このクラスはタッチ座標変換と view matrix 保持のみ行う。
*/
class LAppView : public LAppView_Common
{
public:

    /**
    * @brief コンストラクタ
    */
    LAppView();

    /**
    * @brief デストラクタ
    */
    ~LAppView();

    /**
    * @brief 初期化する。
    */
    virtual void Initialize(int width, int height) override;

    /**
    * @brief 描画する。
    */
    void Render();

    /**
     * @brief 論理座標（TransformScreenX/Y の出力側）をウィンドウピクセルに
     *        戻す。入力リージョン計算用。
     */
    void ScreenToDevice(float screenX, float screenY, float* deviceX, float* deviceY) const;

    /**
     * @brief タッチされたときに呼ばれる。
     *
     * @param[in]       pointX            スクリーンX座標
     * @param[in]       pointY            スクリーンY座標
     */
    void OnTouchesBegan(float pointX, float pointY) const;

    /**
    * @brief タッチしているときにポインタが動いたら呼ばれる。
    *
    * @param[in]       pointX            スクリーンX座標
    * @param[in]       pointY            スクリーンY座標
    */
    void OnTouchesMoved(float pointX, float pointY) const;

    /**
    * @brief タッチが終了したら呼ばれる。
    *
    * @param[in]       pointX            スクリーンX座標
    * @param[in]       pointY            スクリーンY座標
    */
    void OnTouchesEnded(float pointX, float pointY) const;

private:
    TouchManager_Common* _touchManager;                 ///< タッチマネージャー
};
