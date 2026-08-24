#include "Defines.h"
#include "CharacterBase.h"
#include "GameLayer.h"
#include "BGLayer.h"
#include "HudLayer.h"
#include "StartMenu.h"
#include "Core/Provider.hpp"
#include "Core/Warrior/Flog.hpp"
#include "Core/Tower/Tower.hpp"
#include "GameMode/GameModeImpl.h"
#include "Constants/UiFlowKeys.hpp"
#include "Systems/BattleRuntimeSystem.hpp"
#include "Systems/SpawnSystem.hpp"
#include "Systems/SessionState.hpp"
#include "Network/LanNetworkRuntime.hpp"
#include "Network/AuthoritativeBattleState.hpp"
#include "Network/NetworkPresentationAdapter.hpp"

#include <set>

GameLayer *_gLayer = nullptr;
bool _isFullScreen = false;

namespace
{
// Network unit-id namespaces shared by both devices.
constexpr int kNetTowerIdBase = 100;
constexpr int kNetGuardianId = 200;
constexpr int kNetFlogIdBase = 300;

// Shared flog-name table used to encode flog mirrors in unit snapshots.
const char *kNetFlogNames[] = {
	FlogEnum::KotetsuFlog,	 // 0 (Konoha LV1)
	FlogEnum::IzumoFlog,	 // 1 (Konoha LV2)
	FlogEnum::KakashiFlog,	 // 2 (Konoha LV3)
	FlogEnum::FemalePainFlog,// 3 (Akatsuki LV1)
	FlogEnum::PainFlog,		 // 4 (Akatsuki LV2)
	FlogEnum::ObitoFlog,	 // 5 (Akatsuki LV3)
};
constexpr int kNetFlogNameCount = sizeof(kNetFlogNames) / sizeof(kNetFlogNames[0]);

int netFlogVariantForName(const std::string &name)
{
	for (int i = 0; i < kNetFlogNameCount; ++i)
	{
		if (is_same(name.c_str(), kNetFlogNames[i]))
			return i;
	}
	return 0;
}
} // namespace

void BattleRuntimeSystem::onGameStart(GameLayer *layer, bool skipInitFlogs, float flogSpawnDuration) const
{
	if (!layer)
		return;

	layer->_isStarted = true;
	layer->getHudLayer()->openingSprite->removeFromParent();
	layer->getHudLayer()->openingSprite = nullptr;
	layer->schedule(schedule_selector(GameLayer::updateGameTime), 1.0f);
	layer->schedule(schedule_selector(GameLayer::checkBackgroundMusic), 2.0f);
	// Host-authoritative minions: in a LAN battle only the HOST simulates and
	// spawns flogs; the client mirrors them from snapshots so both devices
	// always agree on minion count, position and HP.
	if (!skipInitFlogs)
	{
		const bool isNetworkClient = layer->_networkBattle && !layer->_isNetworkHost;
		layer->initFlogs();
		if (!isNetworkClient)
		{
			layer->schedule(schedule_selector(GameLayer::addFlog), flogSpawnDuration);
			layer->addFlog(0);
		}
	}

	layer->setKeyEventHandler();
			for (auto hero : layer->_CharacterArray)
		{
			hero->setWalkSpeed(hero->_originSpeed);
			if (hero->isCom() && !layer->_networkBattle)
				hero->doAI();
		}

}

void BattleRuntimeSystem::updateGameTime(GameLayer *layer) const
{
	if (!layer)
		return;

	layer->_second += 1;
	if (layer->_second == 60)
	{
		layer->_minute += 1;
		layer->_second = 0;
	}
	auto tempTime = format("{:02d}:{:02d}", layer->_minute, layer->_second);
	layer->getHudLayer()->gameClock->setString(tempTime.c_str());
	layer->setTotalTime(layer->getTotalTime() + 1);
}

void BattleRuntimeSystem::updateViewPoint(GameLayer *layer) const
{
	if (!layer || !layer->currentPlayer)
		return;

	Vec2 playerPoint;
	if (layer->ougisChar)
		playerPoint = layer->ougisChar->getPosition();
	else if (layer->controlChar)
		playerPoint = layer->controlChar->getPosition();
	else
		playerPoint = layer->currentPlayer->getPosition();

	int x = MAX(playerPoint.x, winSize.width / 2);
	int y = MAX(playerPoint.y, winSize.width / 2);
	x = MIN(x, (layer->currentMap->getMapSize().width * layer->currentMap->getTileSize().width) - winSize.width / 2);
	y = MIN(y, (layer->currentMap->getMapSize().height * layer->currentMap->getTileSize().height) - winSize.height / 2);
	layer->setPosition(Vec2(winSize.width / 2, y) - Vec2(x, y));
}

void SpawnSystem::initMatchUnits(GameLayer *layer) const
{
	if (!layer)
		return;
	layer->initTileMap();
	if (!layer->currentMap)
		return;
	layer->initEffects();
}

GameLayer::GameLayer()
{
	_battleRuntimeSystem = std::make_unique<BattleRuntimeSystem>();
	_spawnSystem = std::make_unique<SpawnSystem>();
	_sessionState = std::make_unique<SessionState>();

	mapId = 0;

	_isAttackButtonRelease = true;
	_isSkillFinish = true;

	_second = 0;
	_minute = 0;
	_playNum = 2;

	kEXPBound = 25;
	aEXPBound = 25;

	_isShacking = false;
	_isSurrender = false;
	_gameOverShown = false;
	_hasSpawnedGuardian = false;

	_isStarted = false;
	_isExiting = false;

	ougisChar = nullptr;
	controlChar = nullptr;

	_enableGear = true;
	_isOugis2Game = false;
	_isHardCoreGame = false;
	_isRandomChar = false;

	currentPlayer = nullptr;

	_isGear = false;
	_isPause = false;

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
	_lastPressedMovementKey = -100;
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_LINUX)
	_lastPressedMovementKey = -100;
	_window = GLView::sharedOpenGLView()->m_window;
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
	_lastPressedMovementKey = -100;
#endif
}

GameLayer::~GameLayer()
{
	_gLayer = nullptr;
	removeKeyEventHandler();
}

bool GameLayer::init()
{
	Texture2D::setDefaultAlphaPixelFormat(kCCTexture2DPixelFormat_RGBA8888);
	setTouchEnabled(true);

	_gLayer = this;
	const auto &gd = getGameModeHandler()->gd;
	_enableGear = gd.enableGear;
	_isHardCoreGame = gd.isHardCore;
	_isRandomChar = gd.isRandomChar;
	is4V4Mode = gd.use4v4SpawnLayout;
	playerGroup = gd.playerGroup;

	return Layer::init();
}

void GameLayer::onEnter()
{
	if (_isExiting)
	{
		onLeft();
		return;
	}

	if (currentPlayer && !ougisChar)
	{
		if (currentPlayer->getState() == State::WALK)
		{
			currentPlayer->idle();
		}
	}

	Layer::onEnter();

	if (_isSurrender)
	{
		onGameOver(false);
	}
}

void GameLayer::onExit()
{
	Layer::onExit();

	if (_isExiting)
	{
		_isExiting = false;
	}
}

void GameLayer::onHUDInitialized(const OnHUDInitializedCallback &callback)
{
	callbackssList.push_back(callback);
}

bool GameLayer::isHUDInit()
{
	return isHUDInitialized;
}

void GameLayer::initTileMap()
{
	int mapCount = getMapCount();
	if (mapCount == 0)
	{
		CCMessageBox("Not found any map", "[Error] Not found any map");
		return;
	}
	if (_networkBattle)
		mapId = nsv2::network::sharedLanSession().matchConfig().mapId;
	else
	{
		setRand();
		mapId = random(mapCount) + 1;
	}
	currentMap = TMXTiledMap::create(GetMapPath(mapId));
	addChild(currentMap, kMapOrder);
}

void GameLayer::initGard()
{
	// Guardian is a gameplay entity: in LAN battles only the host spawns it.
	// The client receives it as a mirror via unit snapshots.
	if (_networkBattle && !_isNetworkHost)
		return;

	uint32_t index = netRandom(2);
	auto guardianName = index == 0 ? GuardianEnum::Roshi : GuardianEnum::Han;
	auto guardianGroup = playerGroup == Group::Konoha ? Group::Akatsuki : Group::Konoha;
	auto guardian = Provider::create(guardianName, Role::Com, guardianGroup);

	if (playerGroup == Group::Konoha)
	{
		guardian->setPosition(Vec2(2800, 80));
		guardian->setSpawnPoint(Vec2(2800, 80));
	}
	else
	{
		guardian->setPosition(Vec2(272, 80));
		guardian->setSpawnPoint(Vec2(272, 80));
	}

	addChild(guardian, -guardian->getPositionY());
	guardian->setLV(6);
	guardian->setHPbar();
	guardian->setShadows();
	guardian->setCharId(_CharacterArray.size() + 1);

	guardian->idle();
	guardian->setSkillEffect("smk");

	guardian->doAI();

	_CharacterArray.push_back(guardian);
	_hudLayer->addMapIcon();

	_hasSpawnedGuardian = true;
}

