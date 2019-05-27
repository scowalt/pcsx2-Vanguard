// A basic test implementation of Netcore for IPC in Dolphin

#pragma warning(disable : 4564)
#include <string>

#include "Helpers.hpp"
#include "VanguardClient.h"
#include "VanguardClientInitializer.h"
#include "PCSX2MemoryDomain.h"

#include <msclr/marshal_cppstd.h>
#include "UnmanagedWrapper.h"
#include "wx/string.h"

typedef char s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef uintptr_t uptr;
typedef intptr_t sptr;

typedef unsigned int uint;

using namespace cli;
using namespace System;
using namespace Text;
using namespace RTCV;
using namespace NetCore;
using namespace CorruptCore;
using namespace Vanguard;
using namespace Runtime::InteropServices;
using namespace System::Threading;
using namespace Collections::Generic;
using namespace System::Reflection;
using namespace Diagnostics;

#using < system.dll >
#using < system.windows.forms.dll >

#define SRAM_SIZE 25165824
#define ARAM_SIZE 16777216
#define EXRAM_SIZE 67108864

static int CPU_STEP_Count = 0;
static void EmuThreadExecute(Action ^ callback);

// Define this in here as it's managed and weird stuff happens if it's in a header
public
ref class VanguardClient
{
public:
    static NetCoreReceiver ^ receiver;
    static VanguardConnector ^ connector;

    void OnMessageReceived(Object ^ sender, NetCoreEventArgs ^ e);
    void SpecUpdated(Object ^ sender, SpecUpdateEventArgs ^ e);
    void RegisterVanguardSpec();

    void StartClient();
    void RestartClient();
    void StopClient();

    bool LoadRom(String ^ filename);
    bool LoadState(std::string filename);
    bool SaveState(String ^ filename, bool wait);

    //String ^ GetConfigAsJson(VanguardSettingsWrapper ^ settings);
    //VanguardSettingsWrapper ^ GetConfigFromJson(String ^ json);

    String ^ emuDir =
        IO::Path::GetDirectoryName(Reflection::Assembly::GetExecutingAssembly()->Location);
    String ^ logPath = IO::Path::Combine(emuDir, "EMU_LOG.txt");

    array<String ^> ^ configPaths;

    volatile bool loading = false;
};

ref class ManagedGlobals
{
public:
    static VanguardClient ^ client = nullptr;
};

static void EmuThreadExecute(Action ^ callback)
{
    //IntPtr callbackPtr = Marshal::GetFunctionPointerForDelegate(callback);
    //std::function<void(void)> nativeCallback =
    //  static_cast<void(__stdcall *)(void)>(callbackPtr.ToPointer());

    //Core::RunAsCPUThread(nativeCallback);
}

static PartialSpec ^ getDefaultPartial() {
    PartialSpec ^ partial = gcnew PartialSpec("VanguardSpec");
    partial->Set(VSPEC::NAME, "PCSX2");
    partial->Set(VSPEC::SUPPORTS_RENDERING, false);
    partial->Set(VSPEC::SUPPORTS_CONFIG_MANAGEMENT, false);
    partial->Set(VSPEC::SUPPORTS_CONFIG_HANDOFF, false);
    partial->Set(VSPEC::SUPPORTS_KILLSWITCH, true);
    partial->Set(VSPEC::SUPPORTS_REALTIME, true);
    partial->Set(VSPEC::SUPPORTS_SAVESTATES, true);
    partial->Set(VSPEC::SUPPORTS_MIXED_STOCKPILE, true);
    partial->Set(VSPEC::CONFIG_PATHS, ManagedGlobals::client->configPaths);
    partial->Set(VSPEC::SYSTEM, String::Empty);
    partial->Set(VSPEC::GAMENAME, String::Empty);
    partial->Set(VSPEC::SYSTEMPREFIX, String::Empty);
    partial->Set(VSPEC::OPENROMFILENAME, String::Empty);
    partial->Set(VSPEC::OVERRIDE_DEFAULTMAXINTENSITY, 500000);
    partial->Set(VSPEC::SYNCSETTINGS, String::Empty);
    partial->Set(VSPEC::MEMORYDOMAINS_BLACKLISTEDDOMAINS, gcnew array<String ^>{});
    partial->Set(VSPEC::SYSTEM, String::Empty);

    return partial;
}

    void VanguardClient::SpecUpdated(Object ^ sender, SpecUpdateEventArgs ^ e)
{
    PartialSpec ^ partial = e->partialSpec;

    LocalNetCoreRouter::Route(NetcoreCommands::CORRUPTCORE,
                              NetcoreCommands::REMOTE_PUSHVANGUARDSPECUPDATE, partial, true);
    LocalNetCoreRouter::Route(NetcoreCommands::UI, NetcoreCommands::REMOTE_PUSHVANGUARDSPECUPDATE,
                              partial, true);
}

