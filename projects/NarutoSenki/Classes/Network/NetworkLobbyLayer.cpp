#include "NetworkLobbyLayer.h"
#include "GameMode/GameModeImpl.h"
#include "LoadLayer.h"
#include "StartMenu.h"

#include <cstdlib>
#include <sstream>

using namespace cocos2d::extension;

namespace
{
const char *kAllSelectHeroes[] = {
    "Naruto", "Sasuke", "Sakura", "Kakashi", "Lee",
    "Gaara", "Itachi", "Minato", "Pain", "Jiraiya",
    "Hiruzen", "Asuma", "Choji", "Deidara", "Hidan",
    "Hinata", "Ino", "Jugo", "Kakuzu", "Kankuro",
    "Karin", "Kiba", "Kimimaro", "Kisame", "Konan",
    "Neji", "Orochimaru", "Sai", "Shikamaru", "Shino",
    "Suigetsu", "Tenten", "Tobi", "Tobirama", "Tsunade"
};
constexpr int kAllSelectHeroCount = sizeof(kAllSelectHeroes) / sizeof(kAllSelectHeroes[0]);
const char **kHeroChoices = kAllSelectHeroes;
constexpr int kHeroChoiceCount = kAllSelectHeroCount;

CCLabelBMFont *makeBMFont(const std::string &text, const char *fontFile, float scale = 0.45f)
{
    auto label = CCLabelBMFont::create(text.c_str(), fontFile);
    label->setScale(scale);
    return label;
}
} // namespace

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

    addSprites("UI.plist");
    addSprites("Menu.plist");
    addSprites("Select.plist");
    addSprites("Record.plist");
    addSprites("NamePlate.plist");
    addSprites("Result.plist");

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
    if (UserDefault::sharedUserDefault()->getBoolForKey("isBGM"))
    {
        SimpleAudioEngine::sharedEngine()->playBackgroundMusic(MENU_MUSIC, true);
    }
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
    // Background
    auto bgSprite = Sprite::create("blue_bg.png");
    if (bgSprite)
    {
        bgSprite->setAnchorPoint(Vec2(0, 0));
        bgSprite->setPosition(Vec2(0, 0));
        FULL_SCREEN_SPRITE(bgSprite);
        addChild(bgSprite, -10);
    }

    // Gold borders
    auto gold_left = Sprite::createWithSpriteFrameName("gold_left.png");
    if (gold_left)
    {
        gold_left->setAnchorPoint(Vec2(0, 0));
        gold_left->setPosition(Vec2(0, 20));
        addChild(gold_left, 1);
    }

    auto gold_right = Sprite::createWithSpriteFrameName("gold_right.png");
    if (gold_right)
    {
        gold_right->setAnchorPoint(Vec2(0, 1));
        gold_right->setPosition(Vec2(winSize.width - gold_right->getContentSize().width - 20, winSize.height - 20));
        addChild(gold_right, 1);
    }

    // Floating clouds
    auto cloud_left = Sprite::createWithSpriteFrameName("cloud.png");
    if (cloud_left)
    {
        cloud_left->setPosition(Vec2(0, 15));
        cloud_left->setAnchorPoint(Vec2(0, 0));
        cloud_left->setFlipX(true);
        cloud_left->setFlipY(true);
        addChild(cloud_left, 1);
        auto cmv1 = MoveBy::create(1.0f, Vec2(-15, 0));
        cloud_left->runAction(RepeatForever::create(newSequence(cmv1, cmv1->reverse())));
    }

    auto cloud_right = Sprite::createWithSpriteFrameName("cloud.png");
    if (cloud_right)
    {
        cloud_right->setPosition(Vec2(winSize.width - cloud_right->getContentSize().width,
                                      winSize.height - (cloud_right->getContentSize().height + 15)));
        cloud_right->setAnchorPoint(Vec2(0, 0));
        addChild(cloud_right, 1);
        auto cmv2 = MoveBy::create(1.0f, Vec2(15, 0));
        cloud_right->runAction(RepeatForever::create(newSequence(cmv2, cmv2->reverse())));
    }

    // Top & bottom scroll bars
    auto menu_bar_b = Sprite::create("menu_bar2.png");
    if (menu_bar_b)
    {
        menu_bar_b->setAnchorPoint(Vec2(0, 0));
        FULL_SCREEN_SPRITE(menu_bar_b);
        addChild(menu_bar_b, 2);
    }

    auto menu_bar_t = Sprite::create("menu_bar3.png");
    if (menu_bar_t)
    {
        menu_bar_t->setAnchorPoint(Vec2(0, 0));
        menu_bar_t->setPosition(Vec2(0, winSize.height - menu_bar_t->getContentSize().height));
        FULL_SCREEN_SPRITE(menu_bar_t);
        addChild(menu_bar_t, 2);
    }

    // Title banner
    auto startmenu_title = Sprite::createWithSpriteFrameName("startmenu_title.png");
    if (startmenu_title)
    {
        startmenu_title->setAnchorPoint(Vec2(0, 0));
        startmenu_title->setPosition(Vec2(2, winSize.height - startmenu_title->getContentSize().height - 2));
        addChild(startmenu_title, 3);
    }

    auto titleText = makeBMFont("LAN MULTIPLAYER", Fonts::Yellow, 0.52f);
    titleText->setPosition(Vec2(winSize.width / 2, winSize.height - 18));
    addChild(titleText, 4);

    // Status label at bottom
    _statusLabel = CCLabelTTF::create(_message.c_str(), FONT_NAME, 11);
    _statusLabel->setAnchorPoint(Vec2(0, 0));
    _statusLabel->setPosition(Vec2(12, 4));
    addChild(_statusLabel, 5);

    // Return button
    auto returnImg = MenuItemSprite::create(Sprite::create("UI/return_btn.png"), nullptr, nullptr, this, menu_selector(NetworkLobbyLayer::onBack));
    auto returnMenu = Menu::create(returnImg, nullptr);
    returnMenu->setAnchorPoint(Vec2(1, 0.5f));
    returnMenu->setPosition(Vec2(winSize.width - 35, 45));
    addChild(returnMenu, 6);
}

