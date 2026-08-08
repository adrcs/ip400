################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/errno.c \
../src/ip400frame.c \
../src/logger.c \
../src/main.c \
../src/udp.c 

C_DEPS += \
./src/errno.d \
./src/ip400frame.d \
./src/logger.d \
./src/main.d \
./src/udp.d 

OBJS += \
./src/errno.o \
./src/ip400frame.o \
./src/logger.o \
./src/main.o \
./src/udp.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I"/home/martin/eclipse-workspace/udpgen/include" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/errno.d ./src/errno.o ./src/ip400frame.d ./src/ip400frame.o ./src/logger.d ./src/logger.o ./src/main.d ./src/main.o ./src/udp.d ./src/udp.o

.PHONY: clean-src