void VanguardClient::RegisterVanguardSpec()
{
    PartialSpec ^ emuSpecTemplate = gcnew PartialSpec("VanguardSpec");

    emuSpecTemplate->Insert(getDefaultPartial());

    AllSpec::VanguardSpec =
        gcnew FullSpec(emuSpecTemplate, true); // You have to feed a partial spec as a template

    // if (VanguardCore.attached)
    // RTCV.Vanguard.VanguardConnector.PushVanguardSpecRef(VanguardCore.VanguardSpec);

    LocalNetCoreRouter::Route(NetcoreCommands::CORRUPTCORE, NetcoreCommands::REMOTE_PUSHVANGUARDSPEC,
                              emuSpecTemplate, true);
    LocalNetCoreRouter::Route(NetcoreCommands::UI, NetcoreCommands::REMOTE_PUSHVANGUARDSPEC,
                              emuSpecTemplate, true);
    AllSpec::VanguardSpec->SpecUpdated +=
        gcnew EventHandler<SpecUpdateEventArgs ^>(this, &VanguardClient::SpecUpdated);
}

// Lifted from Bizhawk
static Reflection::Assembly ^ CurrentDomain_AssemblyResolve(Object ^ sender, ResolveEventArgs ^ args) {
    try {
        Trace::WriteLine("Entering AssemblyResolve\n" + args->Name + "\n" +
                         args->RequestingAssembly);
        String ^ requested = args->Name;
        System::Threading::Monitor::Enter(AppDomain::CurrentDomain);
        {
            array<Assembly ^> ^ asms = AppDomain::CurrentDomain->GetAssemblies();
            for (int i = 0; i < asms->Length; i++) {
                Assembly ^ a = asms[i];
                if (a->FullName == requested) {
                    return a;
                }
            }

            AssemblyName ^ n = gcnew AssemblyName(requested);
            // load missing assemblies by trying to find them in the dll directory
            String ^ dllname = n->Name + ".dll";
            String ^ directory = IO::Path::Combine(
                IO::Path::GetDirectoryName(Assembly::GetExecutingAssembly()->Location), "..", "RTCV");
            String ^ fname = IO::Path::Combine(directory, dllname);
            if (!IO::File::Exists(fname)) {
                Trace::WriteLine(fname + " doesn't exist");
                return nullptr;
            }

            // it is important that we use LoadFile here and not load from a byte array; otherwise
            // mixed (managed/unamanged) assemblies can't load
            Trace::WriteLine("Loading " + fname);
            return Reflection::Assembly::LoadFile(fname);
        }
    } catch (System::Exception ^ e) {
        Trace::WriteLine("Something went really wrong in AssemblyResolve. Send this to the devs\n" +
                         e);
        return nullptr;
    } finally {
        Monitor::Exit(AppDomain::CurrentDomain);
    }
}

// Create our VanguardClient
void VanguardClientInitializer::StartVanguardClient()
{
    System::Windows::Forms::Form ^ dummy = gcnew System::Windows::Forms::Form();
    IntPtr Handle = dummy->Handle;
    SyncObjectSingleton::SyncObject = dummy;

    SyncObjectSingleton::EmuInvokeDelegate =
        gcnew SyncObjectSingleton::ActionDelegate(&EmuThreadExecute);

    // Start everything
    ManagedGlobals::client = gcnew VanguardClient;

    //Todo
    ManagedGlobals::client->configPaths = gcnew array<String ^>{};

    ManagedGlobals::client->StartClient();
    ManagedGlobals::client->RegisterVanguardSpec();
    RTCV::CorruptCore::CorruptCore::StartEmuSide();
}

