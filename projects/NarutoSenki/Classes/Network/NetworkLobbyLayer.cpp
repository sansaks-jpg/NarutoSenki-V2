#include "NetworkLobbyLayer.h"
#include "GameMode/GameModeImpl.h"
#include "LoadLayer.h"
#include "StartMenu.h"

#include <cstdlib>
#include <sstream>

using namespace cocos2d::extension;

namespace
{
const char *kHeroChoices[] = {"Sasuke", "Sakura", "Kakashi", "Naruto"};
constexpr int kHeroChoiceCount = sizeof(kHeroChoices) / sizeof(kHeroChoices[0]);

CCLabelTTF *makeLabel(const std::string &text, float size = 16.0f)
{
    return CCLabelTTF::create(text.c_str(), FONT_NAME, size);
}
}

NetworkLobbyLayer::NetworkLobbyLayer()
    : _session(&nsv2::network::sharedLanSession())
{
}

NetworkLobbyLayer::~NetworkLobbyLayer()
{
    if (!_battleEntered)
        _session->stop();
}

bool NetworkLobbyLayer::init()
{
    if (!Layer::init())
        return false;
    setTouchEnabled(true);
    setKeypadEnabled(true);
    scheduleUpdate();
    renderPage();
    return true;
}

void NetworkLobbyLayer::onEnter()
{
    Layer::onEnter();
    SimpleAudioEngine::sharedEngine()->stopBackgroundMusic(true);
}

void NetworkLobbyLayer::onExit()
{
    setKeypadEnabled(false);
    if (!_battleEntered)
        _session->stop();
    _session->stopScan();
    unscheduleUpdate();
    Layer::onExit();
}

void NetworkLobbyLayer::setMessage(const std::string &message)
{
    _message = message;
    if (_statusLabel)
        _statusLabel->setString(_message.c_str());
}

void NetworkLobbyLayer::renderHeader()
{
    auto background = LayerColor::create(ccc4(18, 25, 45, 255), winSize.width, winSize.height);
    addChild(background, -10);

    auto topBar = Sprite::create("menu_bar3.png");
    if (topBar)
    {
        topBar->setAnchorPoint(Vec2(0, 0));
        topBar->setPosition(Vec2(0, winSize.height - topBar->getContentSize().height));
        topBar->setScaleX(winSize.width / topBar->getContentSize().width);
        addChild(topBar, -5);
    }

    auto title = makeLabel("NETWORK", 28);
    title->setColor(ccc3(255, 220, 120));
    title->setPosition(Vec2(winSize.width / 2, winSize.height - 42));
    addChild(title, 1);

    _statusLabel = makeLabel(_message, 15);
    _statusLabel->setDimensions(CCSize(winSize.width - 50, 58));
    _statusLabel->setHorizontalAlignment(kCCTextAlignmentCenter);
    _statusLabel->setVerticalAlignment(kCCVerticalTextAlignmentCenter);
    _statusLabel->setPosition(Vec2(winSize.width / 2, winSize.height - 94));
    addChild(_statusLabel, 2);
}

void NetworkLobbyLayer::addButton(Menu *menu, const char *label, SEL_MenuHandler selector, int tag, float y)
{
    auto item = CCMenuItemFont::create(label, this, selector);
    item->setFontName(FONT_NAME);
    item->setFontSize(20);
    item->setColor(ccc3(245, 245, 245));
    item->setTag(tag);
    item->setPosition(Vec2(winSize.width / 2, y));
    menu->addChild(item);
}

void NetworkLobbyLayer::renderPage()
{
    removeAllChildrenWithCleanup(true);
    _ipEditBox = nullptr;
    _roomEditBox = nullptr;
    _statusLabel = nullptr;
    _roomLabel = nullptr;
    renderHeader();

    if (_page == Page::Home)
    {
        setMessage("Main jaringan lokal. Pastikan semua perangkat berada di hotspot yang sama.");
        auto menu = Menu::create();
        menu->setPosition(Vec2(0, 0));
        addChild(menu, 5);
        addButton(menu, "HOST ROOM", menu_selector(NetworkLobbyLayer::onHost), 1, 190);
        addButton(menu, "JOIN ROOM", menu_selector(NetworkLobbyLayer::onJoin), 2, 140);
        addButton(menu, "BACK", menu_selector(NetworkLobbyLayer::onBack), 3, 76);
    }
    else if (_page == Page::Join && _session->state() == nsv2::network::SessionState::Idle)
    {
        renderJoinPage();
    }
    else
    {
        renderHostPage();
    }
    _lastSessionState = _session->state();
}

