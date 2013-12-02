/* 
 * libnpengine: Nitroplus script interpreter
 * Copyright (C) 2013 Mislav Blažević <krofnica996@gmail.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * */
#include "nsbfile.hpp"
#include "game.hpp"
#include "drawable.hpp"
#include "resourcemgr.hpp"
#include "nsbmagic.hpp"
#include "text.hpp"

#include <iostream>
#include <boost/chrono.hpp>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <sfeMovie/Movie.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Audio/Music.hpp>
#include <SFML/Graphics/RenderTexture.hpp>

#define SPECIAL_POS_NUM 7

enum : int32_t
{
    POS_CENTER = -1,
    POS_IN_BOTTOM = -2,
    POS_MIDDLE = -3,
    POS_ON_LEFT = -4,
    POS_OUT_TOP = -5,
    POS_IN_TOP = -6,
    POS_OUT_RIGHT = -7
};

const std::string SpecialPos[SPECIAL_POS_NUM] =
{
    "Center", "InBottom", "Middle",
    "OnLeft", "OutTop", "InTop",
    "OutRight"
};

std::function<int32_t(int32_t)> SpecialPosTable[SPECIAL_POS_NUM] =
{
  [] (int32_t x) { return WINDOW_WIDTH / 2 - x / 2; },
  [] (int32_t y) { return WINDOW_HEIGHT - y; },
  [] (int32_t y) { return WINDOW_HEIGHT / 2 + y / 2; },
  [] (int32_t x) { return 0; },
  [] (int32_t y) { return 0; },
  [] (int32_t y) { return 0; },
  [] (int32_t x) { return 0; }
};

NsbInterpreter::NsbInterpreter(Game* pGame, const string& InitScript) :
pGame(pGame),
StopInterpreter(false),
ScriptThread(&NsbInterpreter::ThreadMain, this, InitScript)
{
#ifdef _WIN32
    Text::Initialize("fonts-japanese-gothic.ttf");
#else
    Text::Initialize("/etc/alternatives/fonts-japanese-gothic.ttf");
#endif
}

NsbInterpreter::~NsbInterpreter()
{
}

void NsbInterpreter::RegisterBuiltins()
{
    Builtins.resize(0xFF, nullptr);
    Builtins[MAGIC_DRAW_TO_TEXTURE] = &NsbInterpreter::DrawToTexture;
    Builtins[MAGIC_CREATE_TEXTURE] = &NsbInterpreter::CreateTexture;
    Builtins[MAGIC_LOAD_MOVIE] = &NsbInterpreter::LoadMovie;
    Builtins[MAGIC_APPLY_MASK] = &NsbInterpreter::ApplyMask;
    Builtins[MAGIC_CREATE_COLOR] = &NsbInterpreter::CreateColor;
    Builtins[MAGIC_LOAD_TEXTURE] = &NsbInterpreter::LoadTexture;
    Builtins[MAGIC_CALL] = &NsbInterpreter::Call;
    Builtins[MAGIC_CONCAT] = &NsbInterpreter::Concat;
    Builtins[MAGIC_DESTROY] = &NsbInterpreter::Destroy;
    Builtins[MAGIC_SET_OPACITY] = &NsbInterpreter::SetOpacity;
    Builtins[MAGIC_BIND_IDENTIFIER] = &NsbInterpreter::BindIdentifier;
    Builtins[MAGIC_FWN_UNK] = &NsbInterpreter::End; // Fuwanovel hack, unknown purpose
    Builtins[MAGIC_BEGIN] = &NsbInterpreter::Begin;
    Builtins[MAGIC_END] = &NsbInterpreter::End;
    Builtins[MAGIC_CLEAR_PARAMS] = &NsbInterpreter::ClearParams;
    Builtins[MAGIC_UNK3] = &NsbInterpreter::ClearParams; // Unknown if this hack is still needed
    //Builtins[MAGIC_FORMAT] = &NsbInterpreter::Format; // Depends on ArrayRead
}

void NsbInterpreter::ThreadMain(string InitScript)
{
    RegisterBuiltins();

    // TODO: from .map file
    LoadScript("nss/function_steinsgate.nsb");
    LoadScript("nss/function.nsb");
    LoadScript("nss/extra_achievements.nsb");
    LoadScript("nss/function_select.nsb");
    LoadScript("nss/function_stand.nsb");

    pScript = sResourceMgr->GetResource<NsbFile>(InitScript);
    //CallFunction(LoadedScripts[LoadedScripts.size() - 1], "StArray");
    do
    {
        while (!RunInterpreter) Sleep(10); // yield? mutex?
        pLine = pScript->GetNextLine();
        ExecuteLine();
    } while (!StopInterpreter);
}