// Create our VanguardClient
void VanguardClientInitializer::Initialize()
{
    // This has to be in its own method where no other dlls are used so the JIT can compile it
    AppDomain::CurrentDomain->AssemblyResolve +=
        gcnew ResolveEventHandler(CurrentDomain_AssemblyResolve);

    StartVanguardClient();
}

void VanguardClient::StartClient()
{
    receiver = gcnew NetCoreReceiver();
    receiver->MessageReceived +=
        gcnew EventHandler<NetCoreEventArgs ^>(this, &VanguardClient::OnMessageReceived);

    RTCV::NetCore::Extensions::ConsoleHelper::CreateConsole(ManagedGlobals::client->logPath);
    RTCV::NetCore::Extensions::ConsoleHelper::HideConsole();
    // Can't use contains
    auto args = Environment::GetCommandLineArgs();
    for (int i = 0; i < args->Length; i++) {
        if (args[i] == "-CONSOLE") {
            RTCV::NetCore::Extensions::ConsoleHelper::ShowConsole();
        }
    }
    connector = gcnew VanguardConnector(receiver);
}

void VanguardClient::RestartClient()
{
    connector->Kill();
    connector = nullptr;
    StartClient();
}

void VanguardClient::StopClient()
{
    connector->Kill();
    connector = nullptr;
}

#pragma region MemoryDomains
static array<MemoryDomainProxy ^> ^ GetInterfaces() {
    array<MemoryDomainProxy ^> ^ interfaces = gcnew array<MemoryDomainProxy ^>(1);
    interfaces[0] = (gcnew MemoryDomainProxy(gcnew EERAM));

    return interfaces;
}

    static bool RefreshDomains()
{
    auto interfaces = GetInterfaces();
    AllSpec::VanguardSpec->Update(VSPEC::MEMORYDOMAINS_INTERFACES, interfaces, true, true);
    LocalNetCoreRouter::Route(NetcoreCommands::CORRUPTCORE,
                              NetcoreCommands::REMOTE_EVENT_DOMAINSUPDATED, true, true);
    return true;
}

#pragma endregion

//Todo
#pragma region Settings
/*
	String ^ VanguardClient::GetConfigAsJson(VanguardSettingsWrapper ^ settings)
{
    return JsonHelper::Serialize(settings);
}

VanguardSettingsWrapper ^ VanguardClient::GetConfigFromJson(String ^ str)
{
    return JsonHelper::Deserialize<VanguardSettingsWrapper ^>(str);
}
*/
#pragma endregion
static void STEP_CORRUPT() // errors trapped by CPU_STEP
{
    StepActions::Execute();
    CPU_STEP_Count++;
    bool autoCorrupt = RTCV::CorruptCore::CorruptCore::AutoCorrupt;
    long errorDelay = RTCV::CorruptCore::CorruptCore::ErrorDelay;
    if (autoCorrupt && CPU_STEP_Count >= errorDelay) {
        CPU_STEP_Count = 0;
        array<String ^> ^ domains = AllSpec::UISpec->Get<array<String ^> ^>("SELECTEDDOMAINS");

        BlastLayer ^ bl = RTCV::CorruptCore::CorruptCore::GenerateBlastLayer(domains, -1);
        if (bl != nullptr)
            bl->Apply(false, true);
    }
}

#pragma region Hooks
void VanguardClientUnmanaged::CORE_STEP()
{
    // Any step hook for corruption
    STEP_CORRUPT();
}

// This is on the main thread not the emu thread
void VanguardClientUnmanaged::LOAD_GAME_START(std::string romPath)
{
    StepActions::ClearStepBlastUnits();
    CPU_STEP_Count = 0;

    String ^ gameName = Helpers::utf8StringToSystemString(romPath);
    AllSpec::VanguardSpec->Update(VSPEC::OPENROMFILENAME, gameName, true, true);
}

