#include "AppDelegate.h"
#include "CCLuaEngine.h"
#include "script_support/CCScriptSupport.h"

#include "Network/LanNetworkRuntime.hpp"
#include "Systems/Initializer.hpp"

#if (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
#include <CoreFoundation/CoreFoundation.h>
#include <limits.h>
#endif

AppDelegate::AppDelegate()
{
}

AppDelegate::~AppDelegate()
{
	SimpleAudioEngine::sharedEngine()->end();
}

bool AppDelegate::applicationDidFinishLaunching()
{
	// 1. initialize lua
	auto pEngine = LuaEngine::defaultEngine();
	CCScriptEngineManager::sharedManager()->setScriptEngine(pEngine);

	auto pStack = pEngine->getLuaStack();

#if (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
	{
		CFBundleRef bundle = CFBundleGetMainBundle();
		if (bundle)
		{
			CFURLRef url = CFBundleCopyResourcesDirectoryURL(bundle);
			if (url)
			{
				char buf[PATH_MAX];
				if (CFURLGetFileSystemRepresentation(url, true, (UInt8 *)buf, sizeof(buf)))
				{
					std::string root(buf);
					if (!root.empty() && root.back() != '/')
						root.push_back('/');
					pEngine->addSearchPath((root + "lua").c_str());
					FileUtils::sharedFileUtils()->addSearchPath(root.c_str());
					FileUtils::sharedFileUtils()->addSearchPath((root + "lua").c_str());
					FileUtils::sharedFileUtils()->addSearchPath((root + "Resources").c_str());
				}
				CFRelease(url);
			}
		}
	}
#endif
#if (CC_TARGET_PLATFORM == CC_PLATFORM_LINUX || CC_TARGET_PLATFORM == CC_PLATFORM_WIN32 || CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
	pEngine->addSearchPath("../lua");
	FileUtils::sharedFileUtils()->addSearchPath("../lua");
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
	pEngine->addSearchPath("lua");
	FileUtils::sharedFileUtils()->addSearchPath("lua");
	CCLOG("------ Android writable path -> %s", FileUtils::sharedFileUtils()->getWritablePath().c_str());
#endif
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
	pEngine->addSearchPath("../../lua");
	FileUtils::sharedFileUtils()->addSearchPath("../../");
	FileUtils::sharedFileUtils()->addSearchPath("../../Resources");
#endif
#ifdef USE_WIN32_CONSOLE
	CCLOG("---------------------------");
	CCLOG("------ DEBUG CONSOLE ------");
	CCLOG("---------------------------\n");

	auto cwd = FileUtils::sharedFileUtils()->getWritablePath();
	string end = "Debug.win32\\";
	if (cwd.find(end) != string::npos)
	{
		cwd = cwd.substr(0, cwd.length() - end.length());
		auto luaPath = cwd + "projects\\NarutoSenki\\lua";
		auto resPath = cwd + "projects\\NarutoSenki\\Resources";
		auto root = cwd + "projects\\NarutoSenki";
		CCLOG("Lua path: %s", luaPath.c_str());
		CCLOG("Res path: %s", resPath.c_str());
		CCLOG("Root path: %s", root.c_str());
		pEngine->addSearchPath(luaPath.c_str());
		FileUtils::sharedFileUtils()->addSearchPath(luaPath.c_str());
		FileUtils::sharedFileUtils()->addSearchPath(resPath.c_str());
		FileUtils::sharedFileUtils()->addSearchPath(root.c_str());
	}
	CCLOG("Current work path: %s", cwd.c_str());
	CCLOG("---------------------------\n");
#endif

	auto eglView = GLView::sharedOpenGLView();

#if (CC_TARGET_PLATFORM == CC_PLATFORM_LINUX) || (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32) || (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
	pEngine->executeScriptFile(FileUtils::sharedFileUtils()->fullPathForFilename("window.lua").c_str());

	bool isFullscreen = false;
	int width = 1280;
	int height = 720;
	const char *title = "Naruto Senki";

	lua_getL;
	lua_getglobal(L, "ENABLE_FULL_SCREEN");
	if (lua_isboolean(L, 1))
		isFullscreen = lua_toboolean(L, 1);
	lua_pop(L, 1);
	lua_getglobal(L, "WINDOW_WIDTH");
	if (lua_isnumber(L, 1))
		width = lua_tointeger(L, 1);
	lua_pop(L, 1);
	lua_getglobal(L, "WINDOW_HEIGHT");
	if (lua_isnumber(L, 1))
		height = lua_tointeger(L, 1);
	lua_pop(L, 1);
	lua_getglobal(L, "WINDOW_TITLE");
	if (lua_isstring(L, 1))
		title = lua_tostring(L, 1);
	lua_pop(L, 1);

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
	eglView->setViewName(title);
	if (!isFullscreen)
	{
		eglView->setFrameSize(width, height);
	}
	else
	{
		eglView->setFrameSize(eglView->getFullscreenWidth(), eglView->getFullscreenHeight());
		eglView->enterFullscreen(0, 0);
	}
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_LINUX)
	eglView->setFrameSize(width, height);
	eglView->setTitle(title);
	eglView->setIcon("icon.png");
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
	eglView->setViewName(title);
#endif
#endif

	Director *pDirector = Director::sharedDirector();
	pDirector->setOpenGLView(eglView);
	pDirector->setDisplayStats(false);

	Internal::initAllSystems();

	string path = FileUtils::sharedFileUtils()->fullPathForFilename("main.lua");
	pEngine->executeScriptFile(path.c_str());

	return true;
}

void AppDelegate::applicationDidEnterBackground()
{
	// Cocos stops main-thread polling while backgrounded. Resolve an active LAN
	// session synchronously before stopping animation so sockets and match state
	// cannot become a half-alive ghost session. During Battle this is a local
	// forfeit; during lobby/loading the session is simply closed.
	auto &lanSession = nsv2::network::sharedLanSession();
	if (lanSession.networkActive() || lanSession.state() == nsv2::network::SessionState::Battle)
		lanSession.handleAppBackground();

	Director::sharedDirector()->stopAnimation();
	SimpleAudioEngine::sharedEngine()->pauseBackgroundMusic();
}

void AppDelegate::applicationWillEnterForeground()
{
	Director::sharedDirector()->startAnimation();
	SimpleAudioEngine::sharedEngine()->resumeBackgroundMusic();
}