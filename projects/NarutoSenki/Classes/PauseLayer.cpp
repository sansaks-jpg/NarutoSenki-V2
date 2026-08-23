#include "PauseLayer.h"
#include "GameLayer.h"
#include "Data/Fonts.h"

bool PauseLayer::init(RenderTexture *snapshoot)
{
	if (!Layer::init())
		return false;

	SimpleAudioEngine::sharedEngine()->stopAllEffects();
	SimpleAudioEngine::sharedEngine()->pauseAllEffects();
	SimpleAudioEngine::sharedEngine()->pauseBackgroundMusic();

	Texture2D *bgTexture = snapshoot->getSprite()->getTexture();
	Sprite *bg = Sprite::createWithTexture(bgTexture);
	bg->setAnchorPoint(Vec2(0, 0));
	bg->setFlipY(true);
	addChild(bg, 0);

	Layer *blend = LayerColor::create(ccc4(0, 0, 0, 150), winSize.width, winSize.height);
	addChild(blend, 1);

	//produce the menu_bar
	Sprite *menu_bar_b = Sprite::create("menu_bar2.png");
	menu_bar_b->setAnchorPoint(Vec2(0, 0));
	FULL_SCREEN_SPRITE(menu_bar_b);
	addChild(menu_bar_b, 2);

	Sprite *menu_bar_t = Sprite::create("menu_bar3.png");
	menu_bar_t->setAnchorPoint(Vec2(0, 0));
	menu_bar_t->setPosition(Vec2(0, winSize.height - menu_bar_t->getContentSize().height));
	FULL_SCREEN_SPRITE(menu_bar_t);
	addChild(menu_bar_t, 2);

	CCLabelBMFont *pause_title = CCLabelBMFont::create("PAUSE", Fonts::Default);
	pause_title->setAnchorPoint(Vec2(0, 0));
	pause_title->setScale(0.45f);
	pause_title->setPosition(Vec2(2, winSize.height - 24));
	addChild(pause_title, 3);

	CCLabelBMFont *resume_label = CCLabelBMFont::create("RESUME", Fonts::Default);
	resume_label->setScale(0.38f);
	CCLabelBMFont *back_label = CCLabelBMFont::create("BACK TO MENU", Fonts::Default);
	back_label->setScale(0.30f);
	MenuItem *resume_btn = CCMenuItemLabel::create(resume_label, this, menu_selector(PauseLayer::onResume));
	MenuItem *btm_btn = CCMenuItemLabel::create(back_label, this, menu_selector(PauseLayer::onBackToMenu));

	pauseMenu = Menu::create(resume_btn, btm_btn, nullptr);
	pauseMenu->alignItemsVerticallyWithPadding(26);
	pauseMenu->setPosition(Vec2(winSize.width / 2, winSize.height / 2 + 30));
	addChild(pauseMenu, 3);

	CCLabelBMFont *surrender_text = CCLabelBMFont::create("SURRENDER?", Fonts::Default);
	surrender_text->setScale(0.30f);
	surrender_text->setPosition(Vec2(winSize.width / 2, winSize.height / 2 - 23));
	addChild(surrender_text, 4);

	bgm_label = CCLabelBMFont::create("", Fonts::Default);
	bgm_label->setScale(0.28f);
	voice_label = CCLabelBMFont::create("", Fonts::Default);
	voice_label->setScale(0.28f);
	bgm_btn = CCMenuItemLabel::create(bgm_label, this, menu_selector(PauseLayer::onBGM));
	voice_btn = CCMenuItemLabel::create(voice_label, this, menu_selector(PauseLayer::onVoice));
	soundMenu = Menu::create(bgm_btn, voice_btn, nullptr);
	soundMenu->alignItemsHorizontallyWithPadding(25);
	soundMenu->setPosition(Vec2(pauseMenu->getPositionX(), pauseMenu->getPositionY() - 80));
	addChild(soundMenu, 4);

	pre_label = CCLabelBMFont::create("", Fonts::Default);
	pre_label->setScale(0.28f);
	pre_btn = CCMenuItemLabel::create(pre_label, this, menu_selector(PauseLayer::onPreload));
	preMenu = Menu::create(pre_btn, nullptr);
	preMenu->alignItemsHorizontallyWithPadding(25);
	preMenu->setPosition(Vec2(pauseMenu->getPositionX(), preMenu->getPositionY() - 84));
	addChild(preMenu, 4);

	if (UserDefault::sharedUserDefault()->getBoolForKey("isBGM") == false)
	{
		bgm_btn->selected();
	}
	if (UserDefault::sharedUserDefault()->getBoolForKey("isVoice") == false)
	{
		voice_btn->selected();
	}

	if (UserDefault::sharedUserDefault()->getBoolForKey("isPreload") == false)
	{
		pre_btn->selected();
	}
	updateOptionLabels();

	return true;
}