void NsbInterpreter::Stop()
{
    StopInterpreter = true;
}

void NsbInterpreter::Pause()
{
    RunInterpreter = false;
}

void NsbInterpreter::Start()
{
    RunInterpreter = true;
}

void NsbInterpreter::ExecuteLine()
{
    if (NsbAssert(pScript, "Interpreting null script") || NsbAssert(pLine, "Interpreting null line"))
    {
        Stop();
        return;
    }

    if (pLine->Magic < Builtins.size())
    {
        if (BuiltinFunc pFunc = Builtins[pLine->Magic])
        {
            (this->*pFunc)();
            return;
        }
    }

    switch (pLine->Magic)
    {
        case uint16_t(MAGIC_SET_PLACEHOLDER):
            Placeholders.push(Params[Params.size() - 1]);
            Params.resize(Params.size() - 1);
            break;
        case uint16_t(MAGIC_PLACEHOLDER_PARAM):
            Params.push_back({"PH", ""});
            break;
        case uint16_t(MAGIC_APPLY_BLUR):
            pGame->GLCallback(std::bind(&NsbInterpreter::ApplyBlur, this,
                              CacheHolder<Drawable>::Read(GetParam<string>(0)),
                              GetParam<string>(1)));
            return;
        case uint16_t(MAGIC_DISPLAY_TEXT):
            HandleName = GetParam<string>(0);
            DisplayText(GetParam<string>(1));
            return;
        case uint16_t(MAGIC_CREATE_BOX):
            HandleName = GetParam<string>(0);
            CreateBox(GetParam<int32_t>(1), GetParam<int32_t>(2), GetParam<int32_t>(3),
                      GetParam<int32_t>(4), GetParam<int32_t>(5), GetParam<bool>(6));
            break;
        case uint16_t(MAGIC_ARRAY_READ):
            ArrayRead(pLine->Params[0], GetParam<int32_t>(1));
            break;
        case uint16_t(MAGIC_CREATE_ARRAY):
            for (uint32_t i = 1; i < Params.size(); ++i)
                Arrays[pLine->Params[0]].Members.push_back(std::make_pair(string(), ArrayVariable(Params[i])));
            break;
        case uint16_t(MAGIC_SET_TEXTBOX_ATTRIBUTES):
            SetTextboxAttributes(GetParam<string>(0), GetParam<int32_t>(1),
                                 GetParam<string>(2), GetParam<int32_t>(3),
                                 GetParam<string>(4), GetParam<string>(5),
                                 GetParam<int32_t>(6), GetParam<string>(7));
            break;
        case uint16_t(MAGIC_SET_FONT_ATTRIBUTES):
            SetFontAttributes(GetParam<string>(0), GetParam<int32_t>(1),
                              GetParam<string>(2), GetParam<string>(3),
                              GetParam<int32_t>(4), GetParam<string>(5));
            break;
        case uint16_t(MAGIC_SET_AUDIO_STATE):
            SetAudioState(GetParam<string>(0), GetParam<int32_t>(1),
                          GetParam<int32_t>(2), GetParam<string>(3));
            break;
        case uint16_t(MAGIC_SET_AUDIO_LOOP):
            SetAudioLoop(GetParam<string>(0), GetParam<bool>(1));
            break;
        case uint16_t(MAGIC_SET_AUDIO_RANGE): break; // SFML bug #203
            SetAudioRange(GetParam<string>(0), GetParam<int32_t>(1), GetParam<int32_t>(2));
            break;
        case uint16_t(MAGIC_LOAD_AUDIO):
            LoadAudio(GetParam<string>(0), GetParam<string>(1), GetParam<string>(2) + ".ogg");
            break;
        case uint16_t(MAGIC_START_ANIMATION):
            StartAnimation(GetParam<string>(0), GetParam<int32_t>(1), GetParam<int32_t>(2),
                           GetParam<int32_t>(3), GetParam<string>(4), GetParam<bool>(5));
            break;
        case uint16_t(MAGIC_UNK29):
            // This is (mistakenly) done by MAGIC_CALL
            //SetVariable(pLine->Params[0], {"STRING", GetVariable<string>(pLine->Params[1])});
            break;
        case uint16_t(MAGIC_SLEEP_MS):
            Sleep(GetVariable<int32_t>(Params[0].Value));
            break;
        case uint16_t(MAGIC_GET_MOVIE_TIME):
            GetMovieTime(GetParam<string>(0));
            break;
        case uint16_t(MAGIC_CALL_SCRIPT):
            // TODO: extract entry function & convert nss to nsb
            //CallScript(pLine->Params[0]);
            break;
        case uint16_t(MAGIC_UNK5):
            Params[0] = {"STRING", string()}; // Hack
            break;
        case uint16_t(MAGIC_TEXT):
            pGame->GLCallback(std::bind(&NsbInterpreter::ParseText, this,
                              GetParam<string>(0), GetParam<string>(1), GetParam<string>(2)));
            break;
        case uint16_t(MAGIC_SET):
            if (pLine->Params[0] == "__array_variable__")
                ;//*ArrayParams[ArrayParams.size() - 1] = Params[0];
            else
                SetVariable(pLine->Params[0], Params[0]);
            break;
        case uint16_t(MAGIC_GET):
            Params.push_back(Variables[pLine->Params[0]]);
            break;
        case uint16_t(MAGIC_PARAM):
            Params.push_back({pLine->Params[0], pLine->Params[1]});
            break;
        case uint16_t(MAGIC_SET_DISPLAY_STATE):
            SetDisplayState(GetParam<string>(0), GetParam<string>(1));
            break;
        case uint16_t(MAGIC_CALLBACK):
            pGame->RegisterCallback(static_cast<sf::Keyboard::Key>(pLine->Params[0][0] - 'A'), pLine->Params[1]);
            break;
        default:
            //std::cout << "Unknown magic: " << std::hex << pLine->Magic << std::dec << std::endl;
            break;
    }
}

