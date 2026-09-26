/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <string>
#include <vector>
#include <CubismFramework.hpp>
#include <ICubismModelSetting.hpp>
#include <Type/csmRectF.hpp>

#include "LAppWavFileHandler_Common.hpp"
#include "LAppModel_Common.hpp"

/**
 * @brief Per-frame voice input for one character.
 *
 * Produced by LAppLive2DManager (the only owner of per-character voice
 * state) and consumed by LAppModel::Update. Passing it by value keeps the
 * model free of IPC singletons: the model renders what it is given.
 */
struct VoiceSample
{
    bool hasMouth;
    Csm::csmFloat32 mouthY; ///< 0.0 (closed) .. 1.0 (fully open)
    bool hasForm;
    Csm::csmFloat32 mouthForm; ///< -1.0 (round: o, u) .. 1.0 (wide: i, e); ParamMouthForm
    bool hasLook;
    Csm::csmFloat32 lookX;
    Csm::csmFloat32 lookY;
};

/**
 * @brief ユーザーが実際に使用するモデルの実装クラス<br>
 *         モデル生成、機能コンポーネント生成、更新処理とレンダリングの呼び出しを行う。
 *
 */
class LAppModel : public LAppModel_Common
{
public:
    /**
     * @brief Switch to the next skin by cycling through motion groups
     */
    void SwitchSkin();

    /**
     * @brief コンストラクタ
     */
    LAppModel();

    /**
     * @brief デストラクタ
     *
     */
    virtual ~LAppModel();

    /**
     * @brief model3.jsonが置かれたディレクトリとファイルパスからモデルを生成する
     *
     */
    void LoadAssets(const Csm::csmChar* dir, const  Csm::csmChar* fileName);

    /**
     * @brief レンダラを再構築する
     *
     */
    void ReloadRenderer();

    /**
     * @brief   モデルの更新処理。モデルのパラメータから描画状態を決定する。
     *
     * @param[in]   voice   このキャラクター向けの声サンプル（口・視線）。
     *                      hasMouth/hasLook が false の項目は触らない。
     */
    void Update(const VoiceSample& voice);

    /**
     * @brief   モデルを描画する処理。モデルを描画する空間のView-Projection行列を渡す。
     *
     * @param[in]  matrix  View-Projection行列
     */
    void Draw(Csm::CubismMatrix44& matrix);

    /**
     * @brief   引数で指定したモーションの再生を開始する。
     *
     * @param[in]   group                       モーショングループ名
     * @param[in]   no                          グループ内の番号
     * @param[in]   priority                    優先度
     * @param[in]   onFinishedMotionHandler     モーション再生終了時に呼び出されるコールバック関数。NULLの場合、呼び出されない。
     * @param[in]   onBeganMotionHandler        モーション再生開始時に呼び出されるコールバック関数。NULLの場合、呼び出されない。
     * @return                                  開始したモーションの識別番号を返す。個別のモーションが終了したか否かを判定するIsFinished()の引数で使用する。開始できない時は「-1」
     */
    Csm::CubismMotionQueueEntryHandle StartMotion(const Csm::csmChar* group, Csm::csmInt32 no, Csm::csmInt32 priority, Csm::ACubismMotion::FinishedMotionCallback onFinishedMotionHandler = NULL, Csm::ACubismMotion::BeganMotionCallback onBeganMotionHandler = NULL);

    /**
     * @brief   ランダムに選ばれたモーションの再生を開始する。
     *
     * @param[in]   group                       モーショングループ名
     * @param[in]   priority                    優先度
     * @param[in]   onFinishedMotionHandler     モーション再生終了時に呼び出されるコールバック関数。NULLの場合、呼び出されない。
     * @param[in]   onBeganMotionHandler        モーション再生開始時に呼び出されるコールバック関数。NULLの場合、呼び出されない。
     * @return                                  開始したモーションの識別番号を返す。個別のモーションが終了したか否かを判定するIsFinished()の引数で使用する。開始できない時は「-1」
     */
    Csm::CubismMotionQueueEntryHandle StartRandomMotion(const Csm::csmChar* group, Csm::csmInt32 priority, Csm::ACubismMotion::FinishedMotionCallback onFinishedMotionHandler = NULL, Csm::ACubismMotion::BeganMotionCallback onBeganMotionHandler = NULL);

