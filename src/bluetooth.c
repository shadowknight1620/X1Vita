#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/suspend.h>
#include <psp2kern/bt.h>
#include <psp2kern/ctrl.h>
#include <psp2/touch.h>
#include <string.h>
#include <psp2/motion.h>
#include <taihen.h>

#define MICROSOFT_VID 0x45E
#define XBOX_CONTROLLER_PID 0x2FD

#define CONTROLLER_ANALOG_THRESHOLD 4
#define DEADZONE_ANALOG(a) ((a) > 127 - CONTROLLER_ANALOG_THRESHOLD && (a) < 127 + CONTROLLER_ANALOG_THRESHOLD ? 127 : (a))
#define abs(x) (((x) < 0) ? -(x) : (x))

static SceUID bt_mempool_uid = -1;
static SceUID bt_thread_uid = -1;
static SceUID bt_cb_uid = -1;
static int bt_thread_run = 1;

static int controller_connected = 0;
static unsigned int controller_mac0 = 0;
static unsigned int controller_mac1 = 0;

static int lastPID = -1, lastVID = -1;

static char current_recieved_input[0x12];

#define DECL_FUNC_HOOK(name, ...) \
	static tai_hook_ref_t name##_ref; \
	static SceUID name##_hook_uid = -1; \
	static int name##_hook_func(__VA_ARGS__)

static inline void controller_input_reset(void)
{
	memset(&current_recieved_input, 0, sizeof(current_recieved_input));
}

static int is_controller(const unsigned short vid_pid[2])
{
	ksceDebugPrintf("Got VID 0x%x        got PID 0x%x         ", vid_pid[0], vid_pid[1]);
	lastVID = vid_pid[0];
	lastPID = vid_pid[1];
	//return (vid_pid[0] == controller_VID) &&
	//	((vid_pid[1] == controller_PID) || (vid_pid[1] == controller_2_PID));
	return (vid_pid[0] == MICROSOFT_VID) &&
		(vid_pid[1] == XBOX_CONTROLLER_PID);
}

static inline void *mempool_alloc(unsigned int size)
{
	return ksceKernelAllocHeapMemory(bt_mempool_uid, size);
}

static inline void mempool_free(void *ptr)
{
	ksceKernelFreeHeapMemory(bt_mempool_uid, ptr);
}

//Get PID and VID of last request
static int GetPidVid(int *vid, int *pid)
{
	int ret;
	ret = ksceKernelMemcpyKernelToUser((uintptr_t)vid, &lastVID, sizeof(lastVID));
	if (ret < 0) return ret;
	return ksceKernelMemcpyKernelToUser((uintptr_t)pid, &lastPID, sizeof(lastPID));
}

//Get current data recieved from the connected bluetooth device
static int GetBuff(const char* buff)
{
	//We use memcpy instead of strncpy so it copies the whole thing if not values will be cut off.
	return ksceKernelMemcpyKernelToUser((uintptr_t)buff, current_recieved_input, 0x12);
} 

static int controller_send_report(unsigned int mac0, unsigned int mac1, uint8_t flags, uint8_t report, size_t len, const void *data)
{
	SceBtHidRequest *req;
	unsigned char *buf;

	req = mempool_alloc(sizeof(*req));
	if (!req) {
		return -1;
	}

	if ((buf = mempool_alloc((len + 1) * sizeof(*buf))) == NULL) {
		return -1;
	}

	buf[0] = report;
	memcpy(buf + 1, data, len);

	memset(req, 0, sizeof(*req));
	req->type = 1; // 0xA2 -> type = 1
	req->buffer = buf;
	req->length = len + 1;
	req->next = req;

	ksceBtHidTransfer(mac0, mac1, req);

	mempool_free(buf);
	mempool_free(req);

	return 0;
}

