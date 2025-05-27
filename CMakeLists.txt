cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PICO_BOARD pico_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
set(FREERTOS_KERNEL_PATH "C:/EmbarcaTech/FreeRTOS-Kernel")
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)


project(PainelAtendimento C CXX ASM)
pico_sdk_init()


include_directories(${CMAKE_SOURCE_DIR}/lib)

add_executable(${PROJECT_NAME}  
        main.c 
        lib/ssd1306.c # Biblioteca para o display OLED
        )

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME} 
        pico_stdlib 
        hardware_gpio
        hardware_i2c
        hardware_pwm
        FreeRTOS-Kernel 
        FreeRTOS-Kernel-Heap4
        )

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})