    /**
     * @brief   引数で指定した表情モーションをセットする
     *
     * @param   expressionID    表情モーションのID
     */
    void SetExpression(const Csm::csmChar* expressionID);

    /**
     * @brief   ランダムに選ばれた表情モーションをセットする
     *
     */
    void SetRandomExpression();

    /**
    * @brief   イベントの発火を受け取る
    *
    */
    virtual void MotionEventFired(const Live2D::Cubism::Framework::csmString& eventValue);

    /**
     * @brief    当たり判定テスト。<br>
     *            指定IDの頂点リストから矩形を計算し、座標が矩形範囲内か判定する。
     *
     * @param[in]   hitAreaName     当たり判定をテストする対象のID
     * @param[in]   x               判定を行うX座標
     * @param[in]   y               判定を行うY座標
     */
    virtual Csm::csmBool HitTest(const Csm::csmChar* hitAreaName, Csm::csmFloat32 x, Csm::csmFloat32 y);

    /**
     * @brief    キャラクター全体の当たり判定。<br>
     *           Head/Body の指定ヒットエリアではなく、全ドローアブルの
     *           シルエットで判定する。ドラッグやズームの掴み判定用
     *           （HitTestCharacter）。タップの表情/モーション振り分けは
     *           HitTest のまま（作者の意図した部位意味を保つ）。
     *
     * @param[in]   x               判定を行うX座標
     * @param[in]   y               判定を行うY座標
     */
    Csm::csmBool HitTestAnywhere(Csm::csmFloat32 x, Csm::csmFloat32 y) const;

    /**
     * @brief   Get all expression IDs for IPC queries.
     */
    std::vector<std::string> GetExpressionIds() const;

    /**
     * @brief   Motion info for IPC queries.
     */
    struct MotionInfo {
        std::string group;
        int index;
        std::string file;
    };

    /**
     * @brief   Get all available motions for IPC queries.
     */
    std::vector<MotionInfo> GetMotionList() const;

    /**
     * @brief   Get the model setting (for IPC to query model capabilities).
     */
    Csm::ICubismModelSetting* GetModelSetting() const { return _modelSetting; }

    /**
     * @brief   全ドローアブルの頂点範囲の和をモデル空間で返す。
     *          入力リージョン（LAppLive2DManager::GetCharacterPixelRects）と
     *          当たり判定が同じ _modelMatrix 状態を参照するためのもの。
     * @return  ジオメトリがなければfalse。
     */
    bool GetModelSpaceBounds(Csm::csmFloat32& left, Csm::csmFloat32& top,
                             Csm::csmFloat32& right, Csm::csmFloat32& bottom) const;

    /**
     * @brief   Apply a per-character position offset on top of this model's own
     *          layout position (absolute, not compounding — safe to call
     *          repeatedly, e.g. once per drag event, and again after a model
     *          swap). Zoom is intentionally not handled here; it's applied to
     *          the per-frame projection matrix in LAppLive2DManager::OnUpdate()
     *          instead, so it doesn't fight the portrait-canvas SetWidth(2.0f)
     *          correction that also runs there.
     *
     * @param[in]   x       X offset in the same units as the model's own layout position
     * @param[in]   y       Y offset in the same units as the model's own layout position
     */
    void SetCharacterOffset(Csm::csmFloat32 x, Csm::csmFloat32 y);

protected:
    /**
     *  @brief  モデルを描画する処理。モデルを描画する空間のView-Projection行列を渡す。
     *
     */
    void DoDraw();

private:
    /**
     * @brief model3.jsonからモデルを生成する。<br>
     *         model3.jsonの記述に従ってモデル生成、モーション、物理演算などのコンポーネント生成を行う。
     *
     * @param[in]   setting     ICubismModelSettingのインスタンス
     *
     */
    void SetupModel(Csm::ICubismModelSetting* setting);

    /**
     * @brief OpenGLのテクスチャユニットにテクスチャをロードする
     *
     */
    void SetupTextures();
    Csm::csmVector<Csm::ACubismMotion*> _autoMotions;

