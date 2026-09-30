set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(DEFINED ENV{STM32_TOOLCHAIN_PATH})
    file(TO_CMAKE_PATH "$ENV{STM32_TOOLCHAIN_PATH}" STM32_TOOLCHAIN_BIN)
else()
    if(NOT DEFINED STM32_GNU_TOOLS_VERSION)
        set(STM32_GNU_TOOLS_VERSION "14.3.1+st.2")
    endif()
    set(STM32_TOOLCHAIN_BIN
        "$ENV{LOCALAPPDATA}/stm32cube/bundles/gnu-tools-for-stm32/${STM32_GNU_TOOLS_VERSION}/bin")
endif()

if(NOT EXISTS "${STM32_TOOLCHAIN_BIN}/arm-none-eabi-gcc.exe")
    message(FATAL_ERROR
        "GNU Tools for STM32 not found. Install the bundle or set STM32_TOOLCHAIN_PATH.")
endif()

set(CMAKE_C_COMPILER "${STM32_TOOLCHAIN_BIN}/arm-none-eabi-gcc.exe")
set(CMAKE_ASM_COMPILER "${STM32_TOOLCHAIN_BIN}/arm-none-eabi-gcc.exe")
set(CMAKE_AR "${STM32_TOOLCHAIN_BIN}/arm-none-eabi-ar.exe")
set(CMAKE_OBJCOPY "${STM32_TOOLCHAIN_BIN}/arm-none-eabi-objcopy.exe" CACHE FILEPATH "")
set(CMAKE_SIZE "${STM32_TOOLCHAIN_BIN}/arm-none-eabi-size.exe" CACHE FILEPATH "")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
