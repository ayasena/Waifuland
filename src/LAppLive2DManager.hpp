/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */
#pragma once

#include <map>
#include <string>
#include <CubismFramework.hpp>
#include <Math/CubismMatrix44.hpp>
#include <Type/csmVector.hpp>

class LAppModel;

/**
* @brief 複数のLive2Dキャラクターを同一ウィンドウで管理するクラス<br>
*         各キャラクターは独立したLAppModelインスタンス、位置、ズームを持つ。
*
*/
class LAppLive2DManager
{

public:
    static LAppLive2DManager* GetInstance();
    static void ReleaseInstance();

    /**
    * @brief   Resources フォルダにあるモデルフォルダ名をセットする
    *
    */
    void SetUpModel();

    /**
    * @brief   Resources フォルダにあるモデルフォルダ名を取得する（モデルカタログ、全キャラクター共通）
    *
    * 生の参照を返す。呼び出し側でコピーしないこと（IPC応答の組み立てには
    * 参照束縛で十分）。
    */
    const Csm::csmVector<Csm::csmString>& GetModelDir() const;

    /**
    * @brief   モデル名から GetModelDir() のインデックスを引く。
    * @return  見つからなければ-1。
    */
    Csm::csmInt32 FindModelIndex(const char* name) const;

    /**
    * @brief   Resources フォルダにあるモデルフォルダのサイズを取得する
    *
    */
    Csm::csmInt32 GetModelDirSize() const;

    /**
     * @brief   モデルのオフスクリーンのサイズを設定
     */
    void SetRenderTargetSize(Csm::csmUint32 width, Csm::csmUint32 height);

    /**
    * @brief   現在表示中の全キャラクターを解放する
    */
    void ReleaseAllModel();

    /**
    * @brief   画面をドラッグしたときの処理（見つめる方向の更新、全キャラクター共通）
    */
    void OnDrag(Csm::csmFloat32 x, Csm::csmFloat32 y) const;

    /**
    * @brief   画面をタップしたときの処理。ヒットしたキャラクターのみモーション/表情を発火する。
    */
    void OnTap(Csm::csmFloat32 x, Csm::csmFloat32 y);

    /**
    * @brief   画面を更新するときの処理。全キャラクターの更新処理および描画処理を行う<br>
    *           （キャラクターごとのズームをtargetScaleへ毎フレームイージングするため非const）
    */
    void OnUpdate();

    /**
     * @brief   viewMatrixをセットする（ウィンドウ全体で共有）
     */
    void SetViewMatrix(Live2D::Cubism::Framework::CubismMatrix44* m);

    // ─── Character roster ──────────────────────────────────────────────

    /**
     * @brief   キャラクターを追加する。
     *
     * @param[in]   modelDirIndex   GetModelDir()のインデックス
     * @param[in]   hasPosition     x/yを明示指定する場合はtrue。falseなら自動横並び配置される。
     * @param[in]   x, y            明示位置（画面座標、おおよそ -1.0〜1.0）
     * @param[in]   scale           ズーム倍率
     * @return      新しいキャラクターの安定ID。modelDirIndexが不正な場合は-1。
     */
    int AddCharacter(Csm::csmInt32 modelDirIndex, bool hasPosition, Csm::csmFloat32 x, Csm::csmFloat32 y, Csm::csmFloat32 scale);

    /**
     * @brief   キャラクターを削除する。
     * @return  見つかって削除できた場合true。
     */
    bool RemoveCharacter(int characterId);

    Csm::csmInt32 GetCharacterCount() const;

    /// ロスター内の位置(0-indexed)からキャラクターIDを取得する。範囲外は-1。
    int GetCharacterIdAt(Csm::csmInt32 index) const;

    LAppModel* GetCharacterModel(int characterId) const;
    Csm::csmInt32 GetCharacterModelDirIndex(int characterId) const;
    Csm::csmFloat32 GetCharacterX(int characterId) const;
    Csm::csmFloat32 GetCharacterY(int characterId) const;
    Csm::csmFloat32 GetCharacterZoom(int characterId) const;

    /// 指定キャラクターのモデルをその場で切り替える（位置は保持される）。
    void SetCharacterModel(int characterId, Csm::csmInt32 modelDirIndex);
    void NextCharacterModel(int characterId);
    void PrevCharacterModel(int characterId);

