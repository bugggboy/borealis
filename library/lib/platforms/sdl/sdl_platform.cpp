/*
    Copyright 2023 xfangfang

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <strings.h>
#include <unistd.h> /* chdir */

#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/time.hpp>
#include <borealis/platforms/sdl/sdl_platform.hpp>
#include <unordered_map>
#include <chrono>

#if defined(__ANDROID__)
// 由 sdl_video.cpp 提供：最近一次视频窗口绘制的时间戳（毫秒）
extern "C" long long wili_lastVideoDrawMs();
#endif

#if defined(IOS) || defined(TVOS)
#include <sys/utsname.h>

static bool isIPad()
{
    struct utsname systemInfo;
    uname(&systemInfo);
    return strncmp(systemInfo.machine, "iPad", 4) == 0;
}
#endif

namespace brls
{

SDLPlatform::SDLPlatform()
{
#ifdef ANDROID
    // Enable Fullscreen on Android
    VideoContext::FULLSCREEN = true;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
#elif defined(IOS) || defined(TVOS)
    // Enable Fullscreen on iOS
    VideoContext::FULLSCREEN = true;
    if (!isIPad())
    {
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    }
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
#elif defined(__APPLE__)
    // Same behavior as GLFW, change to the app's Resources directory if run in a ".app" bundle
    // Or to the executable's directory if run from other ways
    char *base_path = SDL_GetBasePath();
    if (base_path)
    {
        chdir(base_path);
        SDL_free(base_path);
    }
#endif

    // Init sdl
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER) < 0)
    {
        Logger::error("sdl: failed to initialize");
        return;
    }

    // Platform impls
    this->audioPlayer = new NullAudioPlayer();

    // override local
    if (Platform::APP_LOCALE_DEFAULT == LOCALE_AUTO)
    {
        SDL_Locale* locales = SDL_GetPreferredLocales();
        if (locales != nullptr && locales->language != nullptr)
        {
            std::unordered_map<std::string, std::string> sdl2brls = {
                { "zh_CN", LOCALE_ZH_HANS },
                { "zh_TW", LOCALE_ZH_HANT },
                { "ja_JP", LOCALE_JA },
                { "ko_KR", LOCALE_Ko },
                { "it_IT", LOCALE_IT }
            };
            std::string lang = std::string { locales->language };
            if (locales->country)
            {
                lang += "_" + std::string { locales->country };
            }
            if (sdl2brls.count(lang) > 0)
            {
                this->locale = sdl2brls[lang];
            }
            else
            {
                this->locale = LOCALE_EN_US;
            }
            brls::Logger::info("Set app locale: {}", this->locale);
            SDL_free(locales);
        }
    }
}

void SDLPlatform::createWindow(std::string windowTitle, uint32_t windowWidth, uint32_t windowHeight, float windowXPos, float windowYPos)
{
    this->videoContext = new SDLVideoContext(windowTitle, windowWidth, windowHeight, windowXPos, windowYPos);
    this->inputManager = new SDLInputManager(this->videoContext->getSDLWindow());
    this->imeManager   = new SDLImeManager(&this->otherEvent);
}

void SDLPlatform::restoreWindow()
{
    SDL_RestoreWindow(this->videoContext->getSDLWindow());
}

void SDLPlatform::setWindowAlwaysOnTop(bool enable)
{
    SDL_SetWindowAlwaysOnTop(this->videoContext->getSDLWindow(), enable ? SDL_TRUE : SDL_FALSE);
}

void SDLPlatform::setWindowSize(uint32_t windowWidth, uint32_t windowHeight)
{
    if (windowWidth > 0 && windowHeight > 0) {
        SDL_SetWindowSize(this->videoContext->getSDLWindow(), windowWidth, windowHeight);
    }
}

void SDLPlatform::setWindowSizeLimits(uint32_t windowMinWidth, uint32_t windowMinHeight, uint32_t windowMaxWidth, uint32_t windowMaxHeight)
{
    if (windowMinWidth > 0 && windowMinHeight > 0)
        SDL_SetWindowMinimumSize(this->videoContext->getSDLWindow(), windowMinWidth, windowMinHeight);
    if ((windowMaxWidth > 0 && windowMaxHeight > 0) && (windowMaxWidth > windowMinWidth && windowMaxHeight > windowMinHeight))
        SDL_SetWindowMaximumSize(this->videoContext->getSDLWindow(), windowMaxWidth, windowMaxHeight);
}

void SDLPlatform::setWindowPosition(int windowXPos, int windowYPos)
{
    SDL_SetWindowPosition(this->videoContext->getSDLWindow(), windowXPos, windowYPos);
}