void NetworkLobbyLayer::renderHostPage()
{
    std::ostringstream details;
    if (_session->role() == nsv2::network::SessionRole::Host)
    {
        details << "HOST | port " << nsv2::network::kDefaultLanPort << "\n";
    }
    else
    {
        details << "CLIENT | host " << _session->remoteAddress() << ":" << _session->remotePort() << "\n";
    }

    if (_session->state() == nsv2::network::SessionState::Hosting)
        details << "Menunggu pemain Join...";
    else if (_session->state() == nsv2::network::SessionState::Connecting)
        details << "Menghubungkan ke host...";
    else if (_session->state() == nsv2::network::SessionState::Lobby)
        details << "Lobby aktif. Pilih hero lalu tekan READY.";
    else if (_session->state() == nsv2::network::SessionState::Loading)
        details << "Preload resource sebelum tick match dimulai.";
    else if (_session->state() == nsv2::network::SessionState::Battle)
        details << "Session siap masuk battle authoritative.";
    else
        details << _message;
    setMessage(details.str());

    const auto &config = _session->matchConfig();
    std::ostringstream roster;
    roster << "SLOT 0  " << (config.slots.size() > 0 ? config.slots[0].playerName : "-")
           << " / " << (config.slots.size() > 0 ? config.slots[0].heroName : "-")
           << (config.slots.size() > 0 && config.slots[0].ready ? "  READY" : "  WAIT") << "\n";
    roster << "SLOT 1  " << (config.slots.size() > 1 ? config.slots[1].playerName : "-")
           << " / " << (config.slots.size() > 1 ? config.slots[1].heroName : "-")
           << (config.slots.size() > 1 && config.slots[1].ready ? "  READY" : "  WAIT");
    _roomLabel = makeLabel(roster.str(), 15);
    _roomLabel->setDimensions(CCSize(winSize.width - 50, 70));
    _roomLabel->setHorizontalAlignment(kCCTextAlignmentCenter);
    _roomLabel->setPosition(Vec2(winSize.width / 2, 220));
    addChild(_roomLabel, 2);

    auto menu = Menu::create();
    menu->setPosition(Vec2(0, 0));
    addChild(menu, 5);
    if (_session->state() == nsv2::network::SessionState::Lobby)
    {
        addButton(menu, _session->localReady() ? "UNREADY" : "READY",
                  menu_selector(NetworkLobbyLayer::onReady), 10, 160);
        if (_session->role() == nsv2::network::SessionRole::Client)
        {
            addButton(menu, "CHANGE HERO", menu_selector(NetworkLobbyLayer::onCycleHero), 11, 120);
        }
        if (_session->role() == nsv2::network::SessionRole::Host)
            addButton(menu, "START MATCH", menu_selector(NetworkLobbyLayer::onStart), 12, 120);
        addButton(menu, "LEAVE", menu_selector(NetworkLobbyLayer::onLeave), 13, 80);
    }
    else if (_session->state() == nsv2::network::SessionState::Hosting || _session->state() == nsv2::network::SessionState::Connecting)
    {
        addButton(menu, "LEAVE", menu_selector(NetworkLobbyLayer::onLeave), 13, 90);
    }
    else if (_session->state() == nsv2::network::SessionState::Loading)
    {
        addButton(menu, "MARK LOADED", menu_selector(NetworkLobbyLayer::onStart), 14, 120);
        addButton(menu, "LEAVE", menu_selector(NetworkLobbyLayer::onLeave), 13, 80);
    }
    else
    {
        addButton(menu, "BACK", menu_selector(NetworkLobbyLayer::onBack), 3, 80);
    }
}