void NetworkLobbyLayer::renderPage()
{
    if (_ipEditBox)
    {
        if (_ipEditBox->getText())
            _savedIpText = _ipEditBox->getText();
        _ipEditBox->setDelegate(nullptr);
        _ipEditBox = nullptr;
    }
    _roomListContainer = nullptr;
    _statusLabel = nullptr;
    removeAllChildrenWithCleanup(true);
    renderHeader();

    if (_page == Page::Home)
    {
        renderHomePage();
    }
    else if (_page == Page::Join && _session->state() == nsv2::network::SessionState::Idle)
    {
        renderJoinPage();
    }
    else if (_page == Page::HeroSelect)
    {
        renderHeroSelectPage();
    }
    else
    {
        renderHostPage();
    }
    _lastSessionState = _session->state();
}

void NetworkLobbyLayer::renderHomePage()
{
    setMessage("LAN multiplayer. Ensure all devices are on the same Wi-Fi / Hotspot.");

    auto panel = Sprite::createWithSpriteFrameName("confirm_bg.png");
    if (panel)
    {
        panel->setPosition(Vec2(winSize.width / 2, winSize.height / 2 + 8));
        addChild(panel, 3);
    }

    auto modeTitle = makeBMFont("SELECT NETWORK ROLE", Fonts::Yellow, 0.48f);
    modeTitle->setPosition(Vec2(winSize.width / 2, winSize.height / 2 + 50));
    addChild(modeTitle, 4);

    auto desc = CCLabelTTF::create("Ensure both devices are connected to the same Wi-Fi / Hotspot", FONT_NAME, 10);
    desc->setPosition(Vec2(winSize.width / 2, winSize.height / 2 + 25));
    addChild(desc, 4);

    // Host Button
    auto hostNormal = Sprite::createWithSpriteFrameName("yes_btn1.png");
    auto hostSelect = Sprite::createWithSpriteFrameName("yes_btn2.png");
    auto hostLabel = makeBMFont("HOST", Fonts::White, 0.42f);
    hostLabel->setPosition(Vec2(hostNormal->getContentSize().width / 2, hostNormal->getContentSize().height / 2));
    hostNormal->addChild(hostLabel);
    auto hostBtn = MenuItemSprite::create(hostNormal, hostSelect, this, menu_selector(NetworkLobbyLayer::onHost));

    // Join Button
    auto joinNormal = Sprite::createWithSpriteFrameName("no_btn1.png");
    auto joinSelect = Sprite::createWithSpriteFrameName("no_btn2.png");
    auto joinLabel = makeBMFont("JOIN", Fonts::White, 0.42f);
    joinLabel->setPosition(Vec2(joinNormal->getContentSize().width / 2, joinNormal->getContentSize().height / 2));
    joinNormal->addChild(joinLabel);
    auto joinBtn = MenuItemSprite::create(joinNormal, joinSelect, this, menu_selector(NetworkLobbyLayer::onJoin));

    auto menu = Menu::create(hostBtn, joinBtn, nullptr);
    menu->alignItemsHorizontallyWithPadding(30);
    menu->setPosition(Vec2(winSize.width / 2, winSize.height / 2 - 20));
    addChild(menu, 5);
}