void SDLPlatform::setWindowState(uint32_t windowWidth, uint32_t windowHeight, int windowXPos, int windowYPos)
{
    if (windowWidth > 0 && windowHeight > 0)
    {
        SDL_Window* win = this->videoContext->getSDLWindow();
        SDL_RestoreWindow(win);
        SDL_SetWindowSize(win, windowWidth, windowHeight);
        SDL_SetWindowPosition(win, windowXPos, windowYPos);
    }
}

void SDLPlatform::disableScreenDimming(bool disable, const std::string& reason, const std::string& app)
{
    if (disable)
    {
        SDL_DisableScreenSaver();
    }
    else
    {
        SDL_EnableScreenSaver();
    }
}

bool SDLPlatform::isScreenDimmingDisabled()
{
    return !SDL_IsScreenSaverEnabled();
}

void SDLPlatform::pasteToClipboard(const std::string& text)
{
    SDL_SetClipboardText(text.c_str());
}

std::string SDLPlatform::pasteFromClipboard()
{
    char *str = SDL_GetClipboardText();
    if (!str)
        return "";
    return std::string{str};
}

std::string SDLPlatform::getName()
{
    return "SDL";
}

bool SDLPlatform::processEvent(SDL_Event* event)
{
    if (event->type == SDL_QUIT)
    {
        return false;
    }
    else if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP)
    {
        auto* manager = this->inputManager;
        if (manager)
            manager->updateKeyboardState(event->key);
    }
    else if (event->type == SDL_MOUSEMOTION)
    {
        auto* manager = this->inputManager;
        if (manager)
            manager->updateMouseMotion(event->motion);
    }
    else if (event->type == SDL_MOUSEWHEEL)
    {
        auto* manager = this->inputManager;
        if (manager)
            manager->updateMouseWheel(event->wheel);
    }
    else if (event->type == SDL_CONTROLLERSENSORUPDATE)
    {
        auto* manager = this->inputManager;
        if (manager)
            manager->updateControllerSensorsUpdate(event->csensor);
    }
#if defined(IOS) || defined(ANDROID)
    else if (event->type == SDL_APP_WILLENTERBACKGROUND)
    {
        brls::Application::getWindowFocusChangedEvent()->fire(false);
    }
    else if (event->type == SDL_APP_WILLENTERFOREGROUND)
    {
        brls::Application::getWindowFocusChangedEvent()->fire(true);

    }
#endif
    else if (event->type != SDL_POLLSENTINEL)
    {
        // 其它没有处理的事件
        this->otherEvent.fire(event);
    }
    brls::Application::setActiveEvent(true);
    return true;
}

bool SDLPlatform::mainLoopIteration()
{
    SDL_Event event;
    bool hasEvent = false;
    while (SDL_PollEvent(&event))
    {
        // SDL_POLLSENTINEL 只是「本轮轮询结束」的标记，每轮 poll 都会有，
        // 不是真实输入。之前它也会走 processEvent 里的 setActiveEvent(true)，
        // 导致 activeEvent 恒为 true、主循环从不等待、SDLThread 吃满多核
        // （实测 381%），按键响应被严重拖慢。这里直接结束本轮 poll。
        if (event.type == SDL_POLLSENTINEL) break;
        if (!processEvent(&event))
        {
            return false;
        }
        hasEvent = true;
    }

    // 无真实输入、无动画、也没有视频窗口在绘制时，阻塞等待事件，避免主循环空转。
    // （视频播放中不等待：mpv 的属性/事件需要及时被 eventMainLoop 消费）
    if (!hasEvent && Ticking::runningTickings.empty())
    {
#if defined(__ANDROID__)
        bool canWait = !Application::hasFrameInput();
        if (canWait)
        {
            auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
            long long last = wili_lastVideoDrawMs();
            if (last > 0 && nowMs - last < 500) canWait = false; // 视频窗口活跃，不等待
        }
        if (canWait
            && SDL_WaitEventTimeout(&event, 50)   // 50ms≈20fps 轮询：足够应付高亮渐变等慢速动效，CPU 开销极低
            && event.type != SDL_POLLSENTINEL && !processEvent(&event))
        {
            return false;
        }
#else
        if (!Application::hasActiveEvent()
            && SDL_WaitEventTimeout(&event, (int)(brls::Application::getDeactivatedFrameTime() * 1000))
            && !processEvent(&event))
        {
            return false;
        }
#endif
    }
    return true;
}

AudioPlayer* SDLPlatform::getAudioPlayer()
{
    return this->audioPlayer;
}

VideoContext* SDLPlatform::getVideoContext()
{
    return this->videoContext;
}

InputManager* SDLPlatform::getInputManager()
{
    return this->inputManager;
}

ImeManager* SDLPlatform::getImeManager() {
    return this->imeManager;
}

SDLPlatform::~SDLPlatform()
{
    delete this->audioPlayer;
    delete this->inputManager;
    delete this->imeManager;
    delete this->videoContext;
}

} // namespace brls