void PauseLayer::updateOptionLabels()
{
	if (bgm_label)
		bgm_label->setString(UserDefault::sharedUserDefault()->getBoolForKey("isBGM") ? "BGM: ON" : "BGM: OFF");
	if (voice_label)
		voice_label->setString(UserDefault::sharedUserDefault()->getBoolForKey("isVoice") ? "VOICE: ON" : "VOICE: OFF");
	if (pre_label)
		pre_label->setString(UserDefault::sharedUserDefault()->getBoolForKey("isPreload") ? "PRELOAD: ON" : "PRELOAD: OFF");
}

void PauseLayer::onBGM(Ref *sender)
{
	(void)sender;
	if (UserDefault::sharedUserDefault()->getBoolForKey("isBGM") == true)
	{
		UserDefault::sharedUserDefault()->setBoolForKey("isBGM", false);
		bgm_btn->selected();
	}
	else
	{
		UserDefault::sharedUserDefault()->setBoolForKey("isBGM", true);
		bgm_btn->unselected();
	}
	updateOptionLabels();
}

void PauseLayer::onVoice(Ref *sender)
{
	(void)sender;
	if (UserDefault::sharedUserDefault()->getBoolForKey("isVoice") == true)
	{
		UserDefault::sharedUserDefault()->setBoolForKey("isVoice", false);
		voice_btn->selected();
	}
	else
	{
		UserDefault::sharedUserDefault()->setBoolForKey("isVoice", true);
		voice_btn->unselected();
	}
	updateOptionLabels();
}

void PauseLayer::onPreload(Ref *sender)
{
	(void)sender;
	if (UserDefault::sharedUserDefault()->getBoolForKey("isPreload") == true)
	{
		UserDefault::sharedUserDefault()->setBoolForKey("isPreload", false);
		pre_btn->selected();
	}
	else
	{
		UserDefault::sharedUserDefault()->setBoolForKey("isPreload", true);
		pre_btn->unselected();
	}
	updateOptionLabels();
}

void PauseLayer::onResume(Ref *sender)
{
	if (UserDefault::sharedUserDefault()->getBoolForKey("isBGM"))
	{
		SimpleAudioEngine::sharedEngine()->resumeBackgroundMusic();
	}
	if (UserDefault::sharedUserDefault()->getBoolForKey("isVoice"))
	{
		SimpleAudioEngine::sharedEngine()->resumeAllEffects();
	}

	Director::sharedDirector()->popScene();
	getGameLayer()->_isPause = false;
}

void PauseLayer::onBackToMenu(Ref *sender)
{
	pauseMenu->setVisible(false);
	soundMenu->setVisible(false);
	SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/select.ogg");
	exitLayer = Layer::create();

	Sprite *exit_bg = Sprite::createWithSpriteFrameName("confirm_bg.png");
	exit_bg->setPosition(Vec2(winSize.width / 2, winSize.height / 2));

	CCLabelBMFont *comfirm_title = CCLabelBMFont::create("EXIT GAME?", Fonts::Default);
	comfirm_title->setScale(0.35f);
	comfirm_title->setPosition(Vec2(winSize.width / 2, winSize.height / 2 + 38));

	CCLabelBMFont *surrender_text = CCLabelBMFont::create("RETURN TO MAIN MENU?", Fonts::Default);
	surrender_text->setScale(0.25f);
	surrender_text->setPosition(Vec2(winSize.width / 2, winSize.height / 2 + 8));

	MenuItem *yes_btn = CCMenuItemLabel::create(CCLabelBMFont::create("YES", Fonts::Default), this, menu_selector(PauseLayer::onLeft));
	MenuItem *no_btn = CCMenuItemLabel::create(CCLabelBMFont::create("NO", Fonts::Default), this, menu_selector(PauseLayer::onCancel));

	Menu *confirm_menu = Menu::create(yes_btn, no_btn, nullptr);
	confirm_menu->alignItemsHorizontallyWithPadding(24);
	confirm_menu->setPosition(Vec2(winSize.width / 2, winSize.height / 2 - 30));

	exitLayer->addChild(exit_bg, 1);
	exitLayer->addChild(confirm_menu, 2);
	exitLayer->addChild(comfirm_title, 2);
	exitLayer->addChild(surrender_text, 2);
	addChild(exitLayer, 500);
}

void PauseLayer::onLeft(Ref *sender)
{
	SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/confirm.ogg");
	getGameLayer()->_isSurrender = true;
	Director::sharedDirector()->popScene();

	getGameLayer()->_isPause = false;
}

void PauseLayer::onCancel(Ref *sender)
{
	pauseMenu->setVisible(true);
	soundMenu->setVisible(true);
	SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/cancel.ogg");
	exitLayer->removeFromParent();
}

PauseLayer *PauseLayer::create(RenderTexture *snapshoot)
{
	PauseLayer *pl = new PauseLayer();
	if (pl && pl->init(snapshoot))
	{
		pl->autorelease();
		return pl;
	}
	else
	{
		delete pl;
		return nullptr;
	}
}
