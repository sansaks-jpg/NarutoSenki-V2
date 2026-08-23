#pragma once
#include "Hero.hpp"

class Ino : public Hero
{
	void dead() override
	{
		CharacterBase::dead();

		// TODO: Support Ino controlled by the player can control other characters
		// if (isPlayer())
		// {
		// 	// Has controlled other hero
		// 	if (getGameLayer()->currentPlayer != this)
		// 	{
		// 		auto other = getGameLayer()->currentPlayer;
		// 		other->changeGroup();
		// 		other->doAI();

		// 		getGameLayer()->currentPlayer = this;
		// 		getGameLayer()->getHudLayer()->updateSkillButtons();
		// 	}
		// }
	}

	void perform() override
	{
		_mainTarget = nullptr;
		findHeroHalf();

		if (_skillChangeBuffValue)
		{
			return;
		}

		tryUseGear6();
		tryBuyGear(GearType::Gear06, GearType::Gear05, GearType::Gear01);

		if (needBackToTowerToRestoreHP() ||
			needBackToDefendTower())
			return;

		if (getMaxHP() - getHP() >= 3000 &&
			getCoin() >= 50 && !_isHealing && _isCanItem1 && _isArmored)
		{
			setItem(Item1);
		}

		if (_mainTarget && _mainTarget->isNotFlog())
		{
			Vec2 moveDirection;
			Vec2 sp = getDistanceToTarget();

			if (isFreeState())
			{
				if (_isCanOugis2 && !_isControlled && getGameLayer()->_isOugis2Game)
				{
					if (abs(sp.x) > 96 || abs(sp.y) > 16)
					{
						moveDirection = sp.getNormalized();
						walk(moveDirection);
						return;
					}
					else
					{
						changeSide(sp);
						attack(OUGIS2);
					}

					return;
				}
				else if (_mainTarget->getDEF() < 5000 && (_isCanSkill3 || _isCanSkill2))
				{
					if (abs(sp.x) > 96 || abs(sp.y) > 16)
					{
						moveDirection = sp.getNormalized();
						walk(moveDirection);
						return;
					}

					if (_isCanSkill2)
					{
						changeSide(sp);
						attack(SKILL2);
					}
					else if (_isCanSkill3)
					{
						changeSide(sp);
						attack(SKILL3);
					}

					return;
				}
				else if (enemyCombatPoint > friendCombatPoint && abs(enemyCombatPoint - friendCombatPoint) > 3000 && !_isHealing && !_isControlled)
				{
					if (abs(sp.x) < 160)
						stepBack2();
					else
						idle();
					return;
				}
				else if (abs(sp.x) < 128)
				{
					if (abs(sp.x) > 32 || abs(sp.y) > 32)
					{
						moveDirection = sp.getNormalized();
						walk(moveDirection);
						return;
					}

					if (_isCanOugis1 && !_isControlled && _mainTarget->getDEF() < 5000)
					{
						changeSide(sp);
						attack(OUGIS1);
					}
					else if (_isCanSkill1)
					{
						changeSide(sp);
						attack(SKILL1);
					}
					else
					{
						changeSide(sp);
						attack(NAttack);
					}

					return;
				}
			}
		}
		_mainTarget = nullptr;
		if (notFindFlogHalf())
			findTowerHalf();

		if (_mainTarget)
		{
			Vec2 moveDirection;
			Vec2 sp = getDistanceToTarget();

			if (abs(sp.x) > 32 || abs(sp.y) > 32)
			{
				moveDirection = sp.getNormalized();
				walk(moveDirection);
				return;
			}

			if (isFreeState())
			{
				if (_mainTarget->isFlog() && _isCanSkill1)
				{
					changeSide(sp);
					attack(SKILL1);
				}
				else
				{
					changeSide(sp);
					attack(NAttack);
				}
			}

			return;
		}

		checkHealingState();
	}

	void resumeAction(float dt) override
	{
		if (!_isArmored)
			return;

		for (auto hero : getGameLayer()->_CharacterArray)
		{
			if (hero->_isControlled)
			{
				hero->_isControlled = false;
				if (hero->isPlayer())
				{
					hero->_isAI = false;
					hero->unschedule(schedule_selector(Ino::setAI));
					getGameLayer()->getHudLayer()->_isAllButtonLocked = false;
				}
				if (isPlayer())
				{
					// auto controlledHero = getGameLayer()->currentPlayer;
					// controlledHero->_isAI = true;
					// controlledHero->doAI();
					// _isControlled = false;

					// getGameLayer()->currentPlayer = this;
					getGameLayer()->controlChar = nullptr;
					// getGameLayer()->getHudLayer()->updateSkillButtons();
				}
				if (_state != State::DEAD)
				{
					idle();
				}
				hero->changeGroup();
				hero->setController(nullptr);
			}
		}

		_isArmored = false;
		CharacterBase::resumeAction(dt);
	}

	void setActionResume() override
	{
		unschedule(schedule_selector(Ino::resumeAction));

		for (auto hero : getGameLayer()->_CharacterArray)
		{
			if (hero->_isControlled)
			{
				hero->_isControlled = false;
				hero->changeGroup();
				if (hero->isPlayer())
				{
					hero->unschedule(schedule_selector(Ino::setAI));
					hero->_isAI = false;
					getGameLayer()->getHudLayer()->_isAllButtonLocked = false;
				}
				if (isPlayer())
				{
					getGameLayer()->controlChar = nullptr;
				}
				hero->setController(nullptr);
			}
		}

		_isArmored = false;
	}
};
