#pragma once
#include "BGLayer.h"
#include "GameLayer.h"
#include "HudLayer.h"

class CharacterBase;
class Hero;

class LoadLayer : public Layer
{
public:
	LoadLayer();

	bool init();

	int loadNum;

	PROP_PTR(GameLayer, _gameLayer, GameLayer);
	PROP_PTR(BGLayer, _bgLayer, BGLayer);
	PROP_PTR(HudLayer, _hudLayer, HudLayer);

	bool _isHardCoreMode;
	bool _enableGear;
	bool _networkBattle = false;
	uint8_t _networkLocalSlot = 0;

	void configureNetworkBattle(uint8_t localSlot)
	{
		_networkBattle = true;
		_networkLocalSlot = localSlot;
		_networkLocalLoadComplete = false;
		_networkSceneEntered = false;
	}

	void preloadAudio();
	void preloadIMG();
	void playBGM(float dt);
	void onLoadFinish(float dt);
	void update(float dt) override;

	CREATE_FUNC(LoadLayer);
	static void perloadCharIMG(const string &name);
	static void unloadCharIMG(CharacterBase *player);
	static void unloadAllCharsIMG(const vector<Hero *> &players);

private:
	void setLoadingAnimation(const char *player, int index);
	void enterGameScene();
	void abortNetworkLoading();

	bool _networkLocalLoadComplete = false;
	bool _networkSceneEntered = false;
	vector<string> loadVector;
};