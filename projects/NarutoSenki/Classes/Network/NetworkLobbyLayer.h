#pragma once

#include "Defines.h"
#include "Network/LanNetworkRuntime.hpp"
#include "cocos-ext.h"

class NetworkLobbyLayer : public Layer, public cocos2d::extension::CCEditBoxDelegate
{
public:
    enum class Page
    {
        Home,
        Host,
        Join,
    };

    NetworkLobbyLayer();
    ~NetworkLobbyLayer() override;

    bool init() override;
    void update(float dt) override;

    void onHost(Ref *sender);
    void onJoin(Ref *sender);
    void onBack(Ref *sender);
    void onRefresh(Ref *sender);
    void onJoinManual(Ref *sender);
    void onJoinRoom(Ref *sender);
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
    void renderHostPage();
    void renderJoinPage();
    void renderLobbyPage();
    void enterNetworkBattle();
    void setMessage(const std::string &message);
    void addButton(Menu *menu, const char *label, SEL_MenuHandler selector, int tag, float y);
    std::string selectedAddress() const;
    uint16_t selectedPort() const;

    nsv2::network::LanSession *_session = nullptr;
    Page _page = Page::Home;
    std::vector<nsv2::network::RoomAdvertisement> _rooms;
    std::string _message;
    std::string _roomsFingerprint;
    nsv2::network::SessionState _lastSessionState = nsv2::network::SessionState::Idle;
    cocos2d::extension::CCEditBox *_ipEditBox = nullptr;
    cocos2d::extension::CCEditBox *_roomEditBox = nullptr;
    CCLabelTTF *_statusLabel = nullptr;
    CCLabelTTF *_roomLabel = nullptr;
    int _frameCounter = 0;
    bool _battleEntered = false;
};