void NetworkLobbyLayer::renderJoinPage()
{
    setMessage("Searching for LAN rooms... or enter host IP manually.");

    auto panel = Sprite::createWithSpriteFrameName("confirm_bg.png");
    if (panel)
    {
        panel->setScaleX(1.9f);
        panel->setScaleY(1.35f);
        panel->setPosition(Vec2(winSize.width / 2, winSize.height / 2 + 10));
        addChild(panel, 3);
    }

    auto joinTitle = makeBMFont("SEARCH OR ENTER HOST IP", Fonts::Yellow, 0.46f);
    joinTitle->setPosition(Vec2(winSize.width / 2, winSize.height / 2 + 75));
    addChild(joinTitle, 4);

    auto inputBg = CCScale9Sprite::createWithSpriteFrameName("input_bg.png", CCRect(10, 10, 10, 10));
    _ipEditBox = CCEditBox::create(CCSize(170, 26), inputBg);
    _ipEditBox->setPosition(Vec2(winSize.width / 2 - 45, winSize.height / 2 + 40));
    _ipEditBox->setText(_savedIpText.c_str());
    _ipEditBox->setPlaceHolder("192.168.x.x:28765");
    _ipEditBox->setInputMode(kEditBoxInputModeSingleLine);
    _ipEditBox->setReturnType(kKeyboardReturnTypeGo);
    _ipEditBox->setFont(FONT_NAME, 12);
    _ipEditBox->setFontColor(ccc3(255, 255, 255));
    _ipEditBox->setDelegate(this);
    addChild(_ipEditBox, 4);

    auto joinNormal = Sprite::createWithSpriteFrameName("yes_btn1.png");
    joinNormal->setScale(0.75f);
    auto joinSelect = Sprite::createWithSpriteFrameName("yes_btn2.png");
    joinSelect->setScale(0.75f);
    auto joinLabel = makeBMFont("JOIN", Fonts::White, 0.40f);
    joinLabel->setPosition(Vec2(joinNormal->getContentSize().width / 2, joinNormal->getContentSize().height / 2));
    joinNormal->addChild(joinLabel);
    auto joinBtn = MenuItemSprite::create(joinNormal, joinSelect, this, menu_selector(NetworkLobbyLayer::onJoinManual));

    auto refreshBtn = MenuItemSprite::create(Sprite::createWithSpriteFrameName("refresh_btn.png"),
                                             Sprite::createWithSpriteFrameName("refresh_btn.png"),
                                             this, menu_selector(NetworkLobbyLayer::onRefresh));

    auto actionMenu = Menu::create(joinBtn, refreshBtn, nullptr);
    actionMenu->setPosition(Vec2(0, 0));
    joinBtn->setPosition(Vec2(winSize.width / 2 + 80, winSize.height / 2 + 40));
    refreshBtn->setPosition(Vec2(winSize.width / 2 + 135, winSize.height / 2 + 40));
    addChild(actionMenu, 5);

    _roomListContainer = Node::create();
    _roomListContainer->setPosition(Vec2(0, 0));
    addChild(_roomListContainer, 5);
    updateRoomListUI();
}

void NetworkLobbyLayer::updateRoomListUI()
{
    if (!_roomListContainer)
        return;
    _roomListContainer->removeAllChildrenWithCleanup(true);

    float roomY = winSize.height / 2 - 5;
    if (_rooms.empty())
    {
        auto empty = CCLabelTTF::create("Searching for rooms on network... (Host must create room first)", FONT_NAME, 11);
        empty->setPosition(Vec2(winSize.width / 2, roomY - 15));
        _roomListContainer->addChild(empty, 4);
    }
    else
    {
        auto roomMenu = Menu::create();
        roomMenu->setPosition(Vec2(0, 0));
        _roomListContainer->addChild(roomMenu, 5);
        for (size_t i = 0; i < _rooms.size() && i < 3; ++i)
        {
            const auto &room = _rooms[i];
            std::ostringstream labelText;
            labelText << room.roomName << "  (" << static_cast<int>(room.playerCount) << "/"
                      << static_cast<int>(room.maxPlayers) << ")  " << room.address;

            auto item = CCMenuItemFont::create(labelText.str().c_str(), this, menu_selector(NetworkLobbyLayer::onJoinRoom));
            item->setFontName(FONT_NAME);
            item->setFontSize(13);
            item->setColor(ccc3(255, 230, 150));
            item->setTag(static_cast<int>(i));
            item->setPosition(Vec2(winSize.width / 2, roomY));
            roomMenu->addChild(item);
            roomY -= 25;
        }
    }
}