void NsbInterpreter::DrawToTexture()
{
    pGame->GLCallback(std::bind(&NsbInterpreter::GLDrawToTexture, this,
                      CacheHolder<sf::RenderTexture>::Read(HandleName),
                      GetParam<int32_t>(1), GetParam<int32_t>(2), GetParam<string>(3)));

}

void NsbInterpreter::CreateTexture()
{
    HandleName = GetParam<string>(0);
    pGame->GLCallback(std::bind(&NsbInterpreter::GLCreateTexture, this,
                      GetParam<int32_t>(1), GetParam<int32_t>(2), GetParam<string>(3)));

}

void NsbInterpreter::ClearParams()
{
    Params.clear();
    ArrayParams.clear();
    Placeholders = std::queue<Variable>();
}

void NsbInterpreter::Begin()
{
    // Turn params into variables
    for (uint32_t i = 1; i < pLine->Params.size(); ++i)
        SetVariable(pLine->Params[i], Params[i - 1]);
}

void NsbInterpreter::ApplyMask()
{
    if (Drawable* pDrawable = CacheHolder<Drawable>::Read(GetParam<string>(0)))
    {
        pGame->GLCallback(std::bind(&NsbInterpreter::GLApplyMask, this, pDrawable,
                          GetParam<int32_t>(1), GetParam<int32_t>(2), GetParam<int32_t>(3),
                          GetParam<int32_t>(4), GetParam<string>(5), GetParam<string>(6),
                          GetParam<bool>(7)));
    }
    else
    {
        std::cout << "Applying mask to NULL drawable!" << std::endl;
        WriteTrace(std::cout);
    }
}

void NsbInterpreter::LoadMovie()
{
    HandleName = GetParam<string>(0);
    pGame->GLCallback(std::bind(&NsbInterpreter::GLLoadMovie, this, GetParam<int32_t>(1),
                      GetParam<int32_t>(2), GetParam<int32_t>(3), GetParam<bool>(4),
                      GetParam<bool>(5), GetParam<string>(6), GetParam<bool>(7)));
}

void NsbInterpreter::CreateColor()
{
    HandleName = GetParam<string>(0);
    pGame->GLCallback(std::bind(&NsbInterpreter::GLCreateColor, this,
                      GetParam<int32_t>(1), GetParam<int32_t>(2), GetParam<int32_t>(3),
                      GetParam<int32_t>(4), GetParam<int32_t>(5), GetParam<string>(6)));
}