void NetworkLobbyLayer::renderJoinPage()
{
    setMessage("Mencari room LAN... atau masukkan IP host secara manual.");

    auto inputBackground = CCScale9Sprite::create("menu_bar2.png", CCRect(0, 0, 16, 16));
    _ipEditBox = CCEditBox::create(CCSize(250, 40), inputBackground);
    _ipEditBox->setDelegate(this);
    _ipEditBox->setText("127.0.0.1:28765");
    _ipEditBox->setPlaceHolder("IP:port");
    _ipEditBox->setInputMode(kEditBoxInputModeSingleLine);
    _ipEditBox->setReturnType(kKeyboardReturnTypeGo);
    _ipEditBox->setFont(FONT_NAME, 16);
    _ipEditBox->setPosition(Vec2(winSize.width / 2, 265));
    addChild(_ipEditBox, 4);

    auto menu = Menu::create();
    menu->setPosition(Vec2(0, 0));
    addChild(menu, 5);
    addButton(menu, "JOIN IP", menu_selector(NetworkLobbyLayer::onJoinManual), 20, 215);
    addButton(menu, "REFRESH", menu_selector(NetworkLobbyLayer::onRefresh), 21, 175);

    float roomY = 125;
    if (_rooms.empty())
    {
        auto empty = makeLabel("Belum ada room. Host harus sudah membuat room.", 14);
        empty->setPosition(Vec2(winSize.width / 2, roomY));
        addChild(empty, 2);
    }
    else
    {
        for (size_t i = 0; i < _rooms.size() && i < 4; ++i)
        {
            const auto &room = _rooms[i];
            std::ostringstream label;
            label << room.roomName << "  " << static_cast<int>(room.playerCount) << "/"
                  << static_cast<int>(room.maxPlayers) << "  " << room.address << ":" << room.port;
            addButton(menu, label.str().c_str(), menu_selector(NetworkLobbyLayer::onJoinRoom),
                      static_cast<int>(i), roomY);
            roomY -= 34;
        }
    }
    addButton(menu, "BACK", menu_selector(NetworkLobbyLayer::onBack), 3, 58);
}

void NetworkLobbyLayer::enterNetworkBattle()
{
    if (_battleEntered || _session->state() != nsv2::network::SessionState::Battle)
        return;
    _battleEntered = true;

    s_GameMode = GameMode::OneVsOne;
    auto handler = getGameModeHandler();
    handler->initNetworkHeros(_session->matchConfig(), _session->localSlot());

    auto loadScene = Scene::create();
    auto loadLayer = LoadLayer::create();
    loadLayer->configureNetworkBattle(_session->localSlot());
    loadLayer->preloadAudio();
    loadScene->addChild(loadLayer);
    Director::sharedDirector()->replaceScene(TransitionFade::create(0.75f, loadScene));
}

void NetworkLobbyLayer::update(float dt)
{
    (void)dt;
    ++_frameCounter;
    // LAN is opt-in. Merely opening Network Home must not poll sockets or
    // discovery; Host and Join enable the runtime explicitly.
    if (_session->networkActive())
        _session->poll();

    std::vector<nsv2::network::SessionNotice> notices;
    _session->drainNotices(notices);
    if (!notices.empty())
        setMessage(notices.back().text);

    if (_session->state() == nsv2::network::SessionState::Lobby &&
        _session->role() == nsv2::network::SessionRole::Client &&
        _session->matchConfig().slots.size() > 1 &&
        _session->matchConfig().slots[1].heroName.empty())
    {
        _session->setLocalHero(kHeroChoices[0]);
    }

    if (_session->state() == nsv2::network::SessionState::Battle)
    {
        enterNetworkBattle();
        return;
    }

    if (_session->state() != _lastSessionState)
        renderPage();

    if (_page == Page::Join && _session->networkActive() &&
        _session->state() == nsv2::network::SessionState::Idle && _frameCounter % 10 == 0)
    {
        std::vector<nsv2::network::RoomAdvertisement> rooms;
        _session->getRooms(rooms);
        std::ostringstream fingerprint;
        for (const auto &room : rooms)
            fingerprint << room.address << ':' << room.port << ':' << room.roomId << ';';
        if (fingerprint.str() != _roomsFingerprint)
        {
            _rooms = std::move(rooms);
            _roomsFingerprint = fingerprint.str();
            renderPage();
        }
    }
}

void NetworkLobbyLayer::onHost(Ref *sender)
{
    (void)sender;
    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/confirm.ogg");
    _page = Page::Host;
    std::string error;
    if (!_session->host("Naruto Senki LAN", "Host", nsv2::network::kDefaultLanPort, &error))
        setMessage("Host gagal: " + error);
    renderPage();
}

void NetworkLobbyLayer::onJoin(Ref *sender)
{
    (void)sender;
    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/confirm.ogg");
    _page = Page::Join;
    _rooms.clear();
    _roomsFingerprint.clear();
    std::string error;
    if (!_session->startScan(&error))
        setMessage("Discovery gagal; gunakan IP:port manual. " + error);
    renderPage();
}