void NetworkLobbyLayer::renderHostPage()
{
    const auto &config = _session->matchConfig();
    const uint8_t mySlot = _session->localSlot(); // 0 for Host, 1 for Client
    const uint8_t oppSlot = mySlot == 0 ? 1 : 0;

    std::string statusText;
    if (_session->state() == nsv2::network::SessionState::Hosting)
        statusText = "Waiting for opponent to join...";
    else if (_session->state() == nsv2::network::SessionState::Connecting)
        statusText = "Connecting to host...";
    else if (_session->state() == nsv2::network::SessionState::Lobby)
        statusText = "LOBBY 1v1 - Choose Hero & Tap READY";
    else if (_session->state() == nsv2::network::SessionState::Loading)
        statusText = "Preloading match resources...";
    else if (_session->state() == nsv2::network::SessionState::Battle)
        statusText = "Session ready, entering battle!";
    else
        statusText = _message;
    setMessage(statusText);

    // =======================================================
    // LEFT SIDE: LOCAL PLAYER (POV Self - 1P / Blue Banner)
    // =======================================================
    auto banner1 = Sprite::createWithSpriteFrameName("banner_blue.png");
    if (banner1)
    {
        banner1->setPosition(Vec2(winSize.width * 0.25f, winSize.height - 58));
        addChild(banner1, 3);

        auto p1Badge = Sprite::createWithSpriteFrameName("1p.png");
        if (p1Badge)
        {
            p1Badge->setPosition(Vec2(18, banner1->getContentSize().height / 2));
            banner1->addChild(p1Badge);
        }

        std::string myName = config.slots.size() > mySlot && !config.slots[mySlot].playerName.empty()
                                 ? config.slots[mySlot].playerName
                                 : (_session->role() == nsv2::network::SessionRole::Host ? "Host" : "Client");
        auto p1Label = makeBMFont(myName, Fonts::White, 0.42f);
        p1Label->setPosition(Vec2(banner1->getContentSize().width / 2 + 10, banner1->getContentSize().height / 2));
        banner1->addChild(p1Label);
    }

    std::string myHero = config.slots.size() > mySlot && !config.slots[mySlot].heroName.empty()
                            ? config.slots[mySlot].heroName : (_session->role() == nsv2::network::SessionRole::Host ? "Naruto" : "Sasuke");
    std::string portraitPath0 = myHero + "_half.png";
    auto heroPortrait0 = Sprite::createWithSpriteFrameName(portraitPath0.c_str());
    if (!heroPortrait0)
        heroPortrait0 = Sprite::createWithSpriteFrameName("Naruto_half.png");
    if (heroPortrait0)
    {
        heroPortrait0->setPosition(Vec2(winSize.width * 0.25f, winSize.height / 2 + 8));
        addChild(heroPortrait0, 2);
    }

    auto heroNameLabel0 = makeBMFont(myHero, Fonts::Yellow, 0.44f);
    heroNameLabel0->setPosition(Vec2(winSize.width * 0.25f, 96));
    addChild(heroNameLabel0, 3);

    bool myReady = config.slots.size() > mySlot && config.slots[mySlot].ready;
    auto readyLabel0 = makeBMFont(myReady ? "READY" : "WAITING", myReady ? Fonts::Yellow : Fonts::White, 0.40f);
    readyLabel0->setPosition(Vec2(winSize.width * 0.25f, 78));
    addChild(readyLabel0, 3);

    // ==========================================
    // CENTER: VS Sign & Role Info
    // ==========================================
    auto vsLabel = makeBMFont("VS", Fonts::Yellow, 0.85f);
    vsLabel->setPosition(Vec2(winSize.width / 2, winSize.height / 2 + 25));
    addChild(vsLabel, 3);

    auto subStatus = CCLabelTTF::create((_session->role() == nsv2::network::SessionRole::Host ? "HOST" : "CLIENT"), FONT_NAME, 12);
    subStatus->setPosition(Vec2(winSize.width / 2, winSize.height / 2 - 5));
    addChild(subStatus, 3);

    // =======================================================
    // RIGHT SIDE: REMOTE PLAYER (POV Opponent - 2P / Red Banner)
    // =======================================================
    auto banner2 = Sprite::createWithSpriteFrameName("banner_red.png");
    if (banner2)
    {
        banner2->setPosition(Vec2(winSize.width * 0.75f, winSize.height - 58));
        addChild(banner2, 3);

        auto p2Badge = Sprite::createWithSpriteFrameName("2p.png");
        if (p2Badge)
        {
            p2Badge->setPosition(Vec2(18, banner2->getContentSize().height / 2));
            banner2->addChild(p2Badge);
        }

        std::string oppName = config.slots.size() > oppSlot && !config.slots[oppSlot].playerName.empty()
                                 ? config.slots[oppSlot].playerName
                                 : (_session->state() == nsv2::network::SessionState::Hosting ? "Waiting..." : (_session->role() == nsv2::network::SessionRole::Host ? "Client" : "Host"));
        auto p2Label = makeBMFont(oppName, Fonts::White, 0.42f);
        p2Label->setPosition(Vec2(banner2->getContentSize().width / 2 + 10, banner2->getContentSize().height / 2));
        banner2->addChild(p2Label);
    }

    std::string oppHero = config.slots.size() > oppSlot && !config.slots[oppSlot].heroName.empty()
                            ? config.slots[oppSlot].heroName : (_session->role() == nsv2::network::SessionRole::Host ? "Sasuke" : "Naruto");
    std::string portraitPath1 = oppHero + "_half.png";
    auto heroPortrait1 = Sprite::createWithSpriteFrameName(portraitPath1.c_str());
    if (!heroPortrait1)
        heroPortrait1 = Sprite::createWithSpriteFrameName("Sasuke_half.png");
    if (heroPortrait1)
    {
        heroPortrait1->setFlipX(true);
        heroPortrait1->setPosition(Vec2(winSize.width * 0.75f, winSize.height / 2 + 8));
        if (_session->state() == nsv2::network::SessionState::Hosting)
            heroPortrait1->setColor(ccc3(80, 80, 80));
        addChild(heroPortrait1, 2);
    }

    auto heroNameLabel1 = makeBMFont((_session->state() == nsv2::network::SessionState::Hosting ? "-" : oppHero), Fonts::Yellow, 0.44f);
    heroNameLabel1->setPosition(Vec2(winSize.width * 0.75f, 96));
    addChild(heroNameLabel1, 3);

    bool oppReady = config.slots.size() > oppSlot && config.slots[oppSlot].ready;
    auto readyLabel1 = makeBMFont((_session->state() == nsv2::network::SessionState::Hosting ? "WAIT" : (oppReady ? "READY" : "WAITING")),
                                  oppReady ? Fonts::Yellow : Fonts::White, 0.40f);
    readyLabel1->setPosition(Vec2(winSize.width * 0.75f, 78));
    addChild(readyLabel1, 3);

    // ==========================================
    // ACTION CONTROLS / BUTTONS
    // ==========================================
    auto controlMenu = Menu::create();
    controlMenu->setPosition(Vec2(0, 0));
    addChild(controlMenu, 5);

    if (_session->state() == nsv2::network::SessionState::Lobby)
    {
        // Change Hero button placed below local player portrait (Left Side)
        auto changeBtn = MenuItemSprite::create(Sprite::createWithSpriteFrameName("change_btn.png"),
                                                Sprite::createWithSpriteFrameName("change_btn2.png"),
                                                this, menu_selector(NetworkLobbyLayer::onOpenHeroSelect));
        changeBtn->setPosition(Vec2(winSize.width * 0.25f, 48));
        controlMenu->addChild(changeBtn);

        // Ready Button in center
        auto readyNormal = Sprite::createWithSpriteFrameName(_session->localReady() ? "no_btn1.png" : "yes_btn1.png");
        auto readySelect = Sprite::createWithSpriteFrameName(_session->localReady() ? "no_btn2.png" : "yes_btn2.png");
        auto readyText = makeBMFont(_session->localReady() ? "CANCEL" : "READY", Fonts::White, 0.40f);
        readyText->setPosition(Vec2(readyNormal->getContentSize().width / 2, readyNormal->getContentSize().height / 2));
        readyNormal->addChild(readyText);
        auto readyBtn = MenuItemSprite::create(readyNormal, readySelect, this, menu_selector(NetworkLobbyLayer::onReady));
        readyBtn->setPosition(Vec2(winSize.width / 2, 50));
        controlMenu->addChild(readyBtn);

        // Start Match Button (Host only, when both players are ready)
        bool clientReady = config.slots.size() >= 2 && config.slots[1].ready;
        bool hostReady = config.slots.size() >= 1 && config.slots[0].ready;
        if (_session->role() == nsv2::network::SessionRole::Host && hostReady && clientReady)
        {
            auto startBtn = MenuItemSprite::create(Sprite::createWithSpriteFrameName("start_btn.png"),
                                                   Sprite::createWithSpriteFrameName("start_btn.png"),
                                                   this, menu_selector(NetworkLobbyLayer::onStart));
            startBtn->setPosition(Vec2(winSize.width / 2, 92));
            controlMenu->addChild(startBtn);
        }
    }
}