static int controller_send_0x11_report(unsigned int mac0, unsigned int mac1)
{
	unsigned char data[] = {
		0x80,
		0x0F,
		0x00,
		0x00,
		0x00,
		0x00, 
		0x00, 
		0x00, 
		0x00, 
		0x00, 
		0x00,
		0x00,
	};

	if (controller_send_report(mac0, mac1, 0, 0x11, sizeof(data), data)) {
		return -1;
	}

	return 0;
}

static void reset_input_emulation()
{
	ksceCtrlSetButtonEmulation(0, 0, 0, 0, 32);
	ksceCtrlSetAnalogEmulation(0, 0, 0x80, 0x80, 0x80, 0x80,
		0x80, 0x80, 0x80, 0x80, 0);
}

static void enqueue_read_request(unsigned int mac0, unsigned int mac1, SceBtHidRequest *request, unsigned char *buffer, unsigned int length)
{
	memset(request, 0, sizeof(*request));
	memset(buffer, 0, length);

	request->type = 0;
	request->buffer = buffer;
	request->length = length;
	request->next = request;

	ksceBtHidTransfer(mac0, mac1, request);
}
#pragma region Hooks
DECL_FUNC_HOOK(SceBt_sub_22999C8, void *dev_base_ptr, int r1)
{
	unsigned int flags = *(unsigned int *)(r1 + 4);

	if (dev_base_ptr && !(flags & 2)) {
		const void *dev_info = *(const void **)(dev_base_ptr + 0x14A4);
		const unsigned short *vid_pid = (const unsigned short *)(dev_info + 0x28);

		if (is_controller(vid_pid)) {
			unsigned int *v8_ptr = (unsigned int *)(*(unsigned int *)dev_base_ptr + 8);

			/*
			 * We need to enable the following bits in order to make the Vita
			 * accept the new connection, otherwise it will refuse it.
			 */
			*v8_ptr |= 0x11000;
		}
	}

	return TAI_CONTINUE(int, SceBt_sub_22999C8_ref, dev_base_ptr, r1);
}
#pragma endregion Hooks

static int bt_cb_func(int notifyId, int notifyCount, int notifyArg, void *common)
{

	static SceBtHidRequest hid_request;
	static unsigned char recv_buff[0x100];
	while (1) {
		int ret;
		SceBtEvent hid_event;

		memset(&hid_event, 0, sizeof(hid_event));

		do {
			ret = ksceBtReadEvent(&hid_event, 1);
		} while (ret == SCE_BT_ERROR_CB_OVERFLOW);

		if (ret <= 0) {
			break;
		}
		/*
		 * If we get an event with a MAC, and the MAC is different
		 * from the connected controller, skip the event.
		 */
		if (controller_connected) {
			if (hid_event.mac0 != controller_mac0 || hid_event.mac1 != controller_mac1)
				continue;
		}

		switch (hid_event.id) {
		case 0x15: //Bluetooth turned on or off
		{
			controller_connected = 0;
			reset_input_emulation();
		}
		case 0x01: { /* Inquiry result event */
			unsigned short vid_pid[2];
			ksceBtGetVidPid(hid_event.mac0, hid_event.mac1, vid_pid);

			if (is_controller(vid_pid)) {
				ksceBtStopInquiry();
				controller_mac0 = hid_event.mac0;
				controller_mac1 = hid_event.mac1;
			}
			break;
		}

		case 0x02: /* Inquiry stop event */
			if (!controller_connected) {
				if (controller_mac0 || controller_mac1)
					ksceBtStartConnect(controller_mac0, controller_mac1);
			}
			break;

		case 0x04: /* Link key request? event */
			ksceBtReplyUserConfirmation(hid_event.mac0, hid_event.mac1, 1);
			break;

		case 0x05: { /* Connection accepted event */
			unsigned short vid_pid[2];
			ksceBtGetVidPid(hid_event.mac0, hid_event.mac1, vid_pid);

			if (is_controller(vid_pid)) {

				controller_input_reset();
				controller_mac0 = hid_event.mac0;
				controller_mac1 = hid_event.mac1;
				controller_connected = 1;
				controller_send_0x11_report(hid_event.mac0, hid_event.mac1);
			}
			break;
		}


		case 0x06: /* Device disconnect event*/
			controller_connected = 0;
			reset_input_emulation();
			break;

		case 0x08: /* Connection requested event */
			/*
			 * Do nothing since we will get a 0x05 event afterwards.
			 */
			break;

		case 0x09: /* Connection request without being paired? event */
			/*
			 * The Vita needs to have a pairing with the controller,
			 * otherwise it won't connect.
			 */
			break;

		case 0x0A:
			memcpy(current_recieved_input, recv_buff, 0x11);
			enqueue_read_request(hid_event.mac0, hid_event.mac1, &hid_request, recv_buff, sizeof(recv_buff));
			break;

		case 0x0B:
			enqueue_read_request(hid_event.mac0, hid_event.mac1, &hid_request, recv_buff, sizeof(recv_buff));
			break;
		}
	}

	return 0;
}

