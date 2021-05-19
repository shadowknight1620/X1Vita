cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(X1Vita)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -Wno-unknown-pragmas -O3 -Wno-unused-function")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")

add_executable(${PROJECT_NAME}
  src/bluetooth.c
)

add_executable(debugger
  src/debug.c
)

set_target_properties(${PROJECT_NAME}
	PROPERTIES LINK_FLAGS "-nostdlib"
	COMPILE_FLAGS "-D__VITA_KERNEL__"
)

target_link_libraries(debugger
  SceLibKernel_stub
  taihen_stub
  SceThreadmgrForDriver_stub
  X1Vita_stub_weak
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

target_link_libraries(${PROJECT_NAME}
  gcc
  taihenForKernel_stub
  SceSysclibForDriver_stub
  SceSysmemForDriver_stub
  SceSysmemForKernel_stub
  SceThreadmgrForDriver_stub
  SceIofilemgrForDriver_stub
  SceBtForDriver_stub
  SceDebugForDriver_stub
  SceKernelSuspendForDriver_stub
  SceCtrlForDriver_stub
)
vita_create_self(debugger.self debugger UNSAFE)
vita_create_self(${PROJECT_NAME}.skprx ${PROJECT_NAME} CONFIG ${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.yml UNSAFE)
vita_create_stubs(stubs ${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.yml KERNEL)

install(DIRECTORY ${CMAKE_BINARY_DIR}/stubs/
  DESTINATION lib
  FILES_MATCHING PATTERN "*.a"
)

vita_create_vpk("debugger.vpk" "DEBG00001" "debugger.self"
  VERSION "01.00"
  NAME "X1Vita Debugger"
)