void NetworkLobbyLayer::renderHeroSelectPage()
{
    setMessage("Tap character to preview. Double-click or tap OK to select hero.");

    // Grid of 35 character avatar buttons on the left
    // 7 columns x 5 rows
    auto avatarMenu = Menu::create();
    avatarMenu->setPosition(Vec2(0, 0));
    addChild(avatarMenu, 5);

    const int cols = 7;
    const float startX = 24.0f;
    const float stepX = 35.0f;
    const float startY = winSize.height - 68.0f;
    const float stepY = 38.0f;

    for (int i = 0; i < kAllSelectHeroCount; ++i)
    {
        const int col = i % cols;
        const int row = i / cols;
        const float posX = startX + col * stepX;
        const float posY = startY - row * stepY;

        std::string selSpriteName = std::string(kAllSelectHeroes[i]) + "_select.png";
        auto normalSprite = Sprite::createWithSpriteFrameName(selSpriteName.c_str());
        if (!normalSprite)
            normalSprite = Sprite::createWithSpriteFrameName("Naruto_select.png");
        auto selectSprite = Sprite::createWithSpriteFrameName(selSpriteName.c_str());
        if (!selectSprite)
            selectSprite = Sprite::createWithSpriteFrameName("Naruto_select.png");

        if (normalSprite && selectSprite)
        {
            auto item = MenuItemSprite::create(normalSprite, selectSprite, this, menu_selector(NetworkLobbyLayer::onHeroAvatarClicked));
            item->setTag(i);
            item->setPosition(Vec2(posX, posY));
            avatarMenu->addChild(item);

            // Highlight frame for currently previewed hero
            if (_previewHeroName == kAllSelectHeroes[i])
            {
                auto blinkFrame = Sprite::createWithSpriteFrameName("Blink_select.png");
                if (blinkFrame)
                {
                    blinkFrame->setPosition(Vec2(posX, posY));
                    addChild(blinkFrame, 6);
                }
            }
        }
    }

    // Right Side: Selected Character Preview (Half Portrait, Kanji Font, Name, OK Button)
    const float previewCenterX = winSize.width - 95.0f;

    // Kanji Font Logo
    std::string fontSpriteName = _previewHeroName + "_font.png";
    auto fontLogo = Sprite::createWithSpriteFrameName(fontSpriteName.c_str());
    if (fontLogo)
    {
        fontLogo->setPosition(Vec2(previewCenterX, winSize.height - 68));
        addChild(fontLogo, 4);
    }

    // Half Portrait
    std::string halfSpriteName = _previewHeroName + "_half.png";
    auto halfPortrait = Sprite::createWithSpriteFrameName(halfSpriteName.c_str());
    if (!halfPortrait)
        halfPortrait = Sprite::createWithSpriteFrameName("Naruto_half.png");
    if (halfPortrait)
    {
        halfPortrait->setPosition(Vec2(previewCenterX, winSize.height / 2));
        addChild(halfPortrait, 3);
    }

    // Hero Name text
    auto heroNameLabel = makeBMFont(_previewHeroName, Fonts::Yellow, 0.46f);
    heroNameLabel->setPosition(Vec2(previewCenterX, 76));
    addChild(heroNameLabel, 4);

    // Confirm OK Button
    auto okNormal = Sprite::createWithSpriteFrameName("yes_btn1.png");
    auto okSelect = Sprite::createWithSpriteFrameName("yes_btn2.png");
    auto okLabel = makeBMFont("OK", Fonts::White, 0.42f);
    okLabel->setPosition(Vec2(okNormal->getContentSize().width / 2, okNormal->getContentSize().height / 2));
    okNormal->addChild(okLabel);

    auto okBtn = MenuItemSprite::create(okNormal, okSelect, this, menu_selector(NetworkLobbyLayer::onConfirmHeroSelect));
    okBtn->setPosition(Vec2(previewCenterX, 42));

    auto confirmMenu = Menu::create(okBtn, nullptr);
    confirmMenu->setPosition(Vec2(0, 0));
    addChild(confirmMenu, 5);
}

