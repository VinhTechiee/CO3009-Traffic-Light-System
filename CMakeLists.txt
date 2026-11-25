cmake_minimum_required(VERSION 3.20)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m3)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER arm-none-eabi-gcc)
endif()
if(NOT DEFINED CMAKE_ASM_COMPILER)
    set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
endif()
project(CO3009 LANGUAGES C ASM)

set(STM32CubeF1_ROOT "" CACHE PATH "Path to the installed STM32CubeF1 V1.8.7 package")
if(NOT STM32CubeF1_ROOT)
    set(STM32CubeF1_ROOT "$ENV{USERPROFILE}/STM32Cube/Repository/STM32Cube_FW_F1_V1.8.7")
endif()
set(CO3009_PROFILE_HEADER "" CACHE FILEPATH "Optional measured board profile header")
option(CO3009_BOARD_DIAGNOSTIC "Build the isolated board diagnostic image" OFF)

set(cmsis "${STM32CubeF1_ROOT}/Drivers/CMSIS")
set(device "${cmsis}/Device/ST/STM32F1xx")
set(hal "${STM32CubeF1_ROOT}/Drivers/STM32F1xx_HAL_Driver")
set(hal_config "${STM32CubeF1_ROOT}/Projects/STM32F103RB-Nucleo/Templates/Inc")
set(startup "${device}/Source/Templates/gcc/startup_stm32f103xb.s")
set(linker_script "${CMAKE_CURRENT_SOURCE_DIR}/firmware/STM32F103RBTx_FLASH.ld")
if(NOT EXISTS "${startup}" OR NOT EXISTS "${hal_config}/stm32f1xx_hal_conf.h")
    message(FATAL_ERROR "Set STM32CubeF1_ROOT to the extracted STM32CubeF1 V1.8.7 package with HAL and CMSIS.")
endif()

set(hal_sources)
foreach(module stm32f1xx_hal stm32f1xx_hal_cortex stm32f1xx_hal_dma
               stm32f1xx_hal_flash stm32f1xx_hal_flash_ex stm32f1xx_hal_gpio
               stm32f1xx_hal_i2c stm32f1xx_hal_rcc stm32f1xx_hal_rcc_ex
               stm32f1xx_hal_tim stm32f1xx_hal_tim_ex stm32f1xx_hal_uart)
    list(APPEND hal_sources "${hal}/Src/${module}.c")
endforeach()

set(application_sources
    Core/Src/app_runtime.c
    Core/Src/board_profile.c
    Core/Src/button.c
    Core/Src/dht20.c
    Core/Src/dht20_protocol.c
    Core/Src/firmware_init.c
    Core/Src/fsm_automatic.c
    Core/Src/fsm_manual.c
    Core/Src/fsm_mode.c
    Core/Src/fsm_tuning.c
    Core/Src/global.c
    Core/Src/i2c_stm32.c
    Core/Src/lcd.c
    Core/Src/lcd_display.c
    Core/Src/lcd_format.c
    Core/Src/lcd_pcf8574.c
    Core/Src/lcd_presentation.c
    Core/Src/main.c
    Core/Src/scheduler.c
    Core/Src/software_timer.c
    Core/Src/stm32f1xx_hal_msp.c
    Core/Src/stm32f1xx_it.c
    Core/Src/traffic_light.c)

set(image "co3009")
if(CO3009_BOARD_DIAGNOSTIC)
    set(image "co3009-board-diagnostic")
    list(APPEND application_sources Core/Src/board_diagnostic.c)
endif()
add_executable(${image} ${application_sources} ${hal_sources} "${startup}"
               "${device}/Source/Templates/system_stm32f1xx.c")
set_target_properties(${image} PROPERTIES SUFFIX ".elf" C_STANDARD 11
                      C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
set_source_files_properties(${application_sources} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-Wconversion;-Wshadow")
target_include_directories(${image} PRIVATE Core/Inc)
target_include_directories(${image} SYSTEM PRIVATE "${hal_config}" "${hal}/Inc"
                           "${cmsis}/Include" "${device}/Include")
target_compile_definitions(${image} PRIVATE STM32F103xB USE_HAL_DRIVER)
if(CO3009_BOARD_DIAGNOSTIC)
    target_compile_definitions(${image} PRIVATE CO3009_BOARD_DIAGNOSTIC)
    target_compile_options(${image} PRIVATE -g)
endif()
if(CO3009_PROFILE_HEADER)
    if(NOT EXISTS "${CO3009_PROFILE_HEADER}")
        message(FATAL_ERROR "CO3009_PROFILE_HEADER does not exist.")
    endif()
    target_compile_options(${image} PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-include>" "$<$<COMPILE_LANGUAGE:C>:${CO3009_PROFILE_HEADER}>")
endif()
target_compile_options(${image} PRIVATE -mcpu=cortex-m3 -mthumb
                       -ffunction-sections -fdata-sections)
target_link_options(${image} PRIVATE -mcpu=cortex-m3 -mthumb -nostartfiles
                    --specs=nano.specs --specs=nosys.specs
                    "-T${linker_script}" "-Wl,--gc-sections,--print-memory-usage,-Map=${image}.map")
set_property(TARGET ${image} APPEND PROPERTY LINK_DEPENDS "${linker_script}")

find_program(ARM_OBJCOPY arm-none-eabi-objcopy REQUIRED)
find_program(ARM_SIZE arm-none-eabi-size REQUIRED)
add_custom_command(TARGET ${image} POST_BUILD
    COMMAND "${ARM_OBJCOPY}" -O ihex "$<TARGET_FILE:${image}>" "${image}.hex"
    COMMAND "${ARM_OBJCOPY}" -O binary "$<TARGET_FILE:${image}>" "${image}.bin"
    COMMAND "${ARM_SIZE}" "$<TARGET_FILE:${image}>"
    BYPRODUCTS "${image}.hex" "${image}.bin" "${image}.map"
    VERBATIM)