void NsbInterpreter::SetOpacity()
{
    HandleName = GetParam<string>(0);
    if (HandleName.back() == '*')
        WildcardCall(HandleName, std::bind(&NsbInterpreter::NSBSetOpacity, this,
                     std::placeholders::_1, GetParam<int32_t>(1), GetParam<int32_t>(2),
                     GetParam<string>(3), GetParam<bool>(4)));
    else
        NSBSetOpacity(CacheHolder<Drawable>::Read(HandleName), GetParam<int32_t>(1),
                      GetParam<int32_t>(2), GetParam<string>(3), GetParam<bool>(4));
}

void NsbInterpreter::End()
{
    if (NsbAssert(!Returns.empty(), "Empty return stack"))
        return;

    pScript = Returns.top().pScript;
    pScript->SetSourceIter(Returns.top().SourceLine);
    Returns.pop();
}

void NsbInterpreter::LoadTexture()
{
    int32_t Pos[2];
    for (int32_t i = 2; i <= 3; ++i)
    {
        if (Params[i].Type == "STRING")
        {
            for (int32_t j = 0; j < SPECIAL_POS_NUM; ++j)
                if (Params[i].Value == SpecialPos[j])
                    Pos[i - 2] = -(j + 1);
        }
        else
            Pos[i - 2] = GetParam<int32_t>(i);
    }

    HandleName = GetParam<string>(0);
    pGame->GLCallback(std::bind(&NsbInterpreter::GLLoadTexture, this,
                      GetParam<int32_t>(1), Pos[0], Pos[1], GetParam<string>(4)));
}

void NsbInterpreter::Destroy()
{
    HandleName = GetParam<string>(0);
    // Hack: Do not destroy * (aka everything)
    if (HandleName.back() == '*' && HandleName.size() != 1)
    {
        WildcardCall(HandleName, [this](Drawable* pDrawable)
        {
            pGame->GLCallback(std::bind(&NsbInterpreter::GLDestroy, this, pDrawable));
            CacheHolder<Drawable>::Write(HandleName, nullptr);
        });
    }
    else
    {
        pGame->GLCallback(std::bind(&NsbInterpreter::GLDestroy, this, CacheHolder<Drawable>::Read(HandleName)));
        CacheHolder<Drawable>::Write(HandleName, nullptr);
    }
}

void NsbInterpreter::Call()
{
    const char* FuncName = pLine->Params[0].c_str();

    // Find function override
    if (std::strcmp(FuncName, "MovieWaitSG") == 0)
    {
        GetMovieTime("ムービー");
        Sleep(GetVariable<int32_t>(Params[0].Value));
        pGame->GLCallback(std::bind(&Game::RemoveDrawable, pGame,
                          CacheHolder<Drawable>::Read("ムービー")));
        return;
    }

    // Find function locally
    if (CallFunction(pScript, FuncName))
        return;

    // Find function globally
    for (uint32_t i = 0; i < LoadedScripts.size(); ++i)
        if (CallFunction(LoadedScripts[i], FuncName))
            return;

    std::cout << "Failed to lookup function symbol " << FuncName << std::endl;
}

void NsbInterpreter::Format()
{
    boost::format Fmt(Params[0].Value);
    for (uint8_t i = 1; i < Params.size(); ++i)
        Fmt % Params[i].Value;
    Params[0].Value = Fmt.str();
}

void NsbInterpreter::Concat()
{
    uint32_t First = Params.size() - 2, Second = Params.size() - 1;
    NsbAssert(Params[First].Type == Params[Second].Type,
              "Concating params of different types (% and %)",
              Params[First].Type, Params[Second].Type);
    if (Params[First].Type == "INT" && Params[Second].Type == "INT")
        Params[First].Value = boost::lexical_cast<string>(
                              boost::lexical_cast<int32_t>(Params[First].Value) +
                              boost::lexical_cast<int32_t>(Params[Second].Value));
    else
        Params[First].Value += Params[Second].Value;
    Params.resize(Second);
}

template <class T> void NsbInterpreter::WildcardCall(std::string Handle, T Func)
{
    for (auto i = CacheHolder<Drawable>::ReadFirstMatch(Handle);
         i != CacheHolder<Drawable>::Cache.end();
         i = CacheHolder<Drawable>::ReadNextMatch(Handle, i))
    {
        HandleName = i->first;
        Func(i->second);
    }
}