void NetworkLobbyLayer::enterNetworkBattle()
{
    if (_battleEntered || (_session->state() != nsv2::network::SessionState::Loading && _session->state() != nsv2::network::SessionState::Battle))
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
    _totalRunningTime += dt;
    ++_frameCounter;
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
        _session->setLocalHero(kAllSelectHeroes[1]); // Sasuke for client default
    }

    // Real-time synchronization of lobby state (hero change, ready toggle, opponent join)
    if (_page == Page::Host && _session->state() == nsv2::network::SessionState::Lobby)
    {
        const auto &cfg = _session->matchConfig();
        std::ostringstream fp;
        fp << (_session->localReady() ? 1 : 0) << ':';
        for (const auto &slot : cfg.slots)
            fp << slot.heroName << ',' << (slot.ready ? 1 : 0) << ',' << slot.playerName << ';';
        if (fp.str() != _lobbyFingerprint)
        {
            _lobbyFingerprint = fp.str();
            renderPage();
        }
    }

    if (_session->state() == nsv2::network::SessionState::Loading || _session->state() == nsv2::network::SessionState::Battle)
    {
        enterNetworkBattle();
        return;
    }

    if (_session->state() != _lastSessionState)
        renderPage();

    if (_page == Page::Join && _session->networkActive() &&
        _session->state() == nsv2::network::SessionState::Idle && _frameCounter % 15 == 0)
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
            updateRoomListUI();
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
        setMessage("Failed to host room: " + error);
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
        setMessage("Discovery scan failed; enter manual IP:port. " + error);
    renderPage();
}