    /**
     * @brief   モーションデータをグループ名から一括でロードする。<br>
     *           モーションデータの名前は内部でModelSettingから取得する。
     *
     * @param[in]   group  モーションデータのグループ名
     */
    void PreloadMotionGroup(const Csm::csmChar* group);

    /**
     * @brief   モーションデータをグループ名から一括で解放する。<br>
     *           モーションデータの名前は内部でModelSettingから取得する。
     *
     * @param[in]   group  モーションデータのグループ名
     */
    void ReleaseMotionGroup(const Csm::csmChar* group) const;

    /**
    * @brief すべてのモーションデータの解放
    *
    * すべてのモーションデータを解放する。
    */
    void ReleaseMotions();

    /**
    * @brief すべての表情データの解放
    *
    * すべての表情データを解放する。
    */
    void ReleaseExpressions();

    Csm::ICubismModelSetting* _modelSetting; ///< モデルセッティング情報
    Csm::csmString _modelHomeDir; ///< モデルセッティングが置かれたディレクトリ
    Csm::csmFloat32 _userTimeSeconds; ///< デルタ時間の積算値[秒]
    Csm::csmVector<Csm::CubismIdHandle> _eyeBlinkIds; ///< モデルに設定されたまばたき機能用パラメータID
    Csm::csmVector<Csm::CubismIdHandle> _lipSyncIds; ///< モデルに設定されたリップシンク機能用パラメータID
    Csm::csmMap<Csm::csmString, Csm::ACubismMotion*>   _motions; ///< 読み込まれているモーションのリスト
    Csm::csmMap<Csm::csmString, Csm::ACubismMotion*>   _expressions; ///< 読み込まれている表情のリスト
    Csm::csmVector<Csm::csmRectF> _hitArea;
    Csm::csmVector<Csm::csmRectF> _userArea;
    const Csm::CubismId* _idParamAngleX; ///< パラメータID: ParamAngleX
    const Csm::CubismId* _idParamAngleY; ///< パラメータID: ParamAngleX
    const Csm::CubismId* _idParamAngleZ; ///< パラメータID: ParamAngleX
    const Csm::CubismId* _idParamBodyAngleX; ///< パラメータID: ParamBodyAngleX    
    
    const Csm::CubismId* _idParamBodyAngleY; ///< パラメータID: ParamBodyAngleY
    
    const Csm::CubismId* _idParamBodyAngleZ; ///< パラメータID: ParamBodyAngleZ
    const Csm::CubismId* _idParamEyeBallX; ///< パラメータID: ParamEyeBallX
    const Csm::CubismId* _idParamEyeBallY; ///< パラメータID: ParamEyeBallXY

    Csm::csmBool _motionUpdated; ///< モーション更新フラグ
    Csm::csmInt32 _currentSkinIndex;

    Csm::csmFloat32 _lastExpressionTime; ///< Time when the last expression was set
    static const Csm::csmFloat32 ExpressionTimeoutSeconds; ///< Seconds before expression reverts to default
    Csm::csmInt32 _nextExpressionIndex; ///< Round-robin index for cycling expressions

    /// Layout-derived position captured right after LoadAssets(), used as the
    /// base that SetCharacterOffset() applies a per-character offset on top of.
    Csm::csmFloat32 _baseTranslateX;
    Csm::csmFloat32 _baseTranslateY;

    /// Texture files this model holds references to (via
    /// LAppTextureManager::CreateTextureFromPngFile). Released in the
    /// destructor and before every re-setup, so model switches never leak
    /// GL textures.
    std::vector<std::string> _boundTextureFiles;
    void ReleaseBoundTextures();

    /// All parameter IDs found across skin motion groups, used to reset before switching
    Csm::csmVector<const Csm::CubismId*> _allSkinParamIds;
    /// Default values for each skin parameter (from model defaults)
    Csm::csmVector<Csm::csmFloat32> _allSkinParamDefaults;
    void CollectSkinParams(); ///< Scan motion group JSONs to collect skin parameter IDs

    LAppWavFileHandler_Common _wavFileHandler; ///< wavファイルハンドラ
};