static int controllervita_bt_thread(SceSize args, void *argp)
{
	bt_cb_uid = ksceKernelCreateCallback("controllervita_bt_callback", 0, bt_cb_func, NULL);

	ksceBtRegisterCallback(bt_cb_uid, 0, 0xFFFFFFFF, 0xFFFFFFFF);


	while (bt_thread_run) {
		ksceKernelDelayThreadCB(200 * 1000);
	}

	if (controller_connected) {
		ksceBtStartDisconnect(controller_mac0, controller_mac1);
		reset_input_emulation();
	}

	ksceBtUnregisterCallback(bt_cb_uid);

	ksceKernelDeleteCallback(bt_cb_uid);

	return 0;
}


static void patch_ctrl_data(SceCtrlData *pad_data, int triggers)
{
	if(!controller_connected) return;
	int leftX = 0x80;
	int leftY = 0x80;
	int rightX = 0x80;
	int rightY = 0x80;
	int joyStickMoved = 0;
	unsigned int buttons = 0;
	//Xbox button
	if(current_recieved_input[0] & 0x02) 
	{
		if(current_recieved_input[1] & 0x01)
		{
			buttons |= SCE_CTRL_PSBUTTON;
			ksceCtrlSetButtonEmulation(0, 0, 0, SCE_CTRL_PSBUTTON, 8);
		}
	} //Ant other button call
	else if(current_recieved_input[0] & 0x1)
	{
		//DPad
		switch (current_recieved_input[13])
		{
			case 0x1:
				buttons |= SCE_CTRL_UP;
				break;
			case 0x2:
				buttons |= SCE_CTRL_UP;
				buttons |= SCE_CTRL_RIGHT;
				break;
			case 0x3:
				buttons |= SCE_CTRL_RIGHT;
				break;
			case 0x4:
				buttons |= SCE_CTRL_RIGHT;
				buttons |= SCE_CTRL_DOWN;
				break;
			case 0x5:
				buttons |= SCE_CTRL_DOWN;
				break;
			case 0x6:
				buttons |= SCE_CTRL_DOWN;
				buttons |= SCE_CTRL_LEFT;
				break;
			case 0x7:
				buttons |= SCE_CTRL_LEFT;
				break;
			case 0x8:
				buttons |= SCE_CTRL_LEFT;
				buttons |= SCE_CTRL_UP;
				break;
		}

		//RB LB and ABXY. For some reason the buttons aren't or'ed together when pushed separetly which is why we need to do it like this.
		switch (current_recieved_input[14])
		{
			case 0x1:
				buttons |= SCE_CTRL_CROSS;
				break;
			case 0x2:
				buttons |= SCE_CTRL_CIRCLE;
				break;
			case 0x8:
				buttons |= SCE_CTRL_SQUARE;
				break;
			case 0x10:
				buttons |= SCE_CTRL_TRIANGLE;
				break;
			case 0x80:
				//RB
				if(triggers) buttons |= SCE_CTRL_R1;
				else buttons |= SCE_CTRL_RTRIGGER;
				break;
			case 0x40:
				//LB
				if(triggers) buttons |= SCE_CTRL_L1;
				else buttons |= SCE_CTRL_LTRIGGER;
				break;
			default:
				{
					if(current_recieved_input[14] & 0x1) buttons |= SCE_CTRL_CROSS;
					if(current_recieved_input[14] & 0x2) buttons |= SCE_CTRL_CIRCLE;
					if(current_recieved_input[14] & 0x8) buttons |= SCE_CTRL_SQUARE;
					if(current_recieved_input[14] & 0x10) buttons |= SCE_CTRL_TRIANGLE;		
					if(current_recieved_input[14] & 0x80)
					{
						if(triggers) buttons |= SCE_CTRL_R1;
						else buttons |= SCE_CTRL_RTRIGGER;
					}
					if(current_recieved_input[14] & 0x40) 
					{
						if(triggers) buttons |= SCE_CTRL_L1;
						else buttons |= SCE_CTRL_LTRIGGER;
					}
					break;
				}
		}

		//Start
		//if(current_recieved_input[15] == 0x8) buttons |= SCE_CTRL_START;
		//Select
		if(current_recieved_input[16] == 0x1) buttons |= SCE_CTRL_SELECT;

		//R3 L3 Start and Xbox button on some controllers
		switch (current_recieved_input[15])
		{
			case 0x10:
			{
				buttons |= SCE_CTRL_PSBUTTON;
				ksceCtrlSetButtonEmulation(0, 0, 0, SCE_CTRL_PSBUTTON, 8);
				break;
			}
			case 0x8:
				buttons |= SCE_CTRL_START;
				break;
			case 0x40:
				buttons |= SCE_CTRL_R3;
				break;
			case 0x20:
				buttons |= SCE_CTRL_L3;

			default:
				if(current_recieved_input[15] & 0x8) 
				{
					buttons |= SCE_CTRL_START;
				}
				if(current_recieved_input[15] & 0x40) 
				{
					buttons |= SCE_CTRL_R3;
				}
				if(current_recieved_input[15] & 0x20) 
				{
					buttons |= SCE_CTRL_L3;
				}
				if(current_recieved_input[15] & 0x10)
				{
					buttons |= SCE_CTRL_PSBUTTON;
					ksceCtrlSetButtonEmulation(0, 0, 0, SCE_CTRL_PSBUTTON, 8);
				}
				break;
		}

		//Joysticks
		//Left Joystick X
		leftX = DEADZONE_ANALOG((current_recieved_input[2]));
		//Left Joystick Y
		leftY = DEADZONE_ANALOG((current_recieved_input[4]));

		//Right Joystick X
		rightX = DEADZONE_ANALOG((current_recieved_input[6]));
		//Right Joystick Y
		rightY = DEADZONE_ANALOG((current_recieved_input[8]));

		if(leftX != 128 && leftY != 128)
			joyStickMoved = 1;

	}

	//LT RT
	if(current_recieved_input[9] > 10)
	{
		if(triggers) buttons |= SCE_CTRL_LTRIGGER;
		else buttons |= SCE_CTRL_L1;
	}
	if(current_recieved_input[11] > 10)
	{
		if(triggers) buttons |= SCE_CTRL_RTRIGGER;
		else buttons |= SCE_CTRL_R1;
	}
	int lt = ((current_recieved_input[9] + (255 * current_recieved_input[10])) / 4);
	int rt = ((current_recieved_input[11] + (255 * current_recieved_input[12])) / 4);
	pad_data->lt = lt;
	pad_data->rt = rt;
	//Joysticks
	if(joyStickMoved)
	{	
		pad_data->ry = rightY;
		pad_data->rx = rightX;
		pad_data->lx = leftX;
		pad_data->ly = leftY;
	}
	
	pad_data->buttons |= buttons;
	if(buttons != 0 || joyStickMoved) ksceKernelPowerTick(0);
}