void GameLayer::initHeros()
{
	_spawnSystem->initMatchUnits(this);
	if (currentMap == nullptr)
	{
		// initTileMap() failed (e.g. no map asset bundled). The error has
		// already been surfaced to the user via CCMessageBox; bail out here
		// instead of dereferencing the null tilemap below.
		return;
	}

	addSprites("UI/hpBar/hpBar.plist");

	auto handler = getGameModeHandler();
	auto herosDataVector = handler->getHerosArray();

	_isOugis2Game = true;

	TMXObjectGroup *group = currentMap->objectGroupNamed("object");
	if (group == nullptr)
	{
		CCMessageBox("Map is missing the 'object' layer", "[Error] Bad map");
		return;
	}
	CCArray *objectArray = group->getObjects();

	// 4v4 spawn layout
	if (is4V4Mode)
	{
		auto &hero1 = herosDataVector.at(0);
		auto &hero5 = herosDataVector.at(4);

		hero1.setSpawnPoint(getCustomSpawnPoint(hero1));
		addHero(hero1, 1);

		hero5.setSpawnPoint(getCustomSpawnPoint(hero5));
		addHero(hero5, 5);
	}

	int i = 0;
	for (auto &data : herosDataVector)
	{
		if (data.isInit)
			continue;

		int mapPos = i;
		if (data.group == Group::Akatsuki)
		{
			if (mapPos <= MapPosCount - 1)
				mapPos += MapPosCount;
		}
		else
		{
			if (mapPos > MapPosCount - 1)
				mapPos -= MapPosCount;
		}

		Ref *mapObject = objectArray->objectAtIndex(mapPos);
		auto mapdict = (CCDictionary *)mapObject;
		int x = ((CCString *)mapdict->objectForKey("x"))->intValue();
		int y = ((CCString *)mapdict->objectForKey("y"))->intValue();
		data.setSpawnPoint(Vec2(x, y));

		if (is4V4Mode)
		{
			int id = i + 2;
			if (id >= 5)
				id++;
			addHero(data, id);
		}
		else
		{
			addHero(data, i + 1);
		}
		i++;
	}

	// Tower HP bar color depends on currentPlayer group, so towers must be
	// initialized after at least one hero/player is created.
	initTower();
	initFlogs();

	schedule(schedule_selector(GameLayer::updateViewPoint), 0.00f);
	if (_networkBattle)
		schedule(schedule_selector(GameLayer::updateNetworkBattle), 0.00f);
	scheduleOnce(schedule_selector(GameLayer::playGameOpeningAnimation), 0.5f);
}

Hero *GameLayer::addHero(const HeroData &data, int charId)
{
	return addHero(data.name, data.role, data.group, data.spawnPoint, charId);
}

Hero *GameLayer::addHero(const string &name, Role role, Group group, Vec2 spawnPoint, int charId)
{
	auto hero = Provider::create(name, role, group);
	if (hero->isPlayer())
	{
		currentPlayer = hero;
	}
	hero->setPosition(spawnPoint);
	hero->setSpawnPoint(spawnPoint);
	// NOTE: Set all characters speed to zero. (Control movement before game real start)
	hero->setWalkSpeed(0);
	if (group == Group::Akatsuki)
	{
		hero->_isFlipped = true;
		hero->setFlipX(true);
	}
	hero->setHPbar();
	hero->setShadows();
	hero->idle();
	hero->setCharId(charId);
	hero->schedule(schedule_selector(CharacterBase::setRestore2), 1.0f);

	addChild(hero, -hero->getPositionY());
	_CharacterArray.push_back(hero);

	getGameModeHandler()->onCharacterInit(hero);
	return hero;
}

void GameLayer::playGameOpeningAnimation(float dt)
{
	getHudLayer()->playGameOpeningAnimation();

	setRand();
	auto path = random(2) == 0 ? "Audio/Menu/battle_start1.ogg" : "Audio/Menu/battle_start.ogg";
	SimpleAudioEngine::sharedEngine()->playEffect(path);

	scheduleOnce(schedule_selector(GameLayer::onGameStart), 0.75f);
}

void GameLayer::onGameStart(float dt)
{
	auto handler = getGameModeHandler();
	_battleRuntimeSystem->onGameStart(this, handler->skipInitFlogs, handler->flogSpawnDuration);

	getGameModeHandler()->onGameStart();
}

void GameLayer::initFlogs()
{
	addSprites("UI/hpBar/flogBar.plist");

	kName = FlogEnum::KotetsuFlog;
	aName = FlogEnum::FemalePainFlog;
}

void GameLayer::addFlog(float dt)
{
	auto KonohaFlogName = kName;
	auto AkatsukiFlogName = aName;

	int i;
	Flog *flog;
	float mainPosY;
	for (i = 0; i < kFlogCount; i++)
	{
		flog = Flog::create();
		flog->setID(KonohaFlogName, Role::Flog, Group::Konoha);
		if (_networkBattle && _isNetworkHost)
			flog->setCharId(kNetFlogIdBase + _netNextFlogId++);
		if (i < kFlogCount / 2)
			mainPosY = (5.5 - i / 1.5) * 32;
		else
			mainPosY = (3.5 - i / 1.5) * 32;
		flog->_mainPosY = mainPosY;
		flog->setPosition(Vec2(13 * 32, flog->_mainPosY));
		flog->setHPbar();
		flog->idle();
		flog->doAI();
		_KonohaFlogArray.push_back(flog);
		addChild(flog, -int(flog->getPositionY()));
	}

	for (i = 0; i < kFlogCount; i++)
	{
		flog = Flog::create();
		flog->setID(AkatsukiFlogName, Role::Flog, Group::Akatsuki);
		if (_networkBattle && _isNetworkHost)
			flog->setCharId(kNetFlogIdBase + _netNextFlogId++);
		if (i < kFlogCount / 2)
			mainPosY = (5.5 - i / 1.5) * 32;
		else
			mainPosY = (3.5 - i / 1.5) * 32;
		flog->_mainPosY = mainPosY;
		flog->setPosition(Vec2(83 * 32, flog->_mainPosY));
		flog->setHPbar();
		flog->idle();
		flog->doAI();
		_AkatsukiFlogArray.push_back(flog);
		addChild(flog, -flog->getPositionY());
	}
}

void GameLayer::initTower()
{
	addSprites(format("Unit/Tower/Tower{}.plist", mapId));

	TMXObjectGroup *metaGroup = currentMap->objectGroupNamed("meta");
	CCArray *metaArray = metaGroup->getObjects();
	Ref *pObject;
	int i = 0;

	CCARRAY_FOREACH(metaArray, pObject)
	{
		auto dict = (CCDictionary *)pObject;

		int metaX = ((CCString *)dict->objectForKey("x"))->intValue();
		int metaY = ((CCString *)dict->objectForKey("y"))->intValue();

		int metaWidth = ((CCString *)dict->objectForKey("width"))->intValue();
		int metaHeight = ((CCString *)dict->objectForKey("height"))->intValue();

		auto name = ((CCString *)dict->objectForKey("name"))->m_sString;

		Tower *tower = Tower::create();
		char towerName[7] = "abcdef";
		strncpy(towerName, name.c_str(), 6);
		if (is_same(towerName, kGroupKonoha))
		{
			tower->setID(name, Role::Tower, Group::Konoha);
		}
		else
		{
			tower->setID(name, Role::Tower, Group::Akatsuki);
			tower->setFlipX(true);
			tower->_isFlipped = true;
		}
		float posX = metaX + metaWidth / 2;
		float posY = metaY + metaHeight / 2;
		tower->setPosition(Vec2(posX, posY));
		tower->setSpawnPoint(Vec2(posX, posY));
		tower->setCharId(i + 1);

		if (i == 1 || i == 4)
		{
			if (is4V4Mode)
			{
				tower->setMaxHPValue(80000, false);
			}
			else
			{
				tower->setMaxHPValue(50000, false);
			}
			tower->setHPValue(tower->getMaxHP(), false);
		}
		tower->setHPbar();
		tower->_hpBar->setVisible(false);
		tower->idle();
		addChild(tower, -tower->getPositionY());

		_TowerArray.push_back(tower);
		i++;
	}
}

void GameLayer::initEffects()
{
	addSprites("Effects/SkillEffect.plist");
	skillEffectBatch = SpriteBatchNode::create("Effects/SkillEffect.png");
	addChild(skillEffectBatch, kSkillEffectOrder);

	addSprites("Effects/DamageEffect.plist");
	damageEffectBatch = SpriteBatchNode::create("Effects/DamageEffect.png");
	addChild(damageEffectBatch, kDamageEffectOrder);

	addSprites("Effects/Shadows.plist");
	shadowBatch = SpriteBatchNode::create("Effects/Shadows.png");
	addChild(shadowBatch, kShadowOrder);
}