void VanguardClientUnmanaged::LOAD_GAME_DONE()
{
    PartialSpec ^ gameDone = gcnew PartialSpec("VanguardSpec");

    try {
        gameDone->Set(VSPEC::SYSTEM, "PCSX2");
        gameDone->Set(VSPEC::SYSTEMPREFIX, "PCSX2");
        gameDone->Set(VSPEC::SYSTEMCORE, "PS2");
        gameDone->Set(VSPEC::SYNCSETTINGS, "");
        gameDone->Set(VSPEC::MEMORYDOMAINS_BLACKLISTEDDOMAINS, gcnew array<String ^>{});
        gameDone->Set(VSPEC::MEMORYDOMAINS_INTERFACES, GetInterfaces());
        gameDone->Set(VSPEC::CORE_DISKBASED, true);

        String ^ oldGame = AllSpec::VanguardSpec->Get<String ^>(VSPEC::GAMENAME);

        String ^ gameName = Helpers::utf8StringToSystemString(UnmanagedWrapper::VANGUARD_GameName);

        char replaceChar = L'-';
        gameDone->Set(VSPEC::GAMENAME, CorruptCore_Extensions::MakeSafeFilename(gameName, replaceChar));


        //Todo
        //String^ syncsettings = ManagedGlobals::client->GetConfigAsJson(VanguardSettings::GetVanguardSettingsFromDolphin());
        //gameDone->Set(VSPEC::SYNCSETTINGS, syncsettings);

        AllSpec::VanguardSpec->Update(gameDone, true, false);

        // This is local. If the domains changed it propgates over netcore
        LocalNetCoreRouter::Route(NetcoreCommands::CORRUPTCORE,
                                  NetcoreCommands::REMOTE_EVENT_DOMAINSUPDATED, true, true);
        if (oldGame != gameName) {
            LocalNetCoreRouter::Route(NetcoreCommands::UI,
                                      NetcoreCommands::RESET_GAME_PROTECTION_IF_RUNNING, true);
        }
    } catch (System::Exception ^ e) {
        Trace::WriteLine(e->ToString());
    }
    ManagedGlobals::client->loading = false;
}

void VanguardClientUnmanaged::GAME_CLOSED()
{
    AllSpec::VanguardSpec->Update(VSPEC::OPENROMFILENAME, "", true, true);
}
#pragma endregion

/*ENUMS FOR THE SWITCH STATEMENT*/
enum COMMANDS {
    SAVESAVESTATE,
    LOADSAVESTATE,
    REMOTE_LOADROM,
    REMOTE_CLOSEGAME,
    REMOTE_DOMAIN_GETDOMAINS,
    REMOTE_KEY_SETSYNCSETTINGS,
    REMOTE_KEY_SETSYSTEMCORE,
    REMOTE_EVENT_EMU_MAINFORM_CLOSE,
    REMOTE_EVENT_EMUSTARTED,
    REMOTE_ISNORMALADVANCE,
    REMOTE_EVENT_CLOSEEMULATOR,
    REMOTE_ALLSPECSSENT,
    UNKNOWN
};

inline COMMANDS CheckCommand(String ^ inString)
{
    if (inString == "LOADSAVESTATE")
        return LOADSAVESTATE;
    if (inString == "SAVESAVESTATE")
        return SAVESAVESTATE;
    if (inString == "REMOTE_LOADROM")
        return REMOTE_LOADROM;
    if (inString == "REMOTE_CLOSEGAME")
        return REMOTE_CLOSEGAME;
    if (inString == "REMOTE_ALLSPECSSENT")
        return REMOTE_ALLSPECSSENT;
    if (inString == "REMOTE_DOMAIN_GETDOMAINS")
        return REMOTE_DOMAIN_GETDOMAINS;
    if (inString == "REMOTE_KEY_SETSYSTEMCORE")
        return REMOTE_KEY_SETSYSTEMCORE;
    if (inString == "REMOTE_EVENT_EMU_MAINFORM_CLOSE")
        return REMOTE_EVENT_EMU_MAINFORM_CLOSE;
    if (inString == "REMOTE_EVENT_EMUSTARTED")
        return REMOTE_EVENT_EMUSTARTED;
    if (inString == "REMOTE_ISNORMALADVANCE")
        return REMOTE_ISNORMALADVANCE;
    if (inString == "REMOTE_EVENT_CLOSEEMULATOR")
        return REMOTE_EVENT_CLOSEEMULATOR;
    if (inString == "REMOTE_ALLSPECSSENT")
        return REMOTE_ALLSPECSSENT;
    return UNKNOWN;
}

