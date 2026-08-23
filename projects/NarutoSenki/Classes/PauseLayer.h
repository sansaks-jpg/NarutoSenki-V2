#pragma once
#include "Defines.h"

class PauseLayer : public Layer
{
public:
	bool init(RenderTexture *snapshoot);

	Menu *pauseMenu = nullptr;
	Menu *soundMenu = nullptr;
	Menu *preMenu = nullptr;
	Layer *exitLayer = nullptr;
	CCMenuItemLabel *bgm_btn = nullptr;
	CCMenuItemLabel *voice_btn = nullptr;
	CCMenuItemLabel *pre_btn = nullptr;
	CCLabelBMFont *bgm_label = nullptr;
	CCLabelBMFont *voice_label = nullptr;
	CCLabelBMFont *pre_label = nullptr;

	static PauseLayer *create(RenderTexture *snapshoot);

private:
	void onResume(Ref *sender);
	void onBackToMenu(Ref *sender);
	void onLeft(Ref *sender);
	void onCancel(Ref *sender);
	void onBGM(Ref *sender);
	void onPreload(Ref *sender);
	void onVoice(Ref *sender);
	void updateOptionLabels();
};
