#define ConfigPath "ux0:data/X1Vita/swapltrt"
int SetSwapStatus(int *status);
int GetSwapStatus();
int GetPidVid(int *vid, int *pid);
int GetBuff(int port, const char* buff);
int GetPortBuff(const char *buff);
#define CONTROLLER_COUNT 5