/* IMPLEMENT YOUR COMMANDS HERE */

//Todo
bool VanguardClient::LoadRom(String ^ filename)
{
    String ^ currentOpenRom = "";
    if (AllSpec::VanguardSpec->Get<String ^>(VSPEC::OPENROMFILENAME) != "")
        currentOpenRom = AllSpec::VanguardSpec->Get<String ^>(VSPEC::OPENROMFILENAME);

    // Game is not running
    if (currentOpenRom != filename) {
        // Clear out any old settings
        //Config::ClearCurrentVanguardLayer();

        const std::string &path = Helpers::systemStringToUtf8String(filename);
        ManagedGlobals::client->loading = true;

        //  SetState(Core::State::Paused);
        // VanguardClientInitializer::win->StartGame(path);
        // We have to do it this way to prevent deadlock due to synced calls. It sucks but it's required
        // at the moment
        while (ManagedGlobals::client->loading) {
            System::Threading::Thread::Sleep(20);
            System::Windows::Forms::Application::DoEvents();
        }

        System::Threading::Thread::Sleep(100); // Give the emu thread a chance to recover
    }
    return true;
}
//Todo
bool VanguardClient::LoadState(std::string filename)
{
    StepActions::ClearStepBlastUnits();
    wxString mystring(filename);
    UnmanagedWrapper::VANGUARD_LOADSTATE(mystring);
    // State::LoadAs(filename);
    return true;
}
//Todo
bool VanguardClient::SaveState(String ^ filename, bool wait)
{
    std::string converted_filename = Helpers::systemStringToUtf8String(filename);

    wxString mystring(converted_filename);
    UnmanagedWrapper::VANGUARD_SAVESTATE(mystring);
	return true;

    // if (Core::IsRunningAndStarted())
    //  {
    //   State::SaveAs(converted_filename, wait);
    //    return true;
    //  }
    return false;
}

// No fun anonymous classes with closure here
#pragma region Delegates
void StopGame() //Todo
{
    // Core::Stop();
}

void Quit() 
{
    UnmanagedWrapper::VANGUARD_EXIT();
}

void AllSpecsSent()
{
    AllSpec::VanguardSpec->Update(VSPEC::EMUDIR, ManagedGlobals::client->emuDir, true, true);
    //VanguardClientInitializer::win->Show();
}
#pragma endregion

