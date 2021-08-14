cmake_minimum_required(VERSION 3.2.0)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -Wno-unknown-pragmas -O3 -Wno-unused-function")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")

project(X1Vita)

add_executable(${PROJECT_NAME}
  src/bluetooth.c
)


set_target_properties(${PROJECT_NAME}
	PROPERTIES LINK_FLAGS "-nostdlib"
	COMPILE_FLAGS "-D__VITA_KERNEL__"
)

target_link_libraries(${PROJECT_NAME}
  gcc
  taihenForKernel_stub
  SceSysclibForDriver_stub
  SceSysmemForDriver_stub
  SceSysmemForKernel_stub
  SceThreadmgrForDriver_stub
  SceIofilemgrForDriver_stub
  taihenModuleUtils_stub
  SceBtForDriver_stub
  SceDebugForDriver_stub
  SceKernelSuspendForDriver_stub
  SceModulemgrForKernel_stub 
  SceCtrlForDriver_stub
  SceSblAIMgrForDriver_stub
  SceSysrootForDriver_stub 
  SceProcessmgrForDriver_stub
)

vita_create_self(X1VitaCompanion.self X1VitaCompanion UNSAFE)
vita_create_self(${PROJECT_NAME}.skprx ${PROJECT_NAME} CONFIG ${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.yml UNSAFE)
vita_create_stubs(stubs ${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.yml KERNEL)


add_executable(X1VitaCompanion
    src/debug.c
    src/common.h
)

target_link_libraries(X1VitaCompanion
SceLibKernel_stub
taihen_stub
SceVshBridge_stub
${CMAKE_SOURCE_DIR}/build/Debug/stubs/libX1Vita_stub_weak.a
SceThreadmgrForDriver_stub
SceShellSvc_stub
vita2d 
SceDisplay_stub 
SceGxm_stub
SceSysmodule_stub 
SceCtrl_stub 
ScePgf_stub 
ScePvf_stub
SceCommonDialog_stub 
freetype 
png 
jpeg 
z 
m 
c 
SceAppMgr_stub
)

vita_create_vpk("X1VitaCompanion.vpk" "DEBG00001" "X1VitaCompanion.self"
    VERSION "01.00"
    NAME "X1Vita Companion"
    FILE sce_sys sce_sys
)