void GameLayer::updateGameTime(float dt)
{
	_battleRuntimeSystem->updateGameTime(this);
}

void GameLayer::updateViewPoint(float dt)
{
	_battleRuntimeSystem->updateViewPoint(this);
}

void GameLayer::setTowerState(int charId)
{
	_hudLayer->setTowerState(charId);
}

void GameLayer::updateHudSkillButtons()
{
	_hudLayer->updateSkillButtons();
}

void GameLayer::setHPLose(float percent)
{
	_hudLayer->setHPLose(percent);
}

void GameLayer::setCKRLose(bool isCRK2)
{
	_hudLayer->setCKRLose(isCRK2);
}

void GameLayer::setReport(const string &slayer, const string &dead, uint32_t killNum)
{
	_hudLayer->setReport(slayer, dead, killNum);
}

void GameLayer::resetStatusBar()
{
	_hudLayer->status_hpbar->setRotation(0);
}

void GameLayer::setCoin(const char *value)
{
	_hudLayer->setCoin(value);
}

void GameLayer::removeOugisMark(int type)
{
	if (type == 1)
	{
		if (_hudLayer->skill4Button)
		{
			if (_hudLayer->skill4Button->lockLabel1)
			{
				_hudLayer->skill4Button->lockLabel1->removeFromParent();
				_hudLayer->skill4Button->lockLabel1 = nullptr;
			}
		}
	}
	else
	{
		if (_hudLayer->skill5Button)
		{
			if (_hudLayer->skill5Button->lockLabel1)
			{
				_hudLayer->skill5Button->lockLabel1->removeFromParent();
				_hudLayer->skill5Button->lockLabel1 = nullptr;
			}
		}
	}
}

void GameLayer::checkTower()
{
	// Win/lose is a match outcome: only the HOST decides it. The client is
	// informed through the MatchEnd message so both devices always agree.
	if (_networkBattle && !_isNetworkHost)
		return;

	int konohaTowerCount = 0;
	int akatsukiTowerCount = 0;

	for (auto tower : _TowerArray)
	{
		if (tower->isKonohaGroup())
			konohaTowerCount++;
		else
			akatsukiTowerCount++;
	}

	if (konohaTowerCount == 2)
	{
		aName = FlogEnum::PainFlog;
		kEXPBound = 50;
	}
	else if (konohaTowerCount == 1)
	{
		aName = FlogEnum::ObitoFlog;
		kEXPBound = 100;
	}

	if (akatsukiTowerCount == 2)
	{
		kName = FlogEnum::IzumoFlog;
		aEXPBound = 50;
	}
	else if (akatsukiTowerCount == 1)
	{
		kName = FlogEnum::KakashiFlog;
		aEXPBound = 100;
	}

	for (auto hero : getGameLayer()->_CharacterArray)
	{
		if (hero->isNotCom())
			continue;

		if (hero->isKonohaGroup())
		{
			hero->battleCondiction = konohaTowerCount - akatsukiTowerCount;
			if (konohaTowerCount == 1)
			{
				hero->isBaseDanger = true;
			}
		}
		else
		{
			hero->battleCondiction = akatsukiTowerCount - konohaTowerCount;
			if (_isHardCoreGame)
			{
				if (akatsukiTowerCount == 1)
				{
					hero->isBaseDanger = true;
				}
			}
		}
	}

	if (konohaTowerCount == 0 || akatsukiTowerCount == 0)
	{
		const Group winnerGroup = konohaTowerCount != 0 ? Group::Konoha : Group::Akatsuki;
		if (_networkBattle)
		{
			std::string error;
			nsv2::network::sharedLanSession().sendMatchEnd(
				winnerGroup == Group::Konoha ? 0 : 1, &error);
		}
		if (playerGroup == Group::Konoha)
			onGameOver(winnerGroup == Group::Konoha);
		else
			onGameOver(winnerGroup == Group::Akatsuki);
	}
}

void GameLayer::clearDoubleClick()
{
	if (_hudLayer->skill1Button->getDoubleSkill() &&
		_hudLayer->skill1Button->_clickNum >= 1)
	{
		_hudLayer->skill1Button->setFreezeAction(nullptr);
		_hudLayer->skill1Button->beganAnimation();
	}
}

uint32_t GameLayer::netRandom(uint32_t bound)
{
	if (bound == 0)
		return 0;
	// xorshift32 seeded from MatchConfig::seed so every device produces the
	// same sequence without relying on wall-clock srand().
	uint32_t x = _netRngState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	_netRngState = x == 0 ? 0x9E3779B9u : x;
	return _netRngState % bound;
}

vector<Flog *> *GameLayer::flogArrayForKind(uint8_t kind)
{
	if (kind == static_cast<uint8_t>(nsv2::network::NetUnitKind::FlogKonoha))
		return &_KonohaFlogArray;
	if (kind == static_cast<uint8_t>(nsv2::network::NetUnitKind::FlogAkatsuki))
		return &_AkatsukiFlogArray;
	return nullptr;
}

void GameLayer::enableNetworkBattle(uint8_t localSlot)
{
	_networkBattle = true;
	_networkLocalSlot = localSlot;
	_isNetworkHost = nsv2::network::sharedLanSession().role() == nsv2::network::SessionRole::Host;
	_networkTick = 0;
	_networkAccumulator = 0.0f;
	_lastNetworkJoystickSendTime = 0.0f;
	_networkBattleTime = 0.0f;

	const uint32_t seed = nsv2::network::sharedLanSession().matchConfig().seed;
	_netRngState = seed != 0 ? seed : 0x4E535632u;
	_netLastAppliedTick = 0;
	_netLastCharState[0] = _netLastCharState[1] = 0;
	_netNextFlogId = 0;
	_netCharLerp.clear();
	_netUnitLerp.clear();
	_netFlogMirrors.clear();
	_netGuardianMirror = nullptr;
	_netGuardianUnitId = -1;

	_netPresentation = std::make_unique<nsv2::network::NetworkPresentationAdapter>();
	_netPresentation->initialize(this, _networkLocalSlot);

	if (_isNetworkHost)
	{
		_authBattleState = std::make_unique<nsv2::network::AuthoritativeBattleState>();
		_authBattleState->initializeMatch(nsv2::network::sharedLanSession().matchConfig());
	}
}

void GameLayer::advanceNetworkInterpolation(float dt)
{
	if (!_networkBattle || _isNetworkHost)
		return;

	if (_netPresentation)
	{
		_netPresentation->update(dt);
	}

	const float advance = dt / kNetInterpPeriod;
	auto stepLerp = [advance](NetLerp &lerp) {
		lerp.t = MIN(1.0f, lerp.t + advance);
		const float k = lerp.t;
		return Vec2(lerp.from.x * (1.0f - k) + lerp.to.x * k,
					lerp.from.y * (1.0f - k) + lerp.to.y * k);
	};

	for (auto &entry : _netUnitLerp)
	{
		Vec2 position;
		auto flogIt = _netFlogMirrors.find(entry.first);
		if (flogIt != _netFlogMirrors.end() && flogIt->second)
		{
			position = stepLerp(entry.second);
			flogIt->second->setPosition(position);
			continue;
		}
		if (_netGuardianMirror && entry.first == _netGuardianUnitId)
		{
			position = stepLerp(entry.second);
			_netGuardianMirror->setPosition(position);
		}
	}
}