/* THIS IS WHERE YOU HANDLE ANY RECEIVED MESSAGES */
void VanguardClient::OnMessageReceived(Object ^ sender, NetCoreEventArgs ^ e)
{
    NetCoreMessage ^ message = e->message;
    NetCoreSimpleMessage ^ simpleMessage;
    NetCoreAdvancedMessage ^ advancedMessage;

    if (Helpers::is<NetCoreSimpleMessage ^>(message))
        simpleMessage = static_cast<NetCoreSimpleMessage ^>(message);
    if (Helpers::is<NetCoreAdvancedMessage ^>(message))
        advancedMessage = static_cast<NetCoreAdvancedMessage ^>(message);

    switch (CheckCommand(message->Type)) {
        case REMOTE_ALLSPECSSENT: {
            SyncObjectSingleton::GenericDelegate ^ g =
                gcnew SyncObjectSingleton::GenericDelegate(&AllSpecsSent);
            SyncObjectSingleton::FormExecute(g);
        } break;

        case LOADSAVESTATE: {
            NetCoreAdvancedMessage ^ advancedMessage = (NetCoreAdvancedMessage ^) e->message;
            array<Object ^> ^ cmd = static_cast<array<Object ^> ^>(advancedMessage->objectValue);
            String ^ path = static_cast<String ^>(cmd[0]);
            std::string converted_path = Helpers::systemStringToUtf8String(path);
            StashKeySavestateLocation ^ location = safe_cast<StashKeySavestateLocation ^>(cmd[1]);

            // Clear out any old settings
            // Config::ClearCurrentVanguardLayer();

            // Load up the sync settings //Todo
            String ^ settingStr = AllSpec::VanguardSpec->Get<String ^>(VSPEC::SYNCSETTINGS);
            if (settingStr != nullptr) {
                //  VanguardSettingsWrapper^ settings = GetConfigFromJson(settingStr);
                //  if (settings != nullptr)
                //   AddLayer(ConfigLoaders::GenerateVanguardConfigLoader(
                //      &VanguardSettings::GetVanguardSettingFromVanguardSettingsWrapper(settings)));
            }
            e->setReturnValue(ManagedGlobals::client->LoadState(converted_path));
        } break;

        case SAVESAVESTATE: {
            String ^ Key = (String ^)(advancedMessage->objectValue);
            // Build the shortname
            String ^ quickSlotName = Key + ".timejump";

            // Get the prefix for the state

            String ^ gameName = Helpers::utf8StringToSystemString(UnmanagedWrapper::VANGUARD_GameName);

            char replaceChar = L'-';
            String ^ prefix = CorruptCore_Extensions::MakeSafeFilename(gameName, replaceChar);
            prefix = prefix->Substring(prefix->LastIndexOf('\\') + 1);

            String ^ path = nullptr;
            // Build up our path
            path = RTCV::CorruptCore::CorruptCore::workingDir + IO::Path::DirectorySeparatorChar +
                   "SESSION" + IO::Path::DirectorySeparatorChar + prefix + "." + quickSlotName + ".State";

            // If the path doesn't exist, make it
            IO::FileInfo ^ file = gcnew IO::FileInfo(path);
            if (file->Directory != nullptr && file->Directory->Exists == false)
                file->Directory->Create();

            // if (ManagedGlobals::client->SaveState(path, false) && Core::IsRunningAndStarted())
            e->setReturnValue(path);
        } break;

        case REMOTE_LOADROM: {
            String ^ filename = (String ^) advancedMessage->objectValue;
            ManagedGlobals::client->LoadRom(filename);
        } break;

        case REMOTE_CLOSEGAME: {
            SyncObjectSingleton::GenericDelegate ^ g =
                gcnew SyncObjectSingleton::GenericDelegate(&StopGame);
            SyncObjectSingleton::FormExecute(g);
        } break;

        case REMOTE_DOMAIN_GETDOMAINS: {
            RefreshDomains();
        } break;

        case REMOTE_KEY_SETSYNCSETTINGS: {
            String ^ settings = (String ^)(advancedMessage->objectValue);
            AllSpec::VanguardSpec->Set(VSPEC::SYNCSETTINGS, settings);
        } break;

        case REMOTE_KEY_SETSYSTEMCORE: {
            // Do nothing
        } break;

        case REMOTE_EVENT_EMUSTARTED: {
        } break;

        case REMOTE_ISNORMALADVANCE: {
            // Todo - Dig out fast forward?
            e->setReturnValue(true);
        } break;

        case REMOTE_EVENT_EMU_MAINFORM_CLOSE:
        case REMOTE_EVENT_CLOSEEMULATOR: {
            SyncObjectSingleton::GenericDelegate ^ g =
                gcnew SyncObjectSingleton::GenericDelegate(&StopGame);
            SyncObjectSingleton::FormExecute(g);
            ManagedGlobals::client->StopClient();
            g = gcnew SyncObjectSingleton::GenericDelegate(&Quit);
            SyncObjectSingleton::FormExecute(g);
        } break;

        default:
            break;
    }
}
