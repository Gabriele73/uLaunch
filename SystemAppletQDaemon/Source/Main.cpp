#include <db/db_Save.hpp>
#include <os/os_Titles.hpp>
#include <os/os_HomeMenu.hpp>
#include <os/os_Account.hpp>
#include <fs/fs_Stdio.hpp>
#include <am/am_Application.hpp>
#include <am/am_LibraryApplet.hpp>
#include <am/am_HomeMenu.hpp>
#include <am/am_QCommunications.hpp>
#include <util/util_Convert.hpp>
#include <ipc/ipc_IDaemonService.hpp>

extern "C"
{
    u32 __nx_applet_type = AppletType_SystemApplet;
    size_t __nx_heap_size = 0x3000000;//0x1000000;
}

void CommonSleepHandle()
{
    appletStartSleepSequence(true);
}

void HandleGeneralChannel()
{
    AppletStorage sams_st;
    auto rc = appletPopFromGeneralChannel(&sams_st);
    if(R_SUCCEEDED(rc))
    {
        os::SystemAppletMessage sams = {};
        rc = appletStorageRead(&sams_st, 0, &sams, sizeof(os::SystemAppletMessage));
        appletStorageClose(&sams_st);
        if(R_SUCCEEDED(rc))
        {
            if(sams.magic == os::SAMSMagic)
            {
                os::GeneralChannelMessage msg = (os::GeneralChannelMessage)sams.message;
                switch(msg)
                {
                    case os::GeneralChannelMessage::Shutdown:
                    {
                        appletStartShutdownSequence();
                        break;
                    }
                    case os::GeneralChannelMessage::Reboot:
                    {
                        appletStartRebootSequence();
                        break;
                    }
                    case os::GeneralChannelMessage::Sleep:
                    {
                        appletStartSleepSequence(true);
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }
    svcSleepThread(100000000L);
}

u8 *app_buf;
u128 selected_uid = 0;
u64 titlelaunch_flag = 0;
bool titlelaunch_system = false;

HosMutex latestqlock;
am::QMenuMessage latestqmenumsg = am::QMenuMessage::Invalid;

void HandleAppletMessage()
{
    u32 nmsg = 0;
    auto rc = appletGetMessage(&nmsg);
    if(R_SUCCEEDED(rc))
    {
        os::AppletMessage msg = (os::AppletMessage)nmsg;
        switch(msg)
        {
            case os::AppletMessage::HomeButton:
            {
                bool used_to_reopen_menu = false;
                if(am::ApplicationIsActive())
                {
                    if(am::ApplicationHasForeground())
                    {
                        bool flag;
                        appletUpdateLastForegroundCaptureImage();
                        appletGetLastForegroundCaptureImageEx(app_buf, 1280 * 720 * 4, &flag);
                        FILE *f = fopen(Q_BASE_SD_DIR "/temp-suspended.rgba", "wb");
                        if(f)
                        {
                            fwrite(app_buf, 1, 1280 * 720 * 4, f);
                            fclose(f);
                        }
                        am::HomeMenuSetForeground();
                        am::QDaemon_LaunchQMenu(am::QMenuStartMode::MenuApplicationSuspended);
                        used_to_reopen_menu = true;
                    }
                }
                if(am::LibraryAppletIsQMenu() && !used_to_reopen_menu)
                {
                    std::scoped_lock lock(latestqlock);
                    latestqmenumsg = am::QMenuMessage::HomeRequest;
                }
                break;
            }
            case os::AppletMessage::SdCardOut:
            {
                // SD card out? Cya!
                appletStartShutdownSequence();
                break;
            }
            case os::AppletMessage::PowerButton:
            {
                appletStartSleepSequence(true);
                break;
            }
            default:
                break;
        }
    }
    svcSleepThread(100000000L);
}

void HandleQMenuMessage()
{
    if(am::LibraryAppletIsQMenu())
    {
        am::QDaemonCommandReader reader;
        if(reader)
        {
            switch(reader.GetMessage())
            {
                case am::QDaemonMessage::SetSelectedUser:
                {
                    selected_uid = reader.Read<u128>();
                    reader.FinishRead();

                    break;
                }
                case am::QDaemonMessage::LaunchApplication:
                {
                    auto app_id = reader.Read<u64>();
                    auto is_system = reader.Read<bool>();
                    reader.FinishRead();

                    if(am::ApplicationIsActive())
                    {
                        am::QDaemonCommandResultWriter res(0xBABE1);
                        res.FinishWrite();
                    }
                    else if(selected_uid == 0)
                    {
                        am::QDaemonCommandResultWriter res(0xBABE2);
                        res.FinishWrite();
                    }
                    else if(titlelaunch_flag > 0)
                    {
                        am::QDaemonCommandResultWriter res(0xBABE3);
                        res.FinishWrite();
                    }
                    else
                    {
                        titlelaunch_flag = app_id;
                        titlelaunch_system = is_system;
                        am::QDaemonCommandResultWriter res(0);
                        res.FinishWrite();
                    }
                    break;
                }
                case am::QDaemonMessage::ResumeApplication:
                {
                    reader.FinishRead();

                    if(!am::ApplicationIsActive())
                    {
                        am::QDaemonCommandResultWriter res(0xBABE1);
                        res.FinishWrite();
                    }
                    else
                    {
                        am::ApplicationSetForeground();
                        am::QDaemonCommandResultWriter res(0);
                        res.FinishWrite();
                    }
                    break;
                }
                case am::QDaemonMessage::GetSuspendedApplicationId:
                {
                    reader.FinishRead();

                    if(am::ApplicationIsActive())
                    {
                        am::QDaemonCommandResultWriter res(0);
                        res.Write<u64>(am::ApplicationGetId());
                        res.FinishWrite();
                    }
                    else
                    {
                        am::QDaemonCommandResultWriter res(0xDEAD1);
                        res.FinishWrite();
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
}

namespace qdaemon
{
    void Initialize()
    {
        app_buf = new u8[1280 * 720 * 4]();
        fs::CreateDirectory(Q_BASE_SD_DIR);

        // Debug testing mode
        consoleInit(NULL);
        CONSOLE_FMT("Welcome to QDaemon's debug mode!")
        CONSOLE_FMT("")
        CONSOLE_FMT("(A) -> Dump system save data to sd:/<q>/save_dump")
        CONSOLE_FMT("(B) -> Delete everything in save data (except official HOME menu's content)")
        CONSOLE_FMT("(X) -> Reboot system")
        CONSOLE_FMT("(Y) -> Continue to QMenu (proceed launch)")
        CONSOLE_FMT("")

        while(true)
        {
            hidScanInput();
            auto k = hidKeysDown(CONTROLLER_P1_AUTO);
            if(k & KEY_A)
            {
                db::Mount();
                fs::CopyDirectory(Q_DB_MOUNT_NAME ":/", Q_BASE_SD_DIR "/save_dump");
                db::Unmount();
                CONSOLE_FMT(" - Dump done.")
            }
            else if(k & KEY_B)
            {
                db::Mount();
                fs::DeleteDirectory(Q_BASE_DB_DIR);
                fs::CreateDirectory(Q_BASE_DB_DIR);
                db::Commit();
                db::Unmount();
                CONSOLE_FMT(" - Cleanup done.")
            }
            else if(k & KEY_X)
            {
                CONSOLE_FMT(" - Rebooting...")
                svcSleepThread(200'000'000);
                appletStartRebootSequence();
            }
            else if(k & KEY_Y)
            {
                CONSOLE_FMT(" - Proceeding with launch...")
                svcSleepThread(500'000'000);
                break;
            }
            svcSleepThread(10'000'000);
        }

        consoleExit(NULL);

        svcSleepThread(100'000'000); // Wait for proper moment
    }

    void Exit()
    {
        delete[] app_buf;
    }

    void DaemonServiceMain(void *arg)
    {
        static auto server = WaitableManager(2);
        server.AddWaitable(new ServiceServer<ipc::IDaemonService>("daemon", 0x10));
        server.Process();
    }

    Result LaunchDaemonServiceThread()
    {
        Thread daemon;
        R_TRY(threadCreate(&daemon, &DaemonServiceMain, NULL, 0x4000, 0x2b, -2));
        R_TRY(threadStart(&daemon));
        return 0;
    }
}

int main()
{
    qdaemon::Initialize();
    qdaemon::LaunchDaemonServiceThread();

    am::QDaemon_LaunchQMenu(am::QMenuStartMode::StartupScreen);

    while(true)
    {
        HandleGeneralChannel();
        HandleAppletMessage();
        HandleQMenuMessage();

        if(titlelaunch_flag > 0)
        {
            if(!am::LibraryAppletIsActive())
            {
                auto rc = am::ApplicationStart(titlelaunch_flag, titlelaunch_system, selected_uid);
                if(R_FAILED(rc)) am::QDaemon_LaunchQMenu(am::QMenuStartMode::StartupScreen);
                titlelaunch_flag = 0;
            }
        }

        svcSleepThread(10'000'000);
    }

    qdaemon::Exit();

    return 0;
}