template <class T> T NsbInterpreter::GetVariable(const string& Identifier)
{
    // NULL object
    if (Identifier == "@")
        return T();

    // Needs special handling, currently a hack
    if (Identifier[0] == '@')
        return boost::lexical_cast<T>(string(Identifier, 1, Identifier.size() - 1));

    auto iter = Variables.find(Identifier);

    try
    {
        if (iter == Variables.end())
            return boost::lexical_cast<T>(Identifier);
        return boost::lexical_cast<T>(iter->second.Value);
    }
    catch (...)
    {
        std::cout << "Failed to cast " << Identifier << " to correct type." << std::endl;
        return T();
    }
}

template <class T> T NsbInterpreter::GetParam(int32_t Index)
{
    if (Params.size() > Index && Params[Index].Type == "PH")
    {
        if (!Placeholders.empty())
        {
            Variable Var = Placeholders.front();
            Placeholders.pop();
            return boost::lexical_cast<T>(Var.Value);
        }
    }
    return GetVariable<T>(pLine->Params[Index]);
}

template <> bool NsbInterpreter::GetParam(int32_t Index)
{
    return Boolify(GetParam<string>(Index));
}

void NsbInterpreter::GLCreateTexture(int32_t Width, int32_t Height, const string& Color)
{
    if (sf::RenderTexture* pTexture = CacheHolder<sf::RenderTexture>::Read(HandleName))
        delete pTexture;

    sf::RenderTexture* pTexture = new sf::RenderTexture;
    pTexture->create(Width, Height);
    CacheHolder<sf::RenderTexture>::Write(HandleName, pTexture);
}

void NsbInterpreter::GLDrawToTexture(sf::RenderTexture* pTexture, int32_t x, int32_t y, const string& File)
{
    sf::Texture TempTexture;
    uint32_t Size;
    char* pPixels = sResourceMgr->Read(File, &Size);
    NsbAssert(pPixels, "Failed to load % pixels", File);
    NsbAssert(TempTexture.loadFromMemory(pPixels, Size), "Failed to load pixels from % in memory", File);
    sf::Sprite TempSprite(TempTexture);
    TempSprite.setPosition(x, y);
    pTexture->draw(TempSprite);
    pTexture->display();
}

void NsbInterpreter::ApplyBlur(Drawable* pDrawable, const string& Heaviness)
{
    if (!pDrawable)
    {
        std::cout << "Applying blur to NULL drawable!" << std::endl;
        WriteTrace(std::cout);
    }
    else
        pDrawable->SetBlur(Heaviness);
}

void NsbInterpreter::GLApplyMask(Drawable* pDrawable, int32_t Time, int32_t Start, int32_t End, int32_t Range, const string& Tempo, string File, bool Wait)
{
    uint32_t Size;
    char* pPixels = sResourceMgr->Read(File, &Size);
    NsbAssert(pPixels, "Failed to load % pixels", File);
    sf::Texture* pTexture = new sf::Texture;
    NsbAssert(pTexture->loadFromMemory(pPixels, Size), "Failed to load pixels from % in memory", File);
    pDrawable->SetMask(pTexture, Start, End, Time);
}

void NsbInterpreter::CreateBox(int32_t unk0, int32_t x, int32_t y, int32_t Width, int32_t Height, bool unk1)
{
    sf::IntRect* pRect = new sf::IntRect(x, y, Width, Height);
    CacheHolder<sf::IntRect>::Write(HandleName, pRect);
}

void NsbInterpreter::BindIdentifier()
{
    HandleName = pLine->Params[0];
    ArrayVariable* Var = &Arrays[HandleName];
    for (uint32_t i = 1; i < Params.size(); ++i)
        Var->Members[i - 1].first = Params[i].Value;
}

void NsbInterpreter::ArrayRead(const string& HandleName, int32_t Depth)
{
    const string* MemberName = &HandleName;
    ArrayVariable* pVariable = nullptr;

    while (Depth --> 0) // Depth goes to zero; 'cause recursion is too mainstream
    {
        // TODO: check if exists
        ArrayMembers& Members = Arrays[*MemberName].Members;
        for (uint32_t i = 0; i < Members.size(); ++i)
        {
            if (Members[i].first == Params[Params.size() - Depth - 2].Value)
            {
                MemberName = &Members[i].first;
                pVariable = &Members[i].second;
                break;
            }
        }
    }

    if (!pVariable)
        return;

    ArrayParams.push_back(pVariable);
    Params.push_back(*pVariable);
}