void GameLayer::buildAndSendHostSnapshot()
{
	using namespace nsv2::network;
	auto &session = sharedLanSession();

	StateSnapshot snapshot;
	snapshot.matchId = session.matchConfig().matchId;
	snapshot.tick = _networkTick;
	snapshot.elapsedSeconds = static_cast<uint16_t>(MIN(static_cast<uint32_t>(0xFFFF), getTotalTime()));
	snapshot.sessionEpoch = session.matchConfig().seed ^ snapshot.matchId;

	for (size_t i = 0; i < _CharacterArray.size() && i < session.matchConfig().maxPlayers; ++i)
	{
		auto *character = _CharacterArray[i];
		if (!character)
			continue;
		snapshot.characters.push_back({static_cast<uint8_t>(i),
			static_cast<int32_t>(character->getPositionX() * 100.0f),
			static_cast<int32_t>(character->getPositionY() * 100.0f),
			character->getHP(), character->getCKR(),
			static_cast<uint8_t>(character->getState()), character->_isFlipped});
	}

	// Towers share deterministic charIds (map object order) on both devices.
	for (auto *tower : _TowerArray)
	{
		if (!tower)
			continue;
		snapshot.units.push_back({static_cast<uint16_t>(kNetTowerIdBase + tower->getCharId()),
			NetUnitKind::Tower, 0,
			static_cast<int32_t>(tower->getPositionX() * 100.0f),
			static_cast<int32_t>(tower->getPositionY() * 100.0f),
			tower->getHP(), static_cast<uint8_t>(tower->getState()), tower->_isFlipped});
	}

	// Guardian (hardcore modes) - spawned by the host only.
	for (auto *character : _CharacterArray)
	{
		if (!character || !character->isGuardian() || character->getState() == State::DEAD)
			continue;
		const uint8_t variant = (character->getName() == GuardianEnum::Han ? 1 : 0) |
								(character->getGroup() == Group::Akatsuki ? 2 : 0);
		snapshot.units.push_back({static_cast<uint16_t>(kNetGuardianId),
			NetUnitKind::Guardian, variant,
			static_cast<int32_t>(character->getPositionX() * 100.0f),
			static_cast<int32_t>(character->getPositionY() * 100.0f),
			character->getHP(), static_cast<uint8_t>(character->getState()), character->_isFlipped});
	}

	// Flogs - host-simulated minions mirrored to the client.
	for (auto *array : {&_KonohaFlogArray, &_AkatsukiFlogArray})
	{
		for (auto *flog : *array)
		{
			if (!flog || flog->getCharId() < kNetFlogIdBase || flog->getState() == State::DEAD)
				continue;
			snapshot.units.push_back({static_cast<uint16_t>(flog->getCharId()),
				flog->isKonohaGroup() ? NetUnitKind::FlogKonoha : NetUnitKind::FlogAkatsuki,
				static_cast<uint8_t>(netFlogVariantForName(flog->getName())),
				static_cast<int32_t>(flog->getPositionX() * 100.0f),
				static_cast<int32_t>(flog->getPositionY() * 100.0f),
				flog->getHP(), static_cast<uint8_t>(flog->getState()), flog->_isFlipped});
		}
	}

	if (_authBattleState)
	{
		_authBattleState->drainCombatEvents(snapshot.combatEvents);
	}

	snapshot.stateChecksum = computeStateChecksum(snapshot);

	std::string error;
	session.sendSnapshot(snapshot, &error);
}

void GameLayer::sendLocalClientState()
{
	using namespace nsv2::network;
	auto &session = sharedLanSession();
	if (_networkLocalSlot >= _CharacterArray.size() || !_CharacterArray[_networkLocalSlot])
		return;

	auto *hero = _CharacterArray[_networkLocalSlot];
	StateSnapshot state;
	state.matchId = session.matchConfig().matchId;
	state.tick = _networkTick;
	state.elapsedSeconds = 0;
	state.characters.push_back({_networkLocalSlot,
		static_cast<int32_t>(hero->getPositionX() * 100.0f),
		static_cast<int32_t>(hero->getPositionY() * 100.0f),
		hero->getHP(), hero->getCKR(),
		static_cast<uint8_t>(hero->getState()), hero->_isFlipped});

	std::string error;
	session.sendClientState(state, &error);
}

void GameLayer::updateNetworkBattle(float dt)
{
	if (!_networkBattle)
		return;
	_networkBattleTime += dt;
	using namespace nsv2::network;
	auto &session = sharedLanSession();
	session.poll();

	if (session.state() == nsv2::network::SessionState::Finished || session.state() == nsv2::network::SessionState::Error)
	{
		std::vector<SessionNotice> notices;
		session.drainNotices(notices);
		onGameOver(true);
		return;
	}

	// Relay discrete actions immediately so animations/host combat resolution
	// are not quantized to the tick accumulator.
	std::vector<InputCommand> commands;
	session.drainInputCommands(commands);
	for (const auto &command : commands)
	{
		applyNetworkCommand(command);
		if (_isNetworkHost && _authBattleState)
		{
			_authBattleState->submitPlayerInput(command);
		}
	}

	// Smooth remote entity rendering between snapshots.
	advanceNetworkInterpolation(dt);

	const uint16_t tickRate = session.matchConfig().tickRate == 0 ? 30 : session.matchConfig().tickRate;
	const float step = 1.0f / static_cast<float>(tickRate);
	_networkAccumulator += MIN(dt, 0.25f);
	while (_networkAccumulator >= step)
	{
		_networkAccumulator -= step;
		++_networkTick;

		if (_isNetworkHost)
		{
			if (_authBattleState)
			{
				_authBattleState->stepSimulation(_networkTick);
			}

			// Host broadcast at half tick rate (~15 Hz).
			if (_networkTick % 2 == 0)
				buildAndSendHostSnapshot();
		}
		else if (_networkTick % 3 == 0)
		{
			// Client reports its own hero state at ~10 Hz.
			sendLocalClientState();
		}
	}

	std::vector<uint8_t> matchEnds;
	session.drainMatchEnds(matchEnds);
	for (const auto &winner : matchEnds)
	{
		const Group winnerGroup = winner == 0 ? Group::Konoha : Group::Akatsuki;
		onGameOver(winnerGroup == playerGroup);
		return;
	}

	if (_isNetworkHost)
	{
		// Host: consume client-reported hero state.
		std::vector<StateSnapshot> clientStates;
		session.drainClientStates(clientStates);
		for (const auto &state : clientStates)
			applyClientState(state);
	}
	else
	{
		// Client: apply newest-first authoritative world snapshots.
		std::vector<StateSnapshot> snapshots;
		session.drainSnapshots(snapshots);
		for (auto &snapshot : snapshots)
		{
			if (snapshot.matchId != session.matchConfig().matchId)
				continue;
			// Discard stale/out-of-order snapshots instead of rewinding state.
			if (snapshot.tick <= _netLastAppliedTick)
				continue;
			_netLastAppliedTick = snapshot.tick;

			if (_netPresentation)
			{
				_netPresentation->applyAuthoritativeSnapshot(snapshot, currentPlayer ? static_cast<float>(currentPlayer->getWalkSpeed()) : 3.0f);
			}

			for (const auto &charState : snapshot.characters)
				applyCharacterSnapshot(charState);
			applyNetworkUnits(snapshot.units);

			// Match clock follows the host so timers cannot drift apart.
			if (getHudLayer() && getHudLayer()->gameClock)
			{
				setTotalTime(snapshot.elapsedSeconds);
				auto tempTime = format("{:02d}:{:02d}", snapshot.elapsedSeconds / 60, snapshot.elapsedSeconds % 60);
				getHudLayer()->gameClock->setString(tempTime.c_str());
			}
		}
	}
}

void GameLayer::applyClientState(const nsv2::network::StateSnapshot &state)
{
	using namespace nsv2::network;
	if (state.characters.empty())
		return;
	const auto &characterState = state.characters[0];
	if (characterState.slot >= _CharacterArray.size())
		return;
	auto *character = _CharacterArray[characterState.slot];
	if (!character || character->getState() == State::DEAD)
		return;

	// The client owns this hero's position; the host trusts it for combat
	// resolution so both simulations fight over the same coordinates.
	auto &lerp = _netCharLerp[characterState.slot];
	lerp.from = character->getPosition();
	lerp.to = Vec2(characterState.x / 100.0f, characterState.y / 100.0f);
	lerp.t = 0.0f;
	character->setFlipX(characterState.flipped);

	const auto nextState = static_cast<State>(characterState.state);
	if (nextState == State::WALK && character->getState() != State::WALK)
		character->walk(Vec2(characterState.flipped ? -1.0f : 1.0f, 0.0f));
	else if (nextState == State::IDLE && character->getState() != State::IDLE &&
			 character->getState() != State::NATTACK && character->getState() != State::SATTACK)
		character->idle();
}

void GameLayer::applyNetworkCommand(const nsv2::network::InputCommand &command)
{
	if (command.playerSlot >= _CharacterArray.size())
		return;
	auto *character = _CharacterArray[command.playerSlot];
	if (!character)
		return;
	// Movement is NOT input-driven anymore: positions flow through
	// Snapshot (host -> client) and ClientState (client -> host), so Move
	// commands are ignored everywhere. Only discrete actions are relayed.
	if (command.action == nsv2::network::ActionType::Move)
		return;
	if (command.action == nsv2::network::ActionType::NormalAttack)
	{
		character->attack(NAttack);
	}
	else if (command.action == nsv2::network::ActionType::Skill1)
	{
		character->attack(SKILL1);
	}
	else if (command.action == nsv2::network::ActionType::Skill2)
	{
		character->attack(SKILL2);
	}
	else if (command.action == nsv2::network::ActionType::Skill3)
	{
		character->attack(SKILL3);
	}
	else if (command.action == nsv2::network::ActionType::Skill4)
	{
		character->attack(OUGIS1);
	}
	else if (command.action == nsv2::network::ActionType::Skill5)
	{
		character->attack(OUGIS2);
	}
	else if (command.action == nsv2::network::ActionType::Item1)
	{
		character->setItem(Item1);
	}
}