static void patch_ctrl_data_all_user(int port, SceCtrlData *pad_data, int count, int triggers)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		SceCtrlData k_data;

		ksceKernelMemcpyUserToKernel(&k_data, (uintptr_t)pad_data, sizeof(k_data));
		patch_ctrl_data(&k_data, triggers);
		ksceKernelMemcpyKernelToUser((uintptr_t)pad_data, &k_data, sizeof(k_data));

		pad_data++;
	}
}

static void patch_ctrl_data_all_kernel( int port, SceCtrlData *pad_data, int count, int triggers)
{
	unsigned int i;

	for (i = 0; i < count; i++, pad_data++)
		patch_ctrl_data(pad_data, triggers);
}

#define DECL_FUNC_HOOK_PATCH_CTRL(type, name, triggers) \
	DECL_FUNC_HOOK(SceCtrl_##name, int port, SceCtrlData *pad_data, int count) \
	{ \
		int ret = TAI_CONTINUE(int, SceCtrl_ ##name##_ref, port, pad_data, count, triggers); \
		if (ret >= 0 && controller_connected) \
			patch_ctrl_data_all_##type(port, pad_data, count, triggers); \
		return ret; \
	}

	
DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlPeekBufferPositive, 0)
DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlReadBufferPositive, 0)
DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlPeekBufferNegative, 0)
DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlReadBufferNegative, 0)
DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlPeekBufferPositiveExt, 0)
DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlReadBufferPositiveExt, 0)

DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlPeekBufferPositive2, 1)
DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlReadBufferPositive2, 1)
DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlPeekBufferNegative2, 1)
DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlReadBufferNegative2, 1)
DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlPeekBufferPositiveExt2, 1)
DECL_FUNC_HOOK_PATCH_CTRL(kernel, ksceCtrlReadBufferPositiveExt2, 1)

DECL_FUNC_HOOK_PATCH_CTRL(user, sceCtrlPeekBufferNegative2, 1)
DECL_FUNC_HOOK_PATCH_CTRL(user, sceCtrlPeekBufferPositive2, 1)
DECL_FUNC_HOOK_PATCH_CTRL(user, sceCtrlReadBufferNegative2, 1)
DECL_FUNC_HOOK_PATCH_CTRL(user, sceCtrlReadBufferPositive2, 1)

DECL_FUNC_HOOK_PATCH_CTRL(user, sceCtrlPeekBufferPositiveExt2, 1)
DECL_FUNC_HOOK_PATCH_CTRL(user, sceCtrlReadBufferPositiveExt2, 1)

DECL_FUNC_HOOK_PATCH_CTRL(user, sceCtrlPeekBufferPositiveExt, 0)
DECL_FUNC_HOOK_PATCH_CTRL(user, sceCtrlReadBufferPositiveExt, 0)

void _start() __attribute__ ((weak, alias ("module_start")));
#pragma region Definitions
#define BIND_FUNC_OFFSET_HOOK(name, pid, modid, segidx, offset, thumb) \
	name##_hook_uid = taiHookFunctionOffsetForKernel((pid), \
		&name##_ref, (modid), (segidx), (offset), thumb, name##_hook_func)

#define BIND_FUNC_EXPORT_HOOK(name, pid, module, lib_nid, func_nid) \
	name##_hook_uid = taiHookFunctionExportForKernel((pid), \
		&name##_ref, (module), (lib_nid), (func_nid), name##_hook_func)
