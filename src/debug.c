#include <vitasdk.h>
#include <taihen.h>
#include <string.h>
#include <stdio.h>
#include <vita2d.h>
#include "common.h"

extern SceUID _vshKernelSearchModuleByName(const char *name, SceUInt64 *unk);

vita2d_pgf *pgf;

int moduleLoaded = 0;

void drawBuff()
{   
    if(!moduleLoaded)
    {
        vita2d_pgf_draw_text(pgf, (960/2) - 100, (544/2)-10, RGBA8(0,255,0,255), 1.0f, "Error X1Vita not found!");
        return;
    }
    else
    {
        vita2d_pgf_draw_text(pgf, 960 - 150, 20, RGBA8(0,255,0,255), 1.0f, "X1Vita Found!");
    }
    int swapStatus = GetSwapStatus();
    char buff[0x12];
    GetBuff(buff);

    char outBuff[0x400];
    memset(outBuff, 0, 0x400);

    int currentX = 0;
	for (int i = 0; i < 0x12; i++)
    {
		sprintf(outBuff, "%02X", buff[i]);
        vita2d_pgf_draw_text(pgf, currentX, 20, RGBA8(0,255,0,255), 1.0f, outBuff);
        currentX += 40;
    }
    vita2d_pgf_draw_text(pgf, currentX, 20, RGBA8(0,255,0,255), 1.0f, outBuff);

    int pid;
    int vid;
    GetPidVid(&pid, &vid);

    sprintf(outBuff, "Last connection attempt had PID: 0x%X VID: 0x%X\n", pid, vid);
    vita2d_pgf_draw_text(pgf, 0, 45, RGBA8(0,247,255,255), 1.0f,outBuff);
    if(swapStatus) vita2d_pgf_draw_text(pgf, 0, 65, RGBA8(0,247,255,255), 1.0f, "Triggers and bumpers are swapped");
    else vita2d_pgf_draw_text(pgf, 0, 65, RGBA8(0,247,255,255), 1.0f, "Triggers and bumpers are not swapped");
}

int main()
{
    sceShellUtilInitEvents(0);
    sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2);

    SceUInt64 searchBuff = 0;
    moduleLoaded = (_vshKernelSearchModuleByName("X1Vita", &searchBuff) >= 0);
    SceCtrlData buttons; 
    if(moduleLoaded)
    {
        for (int i = 0; i < 10; i++)
        {
            sceCtrlReadBufferPositive(0, &buttons, 1);
            if(buttons.buttons & SCE_CTRL_CROSS)
            {
                int toSet = 1;
                SetSwapStatus(&toSet);
                break;
            }
            if(buttons.buttons & SCE_CTRL_CIRCLE)
            {
                int toSet = 0;
                SetSwapStatus(&toSet);
                break;
            }
        }
    }

    vita2d_init();
    vita2d_set_clear_color(RGBA8(0x40, 0x40, 0x40, 0xFF));
    
    pgf = vita2d_load_default_pgf();

    do
    {
        sceCtrlPeekBufferPositive(0, &buttons, 1);
        vita2d_start_drawing();
        vita2d_clear_screen();
        drawBuff();
        vita2d_end_drawing();
        vita2d_swap_buffers();
    } while (((buttons.buttons & SCE_CTRL_START) && (buttons.buttons & SCE_CTRL_SELECT) && (buttons.buttons & SCE_CTRL_LTRIGGER) && (buttons.buttons & SCE_CTRL_RTRIGGER)) == 0);

    vita2d_fini();
    vita2d_free_pgf(pgf);
    sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
    sceKernelExitProcess(0);
}