void GameLayer::applyCharacterSnapshot(const nsv2::network::CharacterSnapshot &state)
{
	if (state.slot >= _CharacterArray.size() || state.slot >= 2)
		return;
	auto *character = _CharacterArray[state.slot];
	if (!character)
		return;

	const bool wasDead = _netLastCharState[state.slot] == static_cast<uint8_t>(State::DEAD);
	const bool isDead = state.state == static_cast<uint8_t>(State::DEAD);

	if (isDead && !wasDead)
	{
		// Authoritative death: replicate the outcome of CharacterBase::dead()
		// without triggering local reborn scheduling (host owns reborn timing).
		character->stopAllActions();
		character->setState(State::DEAD);
		character->setHPValue(0, false);
		if (character->_hpBar)
		{
			character->_hpBar->removeFromParent();
			character->_hpBar = nullptr;
		}
		character->runAction(FadeOut::create(0.5f));
		character->_deadNum++;
		if (state.slot == _networkLocalSlot)
		{
			if (getHudLayer())
			{
				if (getHudLayer()->hpLabel)
					getHudLayer()->hpLabel->setString("0");
				if (getHudLayer()->status_hpbar)
					getHudLayer()->status_hpbar->setOpacity(0);
				if (getHudLayer()->deadLabel)
				{
					auto deadStr = getHudLayer()->deadLabel->getString();
					int deads = to_int(deadStr) + 1;
					getHudLayer()->deadLabel->setString(to_cstr(deads));
				}
			}
		}
		CCNotificationCenter::sharedNotificationCenter()->postNotification("updateMap", character);
		_netLastCharState[state.slot] = state.state;
		return;
	}

	if (!isDead && wasDead)
	{
		// Authoritative revival: restore what Hero::reborn restores.
		character->setPosition(Vec2(state.x / 100.0f, state.y / 100.0f));
		CCNotificationCenter::sharedNotificationCenter()->addObserver(
			character, callfuncO_selector(CharacterBase::acceptAttack), "acceptAttack", nullptr);
		character->setOpacity(255);
		character->setVisible(true);
		character->setHPValue(state.hp, false);
		character->setHPbar();
		character->setFlipX(state.flipped);
		character->_isFlipped = state.flipped;
		character->setState(static_cast<State>(state.state));
		character->idle();
		_netLastCharState[state.slot] = state.state;
		_netCharLerp.erase(state.slot);
		return;
	}

	_netLastCharState[state.slot] = state.state;

	// Host is the single source of truth for HP/CKR: no local writer remains.
	if (character->getHP() != state.hp)
	{
		character->setHPValue(state.hp, true);
		if (state.slot == _networkLocalSlot && getHudLayer() && getHudLayer()->status_hpbar)
			setHPLose(character->getHpPercent());
	}
	character->setCKR(state.ckr);

	if (state.slot == _networkLocalSlot)
		return; // Own movement stays locally simulated (reported to host).

	// Remote hero: interpolate toward the authoritative position.
	auto &lerp = _netCharLerp[state.slot];
	lerp.from = character->getPosition();
	lerp.to = Vec2(state.x / 100.0f, state.y / 100.0f);
	lerp.t = 0.0f;
	if (character->_isFlipped != state.flipped)
	{
		character->_isFlipped = state.flipped;
		character->setFlipX(state.flipped);
	}
	const auto nextState = static_cast<State>(state.state);
	if (nextState == State::WALK && character->getState() != State::WALK)
		character->walk(Vec2(state.flipped ? -1.0f : 1.0f, 0.0f));
	else if (nextState == State::IDLE && character->getState() == State::WALK)
		character->idle();
}

void GameLayer::applyNetworkUnits(const std::vector<nsv2::network::UnitSnapshot> &units)
{
	using namespace nsv2::network;
	std::set<int> aliveIds;

	for (const auto &unit : units)
	{
		aliveIds.insert(unit.unitId);

		if (unit.kind == NetUnitKind::Tower)
		{
			const int towerCharId = unit.unitId - kNetTowerIdBase;
			for (auto *tower : _TowerArray)
			{
				if (!tower || tower->getCharId() != towerCharId ||
					tower->getState() == State::DEAD)
					continue;
				if (tower->getHP() != unit.hp)
					tower->setHPValue(unit.hp, true);
				break;
			}
			continue;
		}

		if (unit.kind == NetUnitKind::Guardian)
		{
			if (!_netGuardianMirror || _netGuardianUnitId != unit.unitId)
			{
				// Create the guardian mirror exactly like initGard does, but
				// driven purely by host data (name/group packed in variant).
				const auto guardianName = (unit.variant & 0x01) ? GuardianEnum::Han : GuardianEnum::Roshi;
				const auto guardianGroup = (unit.variant & 0x02) ? Group::Akatsuki : Group::Konoha;
				auto *guardian = Provider::create(guardianName, Role::Com, guardianGroup);
				guardian->setPosition(Vec2(unit.x / 100.0f, unit.y / 100.0f));
				guardian->setSpawnPoint(guardian->getPosition());
				guardian->setLV(6);
				guardian->setHPbar();
				guardian->setShadows();
				guardian->setCharId(_CharacterArray.size() + 1);
				guardian->idle();
				guardian->setSkillEffect("smk");
				// NO doAI(): the mirror never makes gameplay decisions.
				addChild(guardian, -guardian->getPositionY());
				_CharacterArray.push_back(guardian);
				_hasSpawnedGuardian = true;
				_netGuardianMirror = guardian;
				_netGuardianUnitId = unit.unitId;
				_netUnitLerp.erase(unit.unitId);
			}
			auto &lerp = _netUnitLerp[unit.unitId];
			lerp.from = _netGuardianMirror->getPosition();
			lerp.to = Vec2(unit.x / 100.0f, unit.y / 100.0f);
			lerp.t = 0.0f;
			if (_netGuardianMirror->getHP() != unit.hp)
				_netGuardianMirror->setHPValue(unit.hp, true);
			continue;
		}

		// Flog mirrors.
		auto *mirror = _netFlogMirrors.count(unit.unitId) ? _netFlogMirrors[unit.unitId] : nullptr;
		if (!mirror)
		{
			const int nameIndex = MIN(static_cast<int>(unit.variant), kNetFlogNameCount - 1);
			auto *flog = Flog::create();
			flog->setID(kNetFlogNames[nameIndex], Role::Flog,
						unit.kind == NetUnitKind::FlogKonoha ? Group::Konoha : Group::Akatsuki);
			flog->_mainPosY = unit.y / 100.0f;
			flog->setPosition(Vec2(unit.x / 100.0f, unit.y / 100.0f));
			flog->setHPbar();
			flog->idle();
			// NO doAI(): pure mirror of the host simulation.
			auto *array = flogArrayForKind(static_cast<uint8_t>(unit.kind));
			if (array)
				array->push_back(flog);
			_netFlogMirrors[unit.unitId] = flog;
			addChild(flog, -int(flog->getPositionY()));
			mirror = flog;
			_netUnitLerp.erase(unit.unitId);
		}
		auto &lerp = _netUnitLerp[unit.unitId];
		lerp.from = mirror->getPosition();
		lerp.to = Vec2(unit.x / 100.0f, unit.y / 100.0f);
		lerp.t = 0.0f;
		if (mirror->getHP() != unit.hp)
			mirror->setHPValue(unit.hp, true);
		if (mirror->_isFlipped != unit.flipped)
		{
			mirror->_isFlipped = unit.flipped;
			mirror->setFlipX(unit.flipped);
		}
	}

	// Entities absent from the host snapshot have died on the host: run the
	// regular death flow so arrays/z-order clean up identically on both sides.
	for (auto it = _netFlogMirrors.begin(); it != _netFlogMirrors.end();)
	{
		if (aliveIds.count(it->first))
		{
			++it;
			continue;
		}
		if (it->second && it->second->getState() != State::DEAD)
			it->second->dead();
		_netUnitLerp.erase(it->first);
		it = _netFlogMirrors.erase(it);
	}
	if (_netGuardianMirror && !aliveIds.count(_netGuardianUnitId))
	{
		if (_netGuardianMirror->getState() != State::DEAD)
			_netGuardianMirror->dead();
		_netGuardianMirror = nullptr;
		_netGuardianUnitId = -1;
	}
}

void GameLayer::JoyStickRelease()
{
	if (currentPlayer && currentPlayer->getState() == State::WALK)
	{
		currentPlayer->idle();
	}
	if (_networkBattle)
	{
		nsv2::network::InputCommand command;
		command.tick = _networkTick;
		command.action = nsv2::network::ActionType::Move;
		command.axisX = 0;
		command.axisY = 0;
		command.isDiscreteAction = true;
		nsv2::network::sharedLanSession().submitInput(command);
	}
}