#pragma endregion Definitions
int module_start(SceSize argc, const void *args)
{
	int ret;
	tai_module_info_t SceBt_modinfo;

	SceBt_modinfo.size = sizeof(SceBt_modinfo);
	ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &SceBt_modinfo);
	if (ret < 0) {
		goto error_find_scebt;
	}

	tai_module_info_t SceCtrl_modinfo;
	SceCtrl_modinfo.size = sizeof(SceCtrl_modinfo);

	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceCtrl", &SceCtrl_modinfo) < 0) {
		ksceDebugPrintf("Error finding SceBt module\n");
		return SCE_KERNEL_START_FAILED;
	}


	BIND_FUNC_EXPORT_HOOK(SceCtrl_ksceCtrlPeekBufferPositive, KERNEL_PID, "SceCtrl", TAI_ANY_LIBRARY, 0xEA1D3A34);
    BIND_FUNC_EXPORT_HOOK(SceCtrl_ksceCtrlReadBufferPositive, KERNEL_PID, "SceCtrl", TAI_ANY_LIBRARY, 0x9B96A1AA);
    BIND_FUNC_EXPORT_HOOK(SceCtrl_ksceCtrlPeekBufferNegative, KERNEL_PID, "SceCtrl", TAI_ANY_LIBRARY, 0x19895843);
    BIND_FUNC_EXPORT_HOOK(SceCtrl_ksceCtrlReadBufferNegative, KERNEL_PID, "SceCtrl", TAI_ANY_LIBRARY, 0x8D4E0DD1);

	BIND_FUNC_OFFSET_HOOK(SceCtrl_ksceCtrlPeekBufferPositiveExt, KERNEL_PID, SceCtrl_modinfo.modid, 0, 0x3928, 1);
    BIND_FUNC_OFFSET_HOOK(SceCtrl_ksceCtrlReadBufferPositiveExt, KERNEL_PID, SceCtrl_modinfo.modid, 0, 0x3BCC, 1);

    BIND_FUNC_OFFSET_HOOK(SceCtrl_ksceCtrlPeekBufferPositive2, KERNEL_PID, SceCtrl_modinfo.modid, 0, 0x3EF8, 1);
    BIND_FUNC_OFFSET_HOOK(SceCtrl_ksceCtrlReadBufferPositive2, KERNEL_PID, SceCtrl_modinfo.modid, 0, 0x449C, 1);
    BIND_FUNC_OFFSET_HOOK(SceCtrl_ksceCtrlPeekBufferNegative2, KERNEL_PID, SceCtrl_modinfo.modid, 0, 0x41C8, 1);
    BIND_FUNC_OFFSET_HOOK(SceCtrl_ksceCtrlReadBufferNegative2, KERNEL_PID, SceCtrl_modinfo.modid, 0, 0x47F0, 1);
    BIND_FUNC_OFFSET_HOOK(SceCtrl_ksceCtrlPeekBufferPositiveExt2, KERNEL_PID, SceCtrl_modinfo.modid, 0, 0x4B48, 1);
    BIND_FUNC_OFFSET_HOOK(SceCtrl_ksceCtrlReadBufferPositiveExt2, KERNEL_PID, SceCtrl_modinfo.modid, 0, 0x4E14, 1);

	/* SceBt hooks */
	BIND_FUNC_OFFSET_HOOK(SceBt_sub_22999C8, KERNEL_PID,
		SceBt_modinfo.modid, 0, 0x22999C8 - 0x2280000, 1);


	BIND_FUNC_EXPORT_HOOK(SceCtrl_sceCtrlPeekBufferNegative2, KERNEL_PID,
		"SceCtrl", TAI_ANY_LIBRARY, 0x81A89660);


	BIND_FUNC_EXPORT_HOOK(SceCtrl_sceCtrlPeekBufferPositive2, KERNEL_PID,
		"SceCtrl", TAI_ANY_LIBRARY, 0x15F81E8C);

	BIND_FUNC_EXPORT_HOOK(SceCtrl_sceCtrlPeekBufferPositiveExt, KERNEL_PID,
		"SceCtrl", TAI_ANY_LIBRARY, 0xA59454D3);

	BIND_FUNC_EXPORT_HOOK(SceCtrl_sceCtrlPeekBufferPositiveExt2, KERNEL_PID,
		"SceCtrl", TAI_ANY_LIBRARY, 0x860BF292);


	BIND_FUNC_EXPORT_HOOK(SceCtrl_sceCtrlReadBufferNegative2, KERNEL_PID,
		"SceCtrl", TAI_ANY_LIBRARY, 0x27A0C5FB);


	BIND_FUNC_EXPORT_HOOK(SceCtrl_sceCtrlReadBufferPositive2, KERNEL_PID,
		"SceCtrl", TAI_ANY_LIBRARY, 0xC4226A3E);

	BIND_FUNC_EXPORT_HOOK(SceCtrl_sceCtrlReadBufferPositiveExt, KERNEL_PID,
		"SceCtrl", TAI_ANY_LIBRARY, 0xE2D99296);

	BIND_FUNC_EXPORT_HOOK(SceCtrl_sceCtrlReadBufferPositiveExt2, KERNEL_PID,
		"SceCtrl", TAI_ANY_LIBRARY, 0xA7178860);

	SceKernelHeapCreateOpt opt;
	opt.size = 0x1C;
	opt.uselock = 0x100;
	opt.field_8 = 0x10000;
	opt.field_C = 0;
	opt.field_14 = 0;
	opt.field_18 = 0;

	bt_mempool_uid = ksceKernelCreateHeap("controllervita_mempool", 0x100, &opt);

	bt_thread_uid = ksceKernelCreateThread("controllervita_bt_thread", controllervita_bt_thread,
		0x3C, 0x1000, 0, 0x10000, 0);
	ksceKernelStartThread(bt_thread_uid, 0, NULL);


	return SCE_KERNEL_START_SUCCESS;

