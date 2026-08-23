-- Runtime UI localization. Text is rendered with BMFont instead of text-bearing PNG sprites.
ns.text = {}

local text = ns.text

text.values = {
    select_character = 'SELECT CHARACTER',
    credits = 'CREDITS',
    record = 'RECORD',
    return_to_menu = 'RETURN',
    ranking = 'RANKING',
    start = 'START',
    skills = 'SKILLS',
    player_1 = 'PLAYER 1',
    player_2 = 'PLAYER 2',
    player_3 = 'PLAYER 3',
    coming_soon = 'COMING SOON',
    skill = 'SKILL',
    push_start = 'PUSH START',
}

function text.get(key)
    return text.values[key] or key or ''
end

function text.label(value, scale)
    local label = CCLabelBMFont:create(value or '', 'Fonts/1.fnt')
    if label then
        label:setScale(scale or 0.3)
    end
    return label
end

function text.keyLabel(key, scale)
    return text.label(text.get(key), scale)
end

function text.menuItem(value, listener, scale, sound)
    local item = CCMenuItemLabel:create(text.label(value, scale))
    if item and type(listener) == 'function' then
        item:registerScriptHandler(function(tag)
            if sound then audio.playSound(sound) end
            listener(tag)
        end)
    end
    return item
end

function text.keyMenuItem(key, listener, scale, sound)
    return text.menuItem(text.get(key), listener, scale, sound)
end

function text.characterName(name)
    return string.upper(name or '')
end

-- Character-specific descriptions can be added here after the Record2 atlas is
-- transcribed and reviewed. The fallback keeps the UI dynamic without inventing
-- a translation for a character whose source text has not been verified.
text.skillDescriptions = {}

function text.skillDescription(hero, skillIndex)
    local heroKey = hero or 'HERO'
    local heroDescriptions = text.skillDescriptions[heroKey]
    if heroDescriptions and heroDescriptions[skillIndex] then
        return heroDescriptions[skillIndex]
    end
    return string.upper(heroKey) .. ' - ' .. text.get('skill') .. ' ' .. tostring(skillIndex)
end
