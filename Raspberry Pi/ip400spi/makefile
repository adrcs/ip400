################################################################################
# Automatically-generated file. Do not edit!
################################################################################

RM := rm -rf

C_SRCS += \
./src/dataq.c \
./src/errno.c \
./src/logger.c \
./src/main.c \
./src/spi.c \
./src/spitask.c \
./src/timer.c \
./src/udp.c 

OBJS += \
./src/dataq.o \
./src/errno.o \
./src/logger.o \
./src/main.o \
./src/spi.o \
./src/spitask.o \
./src/timer.o \
./src/udp.o 

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: Ip400Spi

# Tool invocations
Ip400Spi: $(OBJS) makefile
	@echo 'Building target: $@'
	@echo 'Invoking: Cross GCC Linker'
	arm-linux-gnueabihf-gcc  -o "Ip400Spi" $(OBJS) -lpthread -lm -lrt
	@echo 'Finished building target: $@'
	@echo ' '


# Each subdirectory must supply rules for building sources it contributes
src/%.o: src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	arm-linux-gnueabihf-gcc -I"./include" -O0 -g3 -Wall -c -fmessage-length=0 -pthread -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

# Other Targets

clean:
	-$(RM) Ip400Spi
	-$(RM) src/*.o
	-@echo ' '

-include ../makefile.targets