void NsbInterpreter::GLCreateColor(int32_t Priority, int32_t x, int32_t y, int32_t Width, int32_t Height, string Color)
{
    // Workaround
    if (HandleName == "クリア黒")
        return;

    if (Drawable* pDrawable = CacheHolder<Drawable>::Read(HandleName))
    {
        pGame->RemoveDrawable(pDrawable);
        delete pDrawable;
    }

    uint32_t IntColor;

    std::transform(Color.begin(), Color.end(), Color.begin(), ::tolower);
    if (Color[0] == '#')
    {
        Color = string(Color.c_str() + 1);
        std::stringstream ss(Color);
        ss >> std::hex >> IntColor;
    }
    else
    {
        if (Color == "black")
            IntColor = 0;
        else if (Color == "white")
            IntColor = 0xFFFFFF;
        else if (Color == "blue")
            IntColor = 0xFF;
        else
            NsbAssert(false, "Unknown color: %, ", Color);
    }

    sf::Image ColorImage;
    ColorImage.create(Width, Height, sf::Color(IntColor / 0x10000, (IntColor / 0x100) % 0x100, IntColor % 0x100));
    sf::Texture* pTexture = new sf::Texture;
    NsbAssert(pTexture->loadFromImage(ColorImage), "Failed to create color % texture to handle %.", Color, HandleName);
    sf::Sprite* pSprite = new sf::Sprite(*pTexture);
    pSprite->setPosition(x, y);
    CacheHolder<Drawable>::Write(HandleName, new Drawable(pSprite, Priority, DRAWABLE_TEXTURE));
}

void NsbInterpreter::SetTextboxAttributes(const string& Handle, int32_t unk0,
                                          const string& Font, int32_t unk1,
                                          const string& Color1, const string& Color2,
                                          int32_t unk2, const string& unk3)
{
}

void NsbInterpreter::SetFontAttributes(const string& Font, int32_t size,
                                       const string& Color1, const string& Color2,
                                       int32_t unk0, const string& unk1)
{
}

void NsbInterpreter::SetAudioState(const string& HandleName, int32_t NumSeconds,
                                   int32_t Volume, const string& Tempo)
{
    if (sf::Music* pMusic = CacheHolder<sf::Music>::Read(HandleName))
        pMusic->setVolume(Volume / 10);
}

void NsbInterpreter::SetAudioLoop(const string& HandleName, bool Loop)
{
    if (sf::Music* pMusic = CacheHolder<sf::Music>::Read(HandleName))
        pMusic->setLoop(Loop);
}

void NsbInterpreter::GLDestroy(Drawable* pDrawable)
{
    if (pDrawable)
    {
        pGame->RemoveDrawable(pDrawable);
        delete pDrawable;
    }
}

void NsbInterpreter::LoadAudio(const string& HandleName, const string& Type, const string& File)
{
    if (sf::Music* pMusic = CacheHolder<sf::Music>::Read(HandleName))
    {
        pMusic->stop();
        delete pMusic;
    }

    sf::Music* pMusic = new sf::Music;
    uint32_t Size;
    char* pMusicData = sResourceMgr->Read(File, &Size);
    if (!pMusicData)
    {
        std::cout << "Failed to read music " << File << std::endl;
        WriteTrace(std::cout);
        CacheHolder<sf::Music>::Write(HandleName, nullptr);
        return;
    }
    NsbAssert(pMusic->openFromMemory(pMusicData, Size), "Failed to load music %!", File);
    CacheHolder<sf::Music>::Write(HandleName, pMusic);
}

void NsbInterpreter::SetAudioRange(const string& HandleName, int32_t Begin, int32_t End)
{
    if (sf::Music* pMusic = CacheHolder<sf::Music>::Read(HandleName))
        pMusic->setPlayingOffset(sf::milliseconds(Begin));
}

void NsbInterpreter::StartAnimation(const string& HandleName, int32_t Time,
                                    int32_t x, int32_t y, const string& Tempo, bool Wait)
{
    if (Drawable* pDrawable = CacheHolder<Drawable>::Read(HandleName))
        pDrawable->Animate(x, y, Time);
}

