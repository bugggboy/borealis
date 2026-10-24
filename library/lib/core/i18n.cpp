/*
    Copyright 2020-2021 natinusala

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

#include <borealis/core/application.hpp>
#include <borealis/core/assets.hpp>
#include <borealis/core/i18n.hpp>
#ifdef USE_BOOST_FILESYSTEM
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#elif __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include("experimental/filesystem")
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#error "Failed to include <filesystem> header!"
#endif
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#ifndef BRLS_I18N_PREFIX
#define BRLS_I18N_PREFIX ""
#endif

namespace brls
{

static nlohmann::json defaultLocale = {};
static nlohmann::json currentLocale = {};

static void loadLocale(std::string locale, nlohmann::json* target)
{
    if (locale.empty())
        return;
#ifdef USE_LIBROMFS
    auto localePath = romfs::list("i18n/" + locale);
    if (localePath.empty())
    {
        Logger::error("Cannot load locale {}: directory i18n/{} doesn't exist", locale, locale);
        return;
    }
    for (auto& entry : localePath)
    {
        std::string path = entry.string();
        std::string name = entry.filename().string();
        if (!endsWith(name, ".json"))
            continue;

        (*target)[name.substr(0, name.length() - 5)] = nlohmann::json::parse(romfs::get(path).string());
    }
#else
    std::string localePath = BRLS_ASSET("i18n/" + locale);

    if (!fs::exists(localePath))
    {
        Logger::error("Cannot load locale {}: directory {} doesn't exist", locale, localePath);
        return;
    }
    else if (!fs::is_directory(localePath))
    {
        Logger::error("Cannot load locale {}: {} isn't a directory", locale, localePath);
        return;
    }

    // Iterate over all JSON files in the directory
    for (const fs::directory_entry& entry : fs::directory_iterator(localePath))
    {
        if (fs::is_directory(entry))
            continue;

        std::string name = entry.path().filename().string();

        if (!endsWith(name, ".json"))
            continue;

        std::string path = entry.path().string();

        nlohmann::json strings;

        std::ifstream jsonStream;
        jsonStream.open(path);

        try
        {
            jsonStream >> strings;
        }
        catch (const std::exception& e)
        {
            Logger::error("Error while loading \"{}\": {}", path, e.what());
        }

        jsonStream.close();

        (*target)[name.substr(0, name.length() - 5)] = strings;
    }
#endif /* USE_LIBROMFS */
}

void loadTranslations()
{
    loadLocale(LOCALE_DEFAULT, &defaultLocale);

    std::string currentLocaleName = Application::getLocale();
    if (currentLocaleName != LOCALE_DEFAULT)
        loadLocale(currentLocaleName, &currentLocale);
}

namespace internal
{
    // 非抛异常版本的查找：类型不对/不存在都返回 nullptr
    static const nlohmann::json* lookupString(const nlohmann::json& doc, const nlohmann::json::json_pointer& ptr)
    {
        if (!doc.is_object() || !doc.contains(ptr)) return nullptr;
        const nlohmann::json& value = doc.at(ptr);
        return value.is_string() ? &value : nullptr;
    }

    std::string getRawStr(std::string stringName)
    {
        nlohmann::json::json_pointer pointer;

        try
        {
            pointer = nlohmann::json::json_pointer("/" + std::string(BRLS_I18N_PREFIX) + stringName);
        }
        catch (const std::exception& e)
        {
            Logger::error("Error while getting string \"{}\": {}", stringName, e.what());
            return stringName;
        }

        // 这里刻意不用异常：Android 上从本线程抛出 C++ 异常会走 libc++abi 的 unwinder，
        // 实测在 _Unwind_RaiseException_Phase2 里直接 SIGSEGV（即使外层 catch(...) 也拦不住）。
        // 缺失的 key 本身是正常情况（比如 currentLocale 还没加载），不应该用异常来表达。
        if (const nlohmann::json* found = lookupString(currentLocale, pointer))
            return *found;
        if (const nlohmann::json* found = lookupString(defaultLocale, pointer))
            return *found;

        // Fallback to returning the string name
        return stringName;
    }
} // namespace internal

inline namespace literals
{
    std::string operator"" _i18n(const char* str, size_t len)
    {
        return internal::getRawStr(std::string(str, len));
    }

} // namespace literals

} // namespace brls