void NetworkLobbyLayer::onBack(Ref *sender)
{
    (void)sender;
    if (_leavingNetwork)
        return;

    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/cancel.ogg");

    if (_page == Page::HeroSelect)
    {
        _page = Page::Host;
        renderPage();
        return;
    }

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
    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/select.ogg");
    if (_ipEditBox && _ipEditBox->getText())
        _savedIpText = _ipEditBox->getText();
    _rooms.clear();
    _roomsFingerprint.clear();
    std::string error;
    _session->startScan(&error);
    if (!error.empty())
        setMessage("Refresh discovery: " + error);
    renderPage();
}

std::string NetworkLobbyLayer::selectedAddress() const
{
    std::string value = _ipEditBox && _ipEditBox->getText() ? _ipEditBox->getText() : _savedIpText;
    const size_t separator = value.find(':');
    if (separator != std::string::npos)
        value.resize(separator);
    return value.empty() ? "127.0.0.1" : value;
}

uint16_t NetworkLobbyLayer::selectedPort() const
{
    const std::string value = _ipEditBox && _ipEditBox->getText() ? _ipEditBox->getText() : _savedIpText;
    const size_t separator = value.find(':');
    if (separator == std::string::npos)
        return nsv2::network::kDefaultLanPort;
    const unsigned long parsed = std::strtoul(value.c_str() + separator + 1, nullptr, 10);
    return parsed > 0 && parsed <= 65535 ? static_cast<uint16_t>(parsed) : nsv2::network::kDefaultLanPort;
}

void NetworkLobbyLayer::onJoinManual(Ref *sender)
{
    (void)sender;
    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/confirm.ogg");
    if (_ipEditBox && _ipEditBox->getText())
        _savedIpText = _ipEditBox->getText();
    const std::string addr = selectedAddress();
    const uint16_t port = selectedPort();
    _session->stopScan();
    _page = Page::Host;
    std::string error;
    if (!_session->join(addr, port, "Client", &error))
        setMessage("Failed to join room: " + error);
    renderPage();
}