void NsbInterpreter::ParseText(const string& HandleName, const string& Box, const string& XML)
{
    string NewHandle = Box + "/" + HandleName;
    SetVariable("$SYSTEM_present_text", { "STRING", NewHandle });
    if (Drawable* pText = CacheHolder<Drawable>::Read(NewHandle))
        delete pText;
    Text* pText = new Text(XML);
    CacheHolder<Drawable>::Write(NewHandle, pText);
}

void NsbInterpreter::DisplayText(const string& unk)
{
    if (Text* pText = (Text*)CacheHolder<Drawable>::Read(HandleName))
    {
        if (sf::Music* pMusic = pText->Voices[0].pMusic)
        {
            pMusic->play();
            pText->pCurrentMusic = pMusic;
        }
        pGame->SetText(pText);
    }
    Pause();
}

void NsbInterpreter::Sleep(int32_t ms)
{
    boost::this_thread::sleep_for(boost::chrono::milliseconds(ms));
}

void NsbInterpreter::SetVariable(const string& Identifier, const Variable& Var)
{
    Variables[Identifier] = Var;
}

void NsbInterpreter::GetMovieTime(const string& HandleName)
{
    Params.clear();
    if (Drawable* pDrawable = CacheHolder<Drawable>::Read(HandleName))
    {
        if (sfe::Movie* pMovie = dynamic_cast<sfe::Movie*>(pDrawable->Get()))
            Params.push_back({"INT", boost::lexical_cast<string>(pMovie->getDuration().asMilliseconds())});
        else
            std::cout << "Failed to get movie duration because Drawable is not movie" << std::endl;
    }
    else
        std::cout << "Failed to get movie time because there is no Drawable " << HandleName << std::endl;
}

bool NsbInterpreter::Boolify(const string& String)
{
    if (String == "true")
        return true;
    else if (String == "false")
        return false;
    NsbAssert(false, "Invalid boolification of string: ", String.c_str());
    return false; // Silence gcc
}

void NsbInterpreter::SetDisplayState(const string& HandleName, const string& State)
{
    if (Drawable* pDrawable = CacheHolder<Drawable>::Read(HandleName))
    {
        if (State == "Play")
        {
            if (sfe::Movie* pMovie = dynamic_cast<sfe::Movie*>(pDrawable->Get()))
            {
                pGame->AddDrawable(pDrawable);
                pMovie->play();
            }
            else
                NsbAssert(false, "Attempted to Play non-movie object ", HandleName);
        }

    }
    else if (sf::Music* pMusic = CacheHolder<sf::Music>::Read(HandleName))
        if (State == "Play")
            pMusic->play();
}

void NsbInterpreter::NSBSetOpacity(Drawable* pDrawable, int32_t Time, int32_t Opacity, const string& Tempo, bool Wait)
{
    if (!pDrawable)
        return;

    if (/*Text* pText = */dynamic_cast<Text*>(pDrawable))
    {
        if (Opacity == 0)
        {
            pGame->GLCallback(std::bind(&Game::ClearText, pGame));
            CacheHolder<Drawable>::Write(HandleName, nullptr); // hack: see Game::ClearText
        }
    }
    else
        pDrawable->SetOpacity(Opacity, Time, FADE_TEX);
}

void NsbInterpreter::GLLoadMovie(int32_t Priority, int32_t x, int32_t y, bool Loop,
                                 bool unk0, const string& File, bool unk1)
{
    if (Drawable* pDrawable = CacheHolder<Drawable>::Read(HandleName))
    {
        pGame->RemoveDrawable(pDrawable);
        delete pDrawable;
    }

    sfe::Movie* pMovie = new sfe::Movie;
    pMovie->setLoop(Loop); // NYI
    pMovie->openFromFile(File);
    string BoxHandle(HandleName, 0, HandleName.find_first_of("/"));
    if (sf::IntRect* pRect = CacheHolder<sf::IntRect>::Read(BoxHandle))
    {
        pMovie->setTextureRect(*pRect);
        pMovie->setPosition(pRect->left, pRect->top);
    }
    else
        pMovie->setPosition(x, y); // Maybe add xy and pRect->xy?

    Drawable* pDrawable = new Drawable(pMovie, Priority, DRAWABLE_MOVIE);
    CacheHolder<Drawable>::Write(HandleName, pDrawable);
    pGame->AddDrawable(pDrawable);
}