    /// 指定キャラクターの位置を設定する（ドラッグ後は自動配置の対象から外れる）。
    void SetCharacterPosition(int characterId, Csm::csmFloat32 x, Csm::csmFloat32 y);
    /// anchorX/anchorY: view-space point that stays fixed while zooming (0,0 = screen center).
    void SetCharacterZoom(int characterId, Csm::csmFloat32 scale, Csm::csmFloat32 anchorX = 0.0f, Csm::csmFloat32 anchorY = 0.0f);
    void SwitchSkin(int characterId);

    /// 指定キャラクターの口の開きを設定する（IPCリップシンク用）。
    /// freshness 期限切れ後は OnUpdate() が自動で閉じる方向へ減衰させる。
    void SetCharacterMouth(int characterId, Csm::csmFloat32 value);
    /// 指定キャラクターの視線オーバーライドを設定/解除する。
    void SetCharacterLook(int characterId, Csm::csmFloat32 x, Csm::csmFloat32 y);
    void ClearCharacterLook(int characterId);

    /**
     * @brief   指定した画面座標にある（Head/Bodyの当たり判定にヒットする）キャラクターを探す。
     * @return  見つかったキャラクターのID、なければ-1。
     */
    int HitTestCharacter(Csm::csmFloat32 x, Csm::csmFloat32 y) const;

    // ロスター内の位置でアクセスするレガシーAPI（LAppView.cppのUSE_RENDER_TARGETサンプルパス用）。
    LAppModel* GetModel(Csm::csmUint32 no) const;
    Csm::csmUint32 GetModelNum() const;

    /// 画面ピクセル空間の矩形（左上原点、Waylandサーフェス座標系）。
    struct ScreenRect
    {
        int left;
        int top;
        int width;
        int height;
    };

    /**
     * @brief   各キャラクターの当たり判定と同じ _modelMatrix 状態から
     *          導いた画面矩形を列挙する（入力リージョン用、§2.6）。
     * @param[in]   windowWidth, windowHeight  現在のウィンドウサイズ
     * @param[out]  out  空にしてから詰め直す
     */
    void GetCharacterPixelRects(int windowWidth, int windowHeight,
                                Csm::csmVector<ScreenRect>& out) const;

private:
    LAppLive2DManager();
    virtual ~LAppLive2DManager();

    void ScanModelsInDir(const Csm::csmString& basePath);

    /// modelDirIndexからLAppModelインスタンスを生成・ロードする。所有権は呼び出し側。
    LAppModel* CreateModelInstance(Csm::csmInt32 modelDirIndex) const;

    /// 自動配置対象(autoPositioned)のキャラクターを横一列に再配置する。
    void ReflowAutoLayout();

    struct CharacterSlot
    {
        int id;
        Csm::csmInt32 modelDirIndex;
        LAppModel* model;
        Csm::csmFloat32 posX;
        Csm::csmFloat32 posY;
        Csm::csmFloat32 scale;       ///< current, eased-toward-targetScale each frame in OnUpdate()
        Csm::csmFloat32 targetScale; ///< set by SetCharacterZoom(); scale eases toward this
        Csm::csmFloat32 zoomAnchorX; ///< view-space point held fixed while scale eases
        Csm::csmFloat32 zoomAnchorY;
        bool autoPositioned;
        Csm::csmFloat32 mouthY;      ///< current mouth openness, eased toward 0 when stale
        bool hasMouth;               ///< ever received a mouth sample
        Csm::csmFloat32 mouthAge;    ///< seconds since the last mouth sample
        Csm::csmFloat32 lookX;
        Csm::csmFloat32 lookY;
        bool hasLook;                ///< IPC look override active
    };

    CharacterSlot* FindSlot(int characterId);
    const CharacterSlot* FindSlot(int characterId) const;

    Csm::CubismMatrix44* _viewMatrix; ///< モデル描画に用いるView行列（ウィンドウ共通）
    Csm::csmVector<CharacterSlot> _characters; ///< 表示中キャラクターのリスト
    int _nextCharacterId; ///< 次に割り当てるキャラクターID

    Csm::csmVector<Csm::csmString> _modelDir; ///< モデルディレクトリ名のコンテナ（カタログ、全キャラクター共通）
    Csm::csmVector<Csm::csmString> _modelBasePath; ///< 各モデルの親ディレクトリパス
    Csm::csmVector<Csm::csmString> _modelJsonName; ///< 各モデルの.model3.jsonファイル名
    std::map<std::string, Csm::csmInt32> _modelIndexByName; ///< モデル名→カタログindex（SetUpModelで構築）
};