void NetworkLobbyLayer::onBack(Ref *sender)
{
    (void)sender;
    if (_leavingNetwork)
        return;

    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/cancel.ogg");
    _session->stop();
    _session->stopScan();

    if (_page != Page::Home)
    {
        _page = Page::Home;
        renderPage();
        return;
    }

    _leavingNetwork = true;
    auto startScene = Scene::create();
    startScene->addChild(StartMenu::create());
    Director::sharedDirector()->replaceScene(TransitionFade::create(0.5f, startScene));
}

void NetworkLobbyLayer::keyBackClicked()
{
    onBack(nullptr);
}

void NetworkLobbyLayer::onRefresh(Ref *sender)
{
    (void)sender;
    _rooms.clear();
    _roomsFingerprint.clear();
    std::string error;
    _session->startScan(&error);
    if (!error.empty())
        setMessage("Refresh discovery: " + error);
}

std::string NetworkLobbyLayer::selectedAddress() const
{
    if (!_ipEditBox || !_ipEditBox->getText())
        return "127.0.0.1";
    std::string value = _ipEditBox->getText();
    const size_t separator = value.find(':');
    if (separator != std::string::npos)
        value.resize(separator);
    return value.empty() ? "127.0.0.1" : value;
}

uint16_t NetworkLobbyLayer::selectedPort() const
{
    if (!_ipEditBox || !_ipEditBox->getText())
        return nsv2::network::kDefaultLanPort;
    const std::string value = _ipEditBox->getText();
    const size_t separator = value.find(':');
    if (separator == std::string::npos)
        return nsv2::network::kDefaultLanPort;
    const unsigned long parsed = std::strtoul(value.c_str() + separator + 1, nullptr, 10);
    return parsed > 0 && parsed <= 65535 ? static_cast<uint16_t>(parsed) : nsv2::network::kDefaultLanPort;
}

void NetworkLobbyLayer::onJoinManual(Ref *sender)
{
    (void)sender;
    _session->stopScan();
    _page = Page::Host;
    std::string error;
    if (!_session->join(selectedAddress(), selectedPort(), "Client", &error))
        setMessage("Join gagal: " + error);
    renderPage();
}

void NetworkLobbyLayer::onJoinRoom(Ref *sender)
{
    if (!sender)
        return;
    const int index = static_cast<MenuItem *>(sender)->getTag();
    if (index < 0 || static_cast<size_t>(index) >= _rooms.size())
        return;
    _session->stopScan();
    _page = Page::Host;
    std::string error;
    const auto &room = _rooms[static_cast<size_t>(index)];
    if (!_session->join(room.address, room.port, "Client", &error))
        setMessage("Join room gagal: " + error);
    renderPage();
}

void NetworkLobbyLayer::onReady(Ref *sender)
{
    (void)sender;
    std::string error;
    if (!_session->setLocalReady(!_session->localReady()))
    {
        if (error.empty())
            error = "hero belum dipilih atau session belum berada di lobby";
        setMessage("Ready gagal: " + error);
    }
    renderPage();
}

void NetworkLobbyLayer::onCycleHero(Ref *sender)
{
    (void)sender;
    static int heroIndex = 0;
    heroIndex = (heroIndex + 1) % kHeroChoiceCount;
    if (!_session->setLocalHero(kHeroChoices[heroIndex]))
        setMessage("Pilihan hero belum bisa dikirim.");
    else
        setMessage(std::string("Hero dipilih: ") + kHeroChoices[heroIndex]);
    renderPage();
}

void NetworkLobbyLayer::onStart(Ref *sender)
{
    (void)sender;
    std::string error;
    bool success = false;
    if (_session->state() == nsv2::network::SessionState::Loading)
        success = _session->markLoaded(&error);
    else
        success = _session->startMatch(&error);
    if (!success)
        setMessage("Match belum bisa dimulai: " + error);
    renderPage();
}

void NetworkLobbyLayer::onLeave(Ref *sender)
{
    (void)sender;
    _session->stop();
    _page = Page::Home;
    renderPage();
}

void NetworkLobbyLayer::editBoxReturn(CCEditBox *editBox)
{
    if (editBox == _ipEditBox)
        onJoinManual(nullptr);
}