void GameLayer::JoyStickUpdate(Vec2 direction)
{
	if (!ougisChar)
	{
		currentPlayer->walk(direction);
		if (_networkBattle && (_networkBattleTime - _lastNetworkJoystickSendTime >= 0.033f))
		{
			_lastNetworkJoystickSendTime = _networkBattleTime;
			nsv2::network::InputCommand command;
			command.tick = _networkTick;
			command.action = nsv2::network::ActionType::Move;
			command.axisX = static_cast<int16_t>(std::clamp(direction.x * 1000.0f, -1000.0f, 1000.0f));
			command.axisY = static_cast<int16_t>(std::clamp(direction.y * 1000.0f, -1000.0f, 1000.0f));
			command.isDiscreteAction = false;
			nsv2::network::sharedLanSession().submitInput(command);

			if (_netPresentation && currentPlayer)
			{
				_netPresentation->recordPredictedInput(command.sequence, command.tick, command.axisX, command.axisY, static_cast<float>(currentPlayer->getWalkSpeed()), 0.033f);
			}
		}
	}
}

void GameLayer::attackButtonClick(ABType type)
{
	if (_networkBattle)
	{
		nsv2::network::InputCommand command;
		command.tick = _networkTick;
		command.isDiscreteAction = true;
		if (type == NAttack)
		{
			_isAttackButtonRelease = false;
			command.action = nsv2::network::ActionType::NormalAttack;
		}
		else if (type == SKILL1)
			command.action = nsv2::network::ActionType::Skill1;
		else if (type == SKILL2)
			command.action = nsv2::network::ActionType::Skill2;
		else if (type == SKILL3)
			command.action = nsv2::network::ActionType::Skill3;
		else if (type == OUGIS1)
			command.action = nsv2::network::ActionType::Skill4;
		else if (type == OUGIS2)
			command.action = nsv2::network::ActionType::Skill5;
		else if (type == Item1)
			command.action = nsv2::network::ActionType::Item1;
		else
			return;

		nsv2::network::sharedLanSession().submitInput(command);

		// Local responsive trigger for client/host
		if (type == Item1)
			currentPlayer->setItem(type);
		else
			currentPlayer->attack(type);
		return;
	}
	if (type == NAttack)
	{
		_isAttackButtonRelease = false;
	}

	if (type == Item1)
	{
		currentPlayer->setItem(type);
	}
	else
	{
		currentPlayer->attack(type);
	}
}

void GameLayer::gearButtonClick(GearType type)
{
	currentPlayer->useGear(type);
}

void GameLayer::attackButtonRelease()
{
	_isAttackButtonRelease = true;
}

void GameLayer::onPause()
{
	if (_isPause)
		return;

	_isPause = true;
	RenderTexture *snapshoot = RenderTexture::create(winSize.width, winSize.height);
	Scene *f = Director::sharedDirector()->getRunningScene();
	Ref *pObject = f->getChildren()->objectAtIndex(0);
	BGLayer *bg = (BGLayer *)pObject;
	snapshoot->begin();
	bg->visit();

	visit();
	snapshoot->end();

	Scene *pscene = Scene::create();
	PauseLayer *layer = PauseLayer::create(snapshoot);
	pscene->addChild(layer);
	Director::sharedDirector()->pushScene(pscene);
}

void GameLayer::resumeFromPause()
{
	if (!_isPause)
		return;

	if (UserDefault::sharedUserDefault()->getBoolForKey("isBGM"))
	{
		SimpleAudioEngine::sharedEngine()->resumeBackgroundMusic();
	}
	if (UserDefault::sharedUserDefault()->getBoolForKey("isVoice"))
	{
		SimpleAudioEngine::sharedEngine()->resumeAllEffects();
	}

	Director::sharedDirector()->popScene();
	_isPause = false;
}

void GameLayer::onGear()
{
	if (!_enableGear)
		return;
	if (_isGear)
		return;
	_isGear = true;

	RenderTexture *snapshoot = RenderTexture::create(winSize.width, winSize.height);
	Scene *f = Director::sharedDirector()->getRunningScene();
	Ref *pObject = f->getChildren()->objectAtIndex(0);
	BGLayer *bg = (BGLayer *)pObject;
	snapshoot->begin();
	bg->visit();

	visit();
	snapshoot->end();

	Scene *pscene = Scene::create();
	GearLayer *layer = GearLayer::create(snapshoot);
	_gearLayer = layer;
	layer->updatePlayerGear();
	pscene->addChild(layer);
	Director::sharedDirector()->pushScene(pscene);
}

void GameLayer::onGameOver(bool isWin)
{
	if (_gameOverShown)
		return;
	_gameOverShown = true;
	removeKeyEventHandler();
	if (_networkBattle)
	{
		nsv2::network::sharedLanSession().stop();
		_networkBattle = false;
	}

	if (_isPause)
	{
		_isPause = false;
		Director::sharedDirector()->popScene();
	}
	if (_isGear)
	{
		_isGear = false;
		Director::sharedDirector()->popScene();
	}

	RenderTexture *snapshoot = RenderTexture::create(winSize.width, winSize.height);
	Scene *f = Director::sharedDirector()->getRunningScene();
	Ref *pObject = f->getChildren()->objectAtIndex(0);
	BGLayer *bg = (BGLayer *)pObject;
	snapshoot->begin();
	bg->visit();
	visit();
	snapshoot->end();

	getGameModeHandler()->Internal_GameOver();

	Scene *pscene = Scene::create();
	GameOver *layer = GameOver::create(snapshoot);
	layer->setWin(isWin);
	pscene->addChild(layer);
	Director::sharedDirector()->pushScene(pscene);
}

void GameLayer::onLeft()
{
	if (_networkBattle)
	{
		nsv2::network::sharedLanSession().stop();
		_networkBattle = false;
	}

	unscheduleUpdate();
	unscheduleAllSelectors();

	CCArray *childArray = getChildren();
	Ref *pObject;
	CCARRAY_FOREACH(childArray, pObject)
	{
		auto ac = (Node *)pObject;
		ac->unscheduleUpdate();
		ac->unscheduleAllSelectors();
	}

	LoadLayer::unloadAllCharsIMG(_CharacterArray);
	removeSprites(format("Unit/Tower/Tower{}.plist", mapId));

	if (_isHardCoreGame)
	{
		removeSprites(kGuardian_Han);
		removeSprites(kGuardian_Roshi);
		KTools::prepareFileOGG(GuardianEnum::Han, true);
		KTools::prepareFileOGG(GuardianEnum::Roshi, true);
	}

	KTools::prepareFileOGG("Effect", true);
	KTools::prepareFileOGG("Ougis", true);

	_CharacterArray.clear();
	_TowerArray.clear();
	_KonohaFlogArray.clear();
	_AkatsukiFlogArray.clear();

	removeSprites("UI.plist");
	removeSprites("Map.plist");

	SimpleAudioEngine::sharedEngine()->stopAllEffects();
	SimpleAudioEngine::sharedEngine()->stopBackgroundMusic(true);

	lua_call_func(UiFlowKeys::kOnGameOver);
}

void GameLayer::checkBackgroundMusic(float dt)
{
	if (UserDefault::sharedUserDefault()->getBoolForKey("isBGM"))
	{
		if (!SimpleAudioEngine::sharedEngine()->isBackgroundMusicPlaying())
		{
			if (!_isHardCoreGame)
			{
				SimpleAudioEngine::sharedEngine()->playBackgroundMusic(BATTLE_MUSIC);
			}
			else
			{
				int id = (mapId - 1) > 4 ? 4 : (mapId - 1);
				if (_playNum == 0)
				{
					SimpleAudioEngine::sharedEngine()->playBackgroundMusic(format("Audio/Music/Battle{}.ogg", 2 + id * 3).c_str(), false);
					_playNum++;
				}
				else if (_playNum == 1)
				{
					SimpleAudioEngine::sharedEngine()->playBackgroundMusic(format("Audio/Music/Battle{}.ogg", 3 + id * 3).c_str(), false);
					_playNum++;
				}
				else if (_playNum == 2)
				{
					SimpleAudioEngine::sharedEngine()->playBackgroundMusic(format("Audio/Music/Battle{}.ogg", 1 + id * 3).c_str(), false);
					_playNum = 0;
				}
			}
		}
	}
}

void GameLayer::setOugis(CharacterBase *sender)
{
	if (!_hudLayer->ougisLayer)
	{
		ougisChar = sender;

		CCArray *childArray = getChildren();
		Ref *pObject;
		CCARRAY_FOREACH(childArray, pObject)
		{
			auto object = (Node *)pObject;
			object->pauseSchedulerAndActions();
		}
		pauseSchedulerAndActions();

		updateViewPoint(0.01f);

		blend = LayerColor::create(ccc4(0, 0, 0, 200), winSize.width, winSize.height);
		blend->setPosition(Vec2(-getPositionX(), 0));
		addChild(blend, 1000);
		sender->setZOrder(2000);

		if (UserDefault::sharedUserDefault()->getBoolForKey("isVoice"))
		{
			SimpleAudioEngine::sharedEngine()->playEffect(format("Audio/Ougis/{}_ougis.ogg", ougisChar->getName()).c_str());
		}

		_hudLayer->setOugis(ougisChar->getName(), ougisChar->getGroup());
	}
}