error_find_scebt:
	return SCE_KERNEL_START_FAILED;
}

#define UNBIND_FUNC_HOOK(name) \
	do { \
		if (name##_hook_uid > 0) { \
			taiHookReleaseForKernel(name##_hook_uid, name##_ref); \
		} \
	} while(0)

int module_stop(SceSize argc, const void *args)
{
	SceUInt timeout = 0xFFFFFFFF;

	if (bt_thread_uid > 0) {
		bt_thread_run = 0;
		ksceKernelWaitThreadEnd(bt_thread_uid, NULL, &timeout);
		ksceKernelDeleteThread(bt_thread_uid);
	}

	if (bt_mempool_uid > 0) {
		ksceKernelDeleteHeap(bt_mempool_uid);
	}

	UNBIND_FUNC_HOOK(SceBt_sub_22999C8);

	UNBIND_FUNC_HOOK(SceCtrl_sceCtrlPeekBufferNegative2);
	UNBIND_FUNC_HOOK(SceCtrl_sceCtrlPeekBufferPositive2);
	UNBIND_FUNC_HOOK(SceCtrl_sceCtrlPeekBufferPositiveExt);
	UNBIND_FUNC_HOOK(SceCtrl_sceCtrlPeekBufferPositiveExt2);
	UNBIND_FUNC_HOOK(SceCtrl_sceCtrlReadBufferNegative2);
	UNBIND_FUNC_HOOK(SceCtrl_sceCtrlReadBufferPositive2);
	UNBIND_FUNC_HOOK(SceCtrl_sceCtrlReadBufferPositiveExt);
	UNBIND_FUNC_HOOK(SceCtrl_sceCtrlReadBufferPositiveExt2);

	UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlPeekBufferPositive);
    UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlReadBufferPositive);
    UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlPeekBufferNegative);
    UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlReadBufferNegative);

	UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlPeekBufferPositiveExt);
    UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlReadBufferPositiveExt);

    UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlPeekBufferPositive2);
    UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlReadBufferPositive2);
    UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlPeekBufferNegative2);
    UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlReadBufferNegative2);
    UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlPeekBufferPositiveExt2);
    UNBIND_FUNC_HOOK(SceCtrl_ksceCtrlReadBufferPositiveExt2);

	return SCE_KERNEL_STOP_SUCCESS;
}
