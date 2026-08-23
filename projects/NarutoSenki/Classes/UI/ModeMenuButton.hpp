#pragma once
#include "UI/GameModeLayer.h"
#include "Data/Fonts.h"

class ModeMenuButton : public Sprite, public CCTouchDelegate
{
private:
	Sprite *lockMask = nullptr;
	CCLabelBMFont *titleLabel = nullptr;

public:
	PROP(GameModeLayer *, _gameModeLayer, Delegate);

	GameMode mode;
	bool useMask2;

	bool init(const string &szImage)
	{
		RETURN_FALSE_IF(!Sprite::init());
		float width = 100.0f;
		float height = 55.0f;
		ccColor4B color = ccc4(218, 163, 120, 255);
		if (szImage.find("4.png") != string::npos || szImage.find("5.png") != string::npos || szImage.find("6.png") != string::npos)
		{
			width = 80.0f;
			height = 180.0f;
			color = ccc4(35, 165, 220, 255);
		}
		else if (szImage.find("7.png") != string::npos || szImage.find("8.png") != string::npos)
		{
			width = 100.0f;
			height = 86.0f;
		}
		setContentSize(CCSize(width, height));
		setAnchorPoint(Vec2(0.5, 0.5));
		auto background = LayerColor::create(color, width, height);
		background->setAnchorPoint(Vec2(0, 0));
		background->setPosition(Vec2(-width / 2, -height / 2));
		addChild(background, -1);
		titleLabel = CCLabelBMFont::create("", Fonts::Default);
		titleLabel->setAnchorPoint(Vec2(0.5f, 0.5f));
		titleLabel->setScale(0.34f);
		titleLabel->setWidth(width / 0.34f - 12.0f);
		titleLabel->setLineBreakWithoutSpace(true);
		addChild(titleLabel, 1);
		return true;
	}

	void setTitle(const char *title)
	{
		if (titleLabel)
			titleLabel->setString(title ? title : "");
	}

	void onEnter()
	{
		Sprite::onEnter();
		Director::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, 10, true);
	}

	void onExit()
	{
		Sprite::onExit();
		Director::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
	}

	CCRect getRect()
	{
		CCSize size = getContentSize();
		return CCRect(0, 0, size.width, size.height);
	}

	bool ccTouchBegan(Touch *touch, Event *event)
	{
		return getRect().containsPoint(convertTouchToNodeSpace(touch));
	}

	void ccTouchEnded(Touch *touch, Event *event)
	{
		SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/confirm.ogg");
		_gameModeLayer->selectMode(mode);
	}

	bool isLocked()
	{
		return lockMask != nullptr;
	}

	void lock()
	{
		if (lockMask == nullptr)
		{
			auto maskPath = useMask2 ? "GameMode/chain_mask2.png" : "GameMode/chain_mask.png";
			lockMask = Sprite::create(maskPath);
			lockMask->setPosition(getPosition());
			_gameModeLayer->addChild(lockMask, 1000);
		}
	}

	void unlock()
	{
		if (lockMask)
		{
			_gameModeLayer->removeChild(lockMask, true);
			lockMask = nullptr;
		}
	}

	static ModeMenuButton *create(const string &szImage)
	{
		ModeMenuButton *mb = new ModeMenuButton();
		if (mb && mb->init(szImage))
		{
			mb->autorelease();
			return mb;
		}
		else
		{
			delete mb;
			return nullptr;
		}
	}
};