void GameLayer::removeOugis()
{
	ougisChar->setZOrder(-ougisChar->getPositionY());
	CCArray *childArray = getChildren();
	Ref *pObject;
	CCARRAY_FOREACH(childArray, pObject)
	{
		auto object = (Node *)pObject;
		object->resumeSchedulerAndActions();
	}
	resumeSchedulerAndActions();

	blend->removeFromParent();
	ougisChar = nullptr;
}

#if (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
extern "C" void MacKeyboard_register();
extern "C" void MacKeyboard_unregister();
#endif

void GameLayer::setKeyEventHandler()
{
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
	Director::sharedDirector()->getOpenGLView()->setAccelerometerKeyHook((GLView::LPFN_ACCELEROMETER_KEYHOOK)(&GameLayer::LPFN_ACCELEROMETER_KEYHOOK));
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_LINUX)
	glfwSetKeyCallback(_window, keyEventHandle);
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
	MacKeyboard_register();
#endif
}

void GameLayer::removeKeyEventHandler()
{
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
	Director::sharedDirector()->getOpenGLView()->setAccelerometerKeyHook(nullptr);
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_LINUX)
	glfwSetKeyCallback(_window, nullptr);
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
	MacKeyboard_unregister();
#endif
}

int GameLayer::getMapCount()
{
	int index = 1;
	int mapCount = 0;
	auto fileUtils = FileUtils::sharedFileUtils();
	while (fileUtils->isFileExist(format("Maps/{}.tmx", index++).c_str()))
		mapCount++;
	CCLOG("===== Found %d maps =====", mapCount);
	return mapCount;
}

void GameLayer::invokeAllCallbacks()
{
	isHUDInitialized = true;
	if (callbackssList.size() > 0)
	{
		for (auto &callback : callbackssList)
			callback();
		callbackssList.clear();
	}
}

Vec2 GameLayer::getCustomSpawnPoint(HeroData &data)
{
	data.isInit = true;
	return data.group == Group::Konoha ? Vec2(432, 80) : Vec2(2608, 80);
}

void GameLayer::clearAllFlogsMainTarget(CharacterBase *target)
{
	UnitEx::clearMainTarget(target, _KonohaFlogArray);
	UnitEx::clearMainTarget(target, _AkatsukiFlogArray);
}

void GameLayer::clearAllUnitsMainTarget(CharacterBase *target)
{
	clearAllFlogsMainTarget(target);
	UnitEx::clearMainTarget(target, _AkatsukiFlogArray);
}

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32 || CC_TARGET_PLATFORM == CC_PLATFORM_LINUX)

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
#define isPressed(__KEY__) _isPressed(__KEY__)
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_LINUX)
#if __has_include("glfw3.h")
#include "glfw3.h"
#elif __has_include(<GLFW/glfw3.h>)
#include <GLFW/glfw3.h>
#elif __has_include("glfw3/include/mac/glfw3.h")
#include "glfw3/include/mac/glfw3.h"
#else
#error "GLFW header not found. Check include paths."
#endif
#define isPressed(__KEY__) glfwGetKey(_window, __KEY__)
#endif

/**
 * Use __W __S __D __A control when to force move
 */
#define MOVE(__W, __S, __D, __A, name, keyState)                                    \
	{                                                                               \
		if (keyState)                                                               \
			_gLayer->_lastPressedMovementKey = name;                                \
		else if (_gLayer->_lastPressedMovementKey == name)                          \
			_gLayer->_lastPressedMovementKey = -100;                                \
		int horizontal;                                                             \
		int vertical;                                                               \
		if (__W)                                                                    \
		{                                                                           \
			vertical = (isPressed(KEY_W) ? 1 : -1);                                 \
		}                                                                           \
		else if (__S)                                                               \
		{                                                                           \
			vertical = (isPressed(KEY_S) ? -1 : 1);                                 \
		}                                                                           \
		else                                                                        \
		{                                                                           \
			vertical = (isPressed(KEY_W) ? 1 : -1) + (isPressed(KEY_S) ? -1 : 1);   \
			vertical = abs(vertical) > 1 ? vertical / 2 : vertical;                 \
		}                                                                           \
		if (__D)                                                                    \
		{                                                                           \
			horizontal = (isPressed(KEY_D) ? 1 : -1);                               \
		}                                                                           \
		else if (__A)                                                               \
		{                                                                           \
			horizontal = (isPressed(KEY_A) ? -1 : 1);                               \
		}                                                                           \
		else                                                                        \
		{                                                                           \
			horizontal = (isPressed(KEY_D) ? 1 : -1) + (isPressed(KEY_A) ? -1 : 1); \
			horizontal = abs(horizontal) > 1 ? horizontal / 2 : horizontal;         \
		}                                                                           \
		if (horizontal != 0 || vertical != 0)                                       \
		{                                                                           \
			if (!_gLayer->ougisChar)                                                \
				_gLayer->currentPlayer->walk(Vec2(horizontal, vertical));           \
		}                                                                           \
		else if (_gLayer->currentPlayer->getState() == State::WALK)                 \
		{                                                                           \
			_gLayer->_lastPressedMovementKey = -100;                                \
			_gLayer->currentPlayer->idle();                                         \
		}                                                                           \
		break;                                                                      \
	}

#define ON_GEAR_BY(__ID__, __KEY_STATE__)                                     \
	if (_gLayer->_isGear && __KEY_STATE__)                                    \
	{                                                                         \
		auto &gearBtns = _gLayer->_gearLayer->_screwLayer->getGearBtnArray(); \
		if (gearBtns.size() >= __ID__ - 1)                                    \
		{                                                                     \
			auto gear_btn = gearBtns.at(__ID__ - 1);                          \
			if (gear_btn)                                                     \
				gear_btn->click();                                            \
		}                                                                     \
	}                                                                         \
	break;

bool GameLayer::checkHasAnyMovement()
{
	if (_gLayer)
	{
		if (_gLayer->_lastPressedMovementKey != -100)
		{
			keyEventHandle(_window, _gLayer->_lastPressedMovementKey, 0, 1, 0);
			return true;
		}
	}
	return false;
}

/** NOTE: Impl key listener */
void GameLayer::keyEventHandle(GLFWwindow *window, int key, int scancode, int keyState, int mods)
{
	// NOTE: only attack button can hold
	//  Other keys is only click
	if (keyState == 2 && key != KEY_J)
		return;
	switch (key)
	{
	case KEY_W:
		// case KEY_UP:
		MOVE(keyState, 0, 0, 0, KEY_W, keyState);
	case KEY_S:
		// case KEY_DOWN:
		MOVE(0, keyState, 0, 0, KEY_S, keyState);
	case KEY_A:
		// case KEY_LEFT:
		MOVE(0, 0, keyState, 0, KEY_A, keyState);
	case KEY_D:
		// case KEY_RIGHT:
		MOVE(0, 0, 0, keyState, KEY_D, keyState);
	case KEY_J:
		if (keyState)
			_gLayer->_hudLayer->nAttackButton->click();
		else
			_gLayer->_isAttackButtonRelease = true;
		break;
	case KEY_L: // Ramen button
		if (keyState)
			_gLayer->_hudLayer->item1Button->click();
		break;
	case KEY_H: // Ougis 2 buttons
		if (keyState)
			_gLayer->_hudLayer->skill5Button->click();
		break;
	case KEY_K: // Ougis 1 buttons
		if (keyState)
			_gLayer->_hudLayer->skill4Button->click();
		break;
	case KEY_U: // skill 1
		if (keyState)
			_gLayer->_hudLayer->skill1Button->click();
		break;
	case KEY_I: // skill 2
		if (keyState)
			_gLayer->_hudLayer->skill2Button->click();
		break;
	case KEY_O: // skill 3
		if (keyState)
			_gLayer->_hudLayer->skill3Button->click();
		break;
		// Gear buttons
	case KEY_1:
	case KEY_KP_1:
		ON_GEAR_BY(1, keyState);
	case KEY_2:
	case KEY_KP_2:
		ON_GEAR_BY(2, keyState);
	case KEY_3:
	case KEY_KP_3:
		ON_GEAR_BY(3, keyState);
	case KEY_4:
	case KEY_KP_4:
		ON_GEAR_BY(4, keyState);
	case KEY_5:
	case KEY_KP_5:
		ON_GEAR_BY(5, keyState);
	case KEY_6:
	case KEY_KP_6:
		ON_GEAR_BY(6, keyState);
	case KEY_7:
	case KEY_KP_7:
		ON_GEAR_BY(7, keyState);
	case KEY_8:
	case KEY_KP_8:
		ON_GEAR_BY(8, keyState);
	case KEY_9:
	case KEY_KP_9:
		ON_GEAR_BY(9, keyState);
	case KEY_0:
	case KEY_KP_0:
		break;
	/* Item buttons */
	// Item 1 & Purchase
	case KEY_B:
		if (keyState)
		{
			if (_gLayer->_isGear)
				_gLayer->_gearLayer->confirmPurchase();
			else
				_gLayer->_hudLayer->getItem3Button()->click();
		}
		break;
	// Item 2
	case KEY_N:
		if (keyState)
			_gLayer->_hudLayer->getItem4Button()->click();
		break;
	// Item 3
	case KEY_M:
		if (keyState)
			_gLayer->_hudLayer->getItem2Button()->click();
		break;
	case KEY_ESCAPE:
	case KEY_ENTER:
		if (keyState && _gLayer->_isStarted == true)
		{
			if (_gLayer->_isPause)
			{
				_gLayer->resumeFromPause();
			}
			else if (_gLayer->_isGear)
			{
				Director::sharedDirector()->popScene();
				_gLayer->_isGear = false;
			}
			else
			{
				_gLayer->onPause(); // enter pause menu
			}
		}
		break;
	case KEY_SPACE:
		if (_gLayer->_enableGear && _gLayer->_isStarted && keyState && !_gLayer->_isPause)
		{
			if (_gLayer->_isGear)
			{
				Director::sharedDirector()->popScene();
				_gLayer->_isGear = false;
			}
			else
			{
				_gLayer->onGear(); // enter gear shop
			}
		}
		break;
	case KEY_F11:
		// if (keyState)
		// {
		// 	// SET_FULL_SCREEN_MODE(_window, _isFullScreen);
		// 	GLFWmonitor *monitor = glfwGetPrimaryMonitor();
		// 	if (nullptr == monitor)
		// 	{
		// 		return;
		// 	}
		// 	const GLFWvidmode *videoMode = glfwGetVideoMode(monitor);
		// 	int width, height;
		// 	glfwGetWindowSize(_window, &width, &height);
		// 	if (_isFullScreen)
		// 	{
		// 		glfwSetWindowMonitor(_window, nullptr, videoMode->width / 2, videoMode->height / 2, WIDTH, HEIGHT, videoMode->refreshRate);
		// 		glfwSetWindowPos(_window,
		// 						 (videoMode->width - WIDTH) / 2,
		// 						 (videoMode->height - HEIGHT) * 0.35f);
		// 	}
		// 	else
		// 	{
		// 		glfwSetWindowMonitor(_window, nullptr, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate);
		// 		Director::sharedDirector()->getOpenGLView()->updateFrameSize(videoMode->width,videoMode->height);
		// 	}
		// 	_isFullScreen = !_isFullScreen;
		// }
		break;
	}
}

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)