void NetworkLobbyLayer::onJoinRoom(Ref *sender)
{
    if (!sender)
        return;
    const int index = static_cast<MenuItem *>(sender)->getTag();
    if (index < 0 || static_cast<size_t>(index) >= _rooms.size())
        return;
    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/confirm.ogg");
    _session->stopScan();
    _page = Page::Host;
    std::string error;
    const auto &room = _rooms[static_cast<size_t>(index)];
    if (!_session->join(room.address, room.port, "Client", &error))
        setMessage("Failed to join room: " + error);
    renderPage();
}

void NetworkLobbyLayer::onReady(Ref *sender)
{
    (void)sender;
    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/select.ogg");
    std::string error;
    if (!_session->setLocalReady(!_session->localReady()))
    {
        if (error.empty())
            error = "hero not chosen or session not in lobby";
        setMessage("Failed to set ready: " + error);
    }
    renderPage();
}

void NetworkLobbyLayer::onOpenHeroSelect(Ref *sender)
{
    (void)sender;
    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/select.ogg");
    const auto &config = _session->matchConfig();
    const uint8_t mySlot = _session->localSlot();
    if (config.slots.size() > mySlot && !config.slots[mySlot].heroName.empty())
        _previewHeroName = config.slots[mySlot].heroName;
    else
        _previewHeroName = (_session->role() == nsv2::network::SessionRole::Host ? "Naruto" : "Sasuke");
    _lastClickedHeroIndex = -1;
    _page = Page::HeroSelect;
    renderPage();
}

void NetworkLobbyLayer::onHeroAvatarClicked(Ref *sender)
{
    if (!sender)
        return;
    const int index = static_cast<MenuItem *>(sender)->getTag();
    if (index < 0 || index >= kAllSelectHeroCount)
        return;

    const std::string clickedHero = kAllSelectHeroes[index];
    const float now = _totalRunningTime;

    // Check double-click on same hero
    if (_lastClickedHeroIndex == index && (now - _lastHeroClickTime) < 0.45f)
    {
        _previewHeroName = clickedHero;
        onConfirmHeroSelect(nullptr);
        return;
    }

    _lastClickedHeroIndex = index;
    _lastHeroClickTime = now;
    _previewHeroName = clickedHero;
    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/select.ogg");
    renderPage();
}

void NetworkLobbyLayer::onConfirmHeroSelect(Ref *sender)
{
    (void)sender;
    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/confirm.ogg");

    // Avoid picking same hero as opponent if already chosen
    const auto &config = _session->matchConfig();
    const uint8_t opponentSlot = _session->localSlot() == 0 ? 1 : 0;
    if (config.slots.size() > opponentSlot && config.slots[opponentSlot].heroName == _previewHeroName)
    {
        setMessage("Opponent already picked " + _previewHeroName + "! Choose another hero.");
        return;
    }

    _session->setLocalHero(_previewHeroName);
    _page = Page::Host;
    renderPage();
}

void NetworkLobbyLayer::onCycleHero(Ref *sender)
{
    onOpenHeroSelect(sender);
}

void NetworkLobbyLayer::onStart(Ref *sender)
{
    (void)sender;
    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/confirm.ogg");
    std::string error;
    bool success = false;
    if (_session->state() == nsv2::network::SessionState::Loading)
        success = _session->markLoaded(&error);
    else
        success = _session->startMatch(&error);
    if (!success)
        setMessage("Cannot start match yet: " + error);
    renderPage();
}

void NetworkLobbyLayer::onLeave(Ref *sender)
{
    (void)sender;
    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/cancel.ogg");
    _session->stop();
    _page = Page::Home;
    renderPage();
}

void NetworkLobbyLayer::editBoxEditingDidBegin(CCEditBox *editBox)
{
    (void)editBox;
}

void NetworkLobbyLayer::editBoxEditingDidEnd(CCEditBox *editBox)
{
    if (editBox && editBox == _ipEditBox && editBox->getText())
        _savedIpText = editBox->getText();
}

void NetworkLobbyLayer::editBoxTextChanged(CCEditBox *editBox, const std::string &text)
{
    (void)editBox;
    _savedIpText = text;
}

void NetworkLobbyLayer::editBoxReturn(CCEditBox *editBox)
{
    if (editBox && editBox == _ipEditBox)
    {
        if (editBox->getText())
            _savedIpText = editBox->getText();
        onJoinManual(nullptr);
    }
}