void NsbInterpreter::GLLoadTexture(int32_t Priority, int32_t x, int32_t y, const string& File)
{
    if (Drawable* pDrawable = CacheHolder<Drawable>::Read(HandleName))
    {
        pGame->RemoveDrawable(pDrawable);
        delete pDrawable;
    }

    sf::Texture* pTexture;

    // Load from texture instead of file
    if (sf::RenderTexture* pRenderTexture = CacheHolder<sf::RenderTexture>::Read(File))
    {
        // TODO: Dont copy
        pTexture = new sf::Texture(pRenderTexture->getTexture());
    }
    else
    {
        pTexture = new sf::Texture;
        uint32_t Size;
        char* pTexData = sResourceMgr->Read(File, &Size);
        if (!pTexData)
        {
            std::cout << "Failed to read texture " << File << std::endl;
            delete pTexture;
            CacheHolder<Drawable>::Write(HandleName, nullptr);
            return;
        }
        NsbAssert(pTexture->loadFromMemory(pTexData, Size), "Failed to load texture %!", File);
    }

    sf::Sprite* pSprite = new sf::Sprite(*pTexture);
    // TODO: Positions are x/y specific!
    if (x < 0 && x >= -SPECIAL_POS_NUM)
        x = SpecialPosTable[-(x + 1)](pTexture->getSize().x);
    if (y < 0 && y >= -SPECIAL_POS_NUM)
        y = SpecialPosTable[-(y + 1)](pTexture->getSize().y);
    pSprite->setPosition(x, y);
    Drawable* pDrawable = new Drawable(pSprite, Priority, DRAWABLE_TEXTURE);
    CacheHolder<Drawable>::Write(HandleName, pDrawable);
    pGame->AddDrawable(pDrawable);
}

void NsbInterpreter::LoadScript(const string& FileName)
{
    LoadedScripts.push_back(sResourceMgr->GetResource<NsbFile>(FileName));
}

void NsbInterpreter::CallScript(const string& FileName)
{
    pScript = sResourceMgr->GetResource<NsbFile>(FileName);
}

bool NsbInterpreter::CallFunction(NsbFile* pDestScript, const char* FuncName)
{
    if (uint32_t FuncLine = pDestScript->GetFunctionLine(FuncName))
    {
        Returns.push({pScript, pScript->GetNextLineEntry()});
        pScript = pDestScript;
        pScript->SetSourceIter(FuncLine - 1);
        return true;
    }
    return false;
}

void NsbInterpreter::WriteTrace(std::ostream& Stream)
{
    std::stack<FuncReturn> Stack = Returns;
    Stack.push({pScript, pScript->GetNextLineEntry()});
    while (!Stack.empty())
    {
        Stream << Stack.top().pScript->GetName() << " at " << Stack.top().SourceLine << std::endl;
        Stack.pop();
    }
}

void NsbInterpreter::DumpState()
{
    std::ofstream Log("state-log.txt");
    WriteTrace(Log);
}

void NsbInterpreter::Crash()
{
    std::cout << "\n**STACK TRACE BEGIN**\n";
    WriteTrace(std::cout);
    std::cout << "**STACK TRACE END**\nRecovering...\n" << std::endl;

#ifdef DEBUG
    abort();
#else
    Recover();
#endif
}

void NsbInterpreter::Recover()
{
    while (Line* pLine = pScript->GetNextLine())
        if (pLine->Magic == MAGIC_CLEAR_PARAMS)
            break;
    pScript->SetSourceIter(pScript->GetNextLineEntry() - 2);
}

// Rename/eliminate pls?
void NsbInterpreter::NsbAssert(const char* fmt)
{
    std::cout << fmt << std::endl;
}

template<typename T, typename... A>
bool NsbInterpreter::NsbAssert(bool expr, const char* fmt, T value, A... args)
{
    if (expr)
        return false;

    NsbAssert(fmt, value, args...);
    Crash();
    return true;
}

template<typename T, typename... A>
void NsbInterpreter::NsbAssert(const char* fmt, T value, A... args)
{

    while (*fmt)
    {
        if (*fmt == '%')
        {
            if (*(fmt + 1) == '%')
                ++fmt;
            else
            {
                std::cout << value;
                NsbAssert(fmt + 1, args...);
                return;
            }
        }
        std::cout << *fmt++;
    }
}

bool NsbInterpreter::NsbAssert(bool expr, const char* fmt)
{
    if (expr)
        return false;

    NsbAssert(fmt);
    Crash();
    return true;
}