void GameLayer::LPFN_ACCELEROMETER_KEYHOOK(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_SYSKEYDOWN:
	case WM_KEYDOWN:
		keyEventHandle(nullptr, wParam, 0, 1, 0);
		break;
	case WM_SYSKEYUP:
	case WM_KEYUP:
		keyEventHandle(nullptr, wParam, 0, 0, 0);
		break;
	}
}

#endif

#endif

#if (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)

static bool s_macKeyState[256] = {};

#define isPressed(__KEY__) ((__KEY__) < 256 && s_macKeyState[__KEY__])

#define MOVE_MAC(__W, __S, __D, __A, name, keyState)                                    \
	{                                                                               \
		if (keyState)                                                               \
			_gLayer->_lastPressedMovementKey = name;                                \
		else if (_gLayer->_lastPressedMovementKey == name)                          \
			_gLayer->_lastPressedMovementKey = -100;                                \
		int horizontal;                                                             \
		int vertical;                                                               \
		if (__W)                                                                    \
		{                                                                           \
			vertical = (isPressed(KEY_W) ? 1 : -1);                                \
		}                                                                           \
		else if (__S)                                                               \
		{                                                                           \
			vertical = (isPressed(KEY_S) ? -1 : 1);                                \
		}                                                                           \
		else                                                                        \
		{                                                                           \
			vertical = (isPressed(KEY_W) ? 1 : -1) + (isPressed(KEY_S) ? -1 : 1);  \
			vertical = abs(vertical) > 1 ? vertical / 2 : vertical;                \
		}                                                                           \
		if (__D)                                                                    \
		{                                                                           \
			horizontal = (isPressed(KEY_D) ? 1 : -1);                              \
		}                                                                           \
		else if (__A)                                                               \
		{                                                                           \
			horizontal = (isPressed(KEY_A) ? -1 : 1);                              \
		}                                                                           \
		else                                                                        \
		{                                                                           \
			horizontal = (isPressed(KEY_D) ? 1 : -1) + (isPressed(KEY_A) ? -1 : 1); \
			horizontal = abs(horizontal) > 1 ? horizontal / 2 : horizontal;        \
		}                                                                           \
		if (horizontal != 0 || vertical != 0)                                       \
		{                                                                           \
			if (!_gLayer->ougisChar)                                                \
				_gLayer->currentPlayer->walk(Vec2(horizontal, vertical));           \
		}                                                                           \
		else if (_gLayer->currentPlayer->getState() == State::WALK)                 \
		{                                                                           \
			_gLayer->_lastPressedMovementKey = -100;                                \
			_gLayer->currentPlayer->idle();                                         \
		}                                                                           \
		break;                                                                      \
	}

void GameLayer::keyEventHandle(int key, int keyState)
{
	if (!_gLayer || !_gLayer->currentPlayer)
		return;

	if (key >= 0 && key < 256)
		s_macKeyState[key] = (keyState != 0);

	switch (key)
	{
	case KEY_W:
		MOVE_MAC(keyState, 0, 0, 0, KEY_W, keyState);
	case KEY_S:
		MOVE_MAC(0, keyState, 0, 0, KEY_S, keyState);
	case KEY_A:
		MOVE_MAC(0, 0, keyState, 0, KEY_A, keyState);
	case KEY_D:
		MOVE_MAC(0, 0, 0, keyState, KEY_D, keyState);
	case KEY_J:
		if (keyState)
			_gLayer->_hudLayer->nAttackButton->click();
		else
			_gLayer->_isAttackButtonRelease = true;
		break;
	case KEY_L:
		if (keyState)
			_gLayer->_hudLayer->item1Button->click();
		break;
	case KEY_H:
		if (keyState)
			_gLayer->_hudLayer->skill5Button->click();
		break;
	case KEY_K:
		if (keyState)
			_gLayer->_hudLayer->skill4Button->click();
		break;
	case KEY_U:
		if (keyState)
			_gLayer->_hudLayer->skill1Button->click();
		break;
	case KEY_I:
		if (keyState)
			_gLayer->_hudLayer->skill2Button->click();
		break;
	case KEY_O:
		if (keyState)
			_gLayer->_hudLayer->skill3Button->click();
		break;
	case KEY_1: case KEY_KP_1:
		if (_gLayer->_isGear && keyState) { auto &gb = _gLayer->_gearLayer->_screwLayer->getGearBtnArray(); if (gb.size()>=1 && gb.at(0)) gb.at(0)->click(); } break;
	case KEY_2: case KEY_KP_2:
		if (_gLayer->_isGear && keyState) { auto &gb = _gLayer->_gearLayer->_screwLayer->getGearBtnArray(); if (gb.size()>=2 && gb.at(1)) gb.at(1)->click(); } break;
	case KEY_3: case KEY_KP_3:
		if (_gLayer->_isGear && keyState) { auto &gb = _gLayer->_gearLayer->_screwLayer->getGearBtnArray(); if (gb.size()>=3 && gb.at(2)) gb.at(2)->click(); } break;
	case KEY_B:
		if (keyState) { if (_gLayer->_isGear) _gLayer->_gearLayer->confirmPurchase(); else _gLayer->_hudLayer->getItem3Button()->click(); } break;
	case KEY_N:
		if (keyState) _gLayer->_hudLayer->getItem4Button()->click(); break;
	case KEY_M:
		if (keyState) _gLayer->_hudLayer->getItem2Button()->click(); break;
	case KEY_SPACE:
		if (_gLayer->_enableGear && _gLayer->_isStarted && keyState && !_gLayer->_isPause)
		{
			if (_gLayer->_isGear)
			{
				Director::sharedDirector()->popScene();
				_gLayer->_isGear = false;
			}
			else
			{
				_gLayer->onGear();
			}
		}
		break;
	case KEY_ESCAPE: case KEY_ENTER:
		if (keyState && _gLayer->_isStarted)
		{
			if (_gLayer->_isPause) { _gLayer->resumeFromPause(); }
			else if (_gLayer->_isGear) { Director::sharedDirector()->popScene(); _gLayer->_isGear = false; }
			else { _gLayer->onPause(); }
		}
		break;
	}
}

bool GameLayer::checkHasAnyMovement()
{
	if (_gLayer && _gLayer->_lastPressedMovementKey != -100)
	{
		keyEventHandle(_gLayer->_lastPressedMovementKey, 1);
		return true;
	}
	return false;
}

#elif !(CC_TARGET_PLATFORM == CC_PLATFORM_WIN32 || CC_TARGET_PLATFORM == CC_PLATFORM_LINUX)
bool GameLayer::checkHasAnyMovement()
{
	return false;
}
#endif
