#pragma once

#include "Defines.h"
#include "Network/LanNetworkRuntime.hpp"
#include "Data/Fonts.h"
#include "cocos-ext.h"

class NetworkLobbyLayer : public Layer, public cocos2d::extension::CCEditBoxDelegate
{
public:
    enum class Page
    {
        Home,
        Host,
        Join,
        HeroSelect,
    };

    NetworkLobbyLayer();
    ~NetworkLobbyLayer() override;

    bool init() override;
    void update(float dt) override;
    void keyBackClicked() override;

    void onHost(Ref *sender);
    void onJoin(Ref *sender);
    void onBack(Ref *sender);
    void onRefresh(Ref *sender);
    void onJoinManual(Ref *sender);
    void onJoinRoom(Ref *sender);
    void onOpenHeroSelect(Ref *sender);
    void onHeroAvatarClicked(Ref *sender);
    void onConfirmHeroSelect(Ref *sender);
    void onCycleHero(Ref *sender);
    void onReady(Ref *sender);
    void onStart(Ref *sender);
    void onLeave(Ref *sender);

    void editBoxReturn(cocos2d::extension::CCEditBox *editBox) override;

    CREATE_FUNC(NetworkLobbyLayer);

private:
    void onEnter() override;
    void onExit() override;
    void renderPage();
    void renderHeader();
    void renderHomePage();
    void renderHostPage();
    void renderJoinPage();
    void renderHeroSelectPage();
    void enterNetworkBattle();
    void setMessage(const std::string &message);
    std::string selectedAddress() const;
    uint16_t selectedPort() const;

    nsv2::network::LanSession *_session = nullptr;
    Page _page = Page::Home;
    std::vector<nsv2::network::RoomAdvertisement> _rooms;
    std::string _message;
    std::string _roomsFingerprint;
    std::string _savedIpText = "127.0.0.1:28765";
    nsv2::network::SessionState _lastSessionState = nsv2::network::SessionState::Idle;
    cocos2d::extension::CCEditBox *_ipEditBox = nullptr;
    CCLabelTTF *_statusLabel = nullptr;
    int _frameCounter = 0;
    bool _battleEntered = false;
    bool _leavingNetwork = false;
    int _localHeroIndex = 0;
    std::string _previewHeroName = "Naruto";
    int _lastClickedHeroIndex = -1;
    float _lastHeroClickTime = 0.0f;
    float _totalRunningTime = 0.0f;
};
