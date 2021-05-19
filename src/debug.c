#include <vitasdk.h>
#include <taihen.h>
#include <string.h>
#include <stdio.h>
#include <vita2d.h>

#define second * 1000000

extern int GetPidVid(int *vid, int *pid);
extern int GetBuff(const char* buff);

vita2d_pgf *pgf;

void drawBuff()
{   
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
    strcpy(outBuff, "\n"); //dunno if we need this
    vita2d_pgf_draw_text(pgf, currentX, 20, RGBA8(0,255,0,255), 1.0f, outBuff);

    int pid;
    int vid;
    GetPidVid(&pid, &vid);

    sprintf(outBuff, "Last connection attempt had PID: 0x%X VID: 0x%X\n", pid, vid);
    vita2d_pgf_draw_text(pgf, 0, 45, RGBA8(0,247,255,255), 1.0f,outBuff);
}

int main()
{
    sceShellUtilInitEvents(0);
    sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2);

    SceCtrlData buttons; 
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
    } while (((buttons.buttons & SCE_CTRL_START) && (buttons.buttons & SCE_CTRL_SELECT) & (buttons.buttons & SCE_CTRL_PSBUTTON)) == 0);

    vita2d_fini();
    vita2d_free_pgf(pgf);
    sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
    sceKernelExitProcess(0);
}
