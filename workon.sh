if [ ! -d /Volumes/case-sensitive ]
then
    sudo hdiutil mount ~/Documents/case-sensitive.dmg
fi

if [ -e workon-secure.sh ]
then
    source workon-secure.sh
fi

export PATH=/Volumes/case-sensitive/esp-open-sdk/xtensa-lx106-elf/bin:$PATH
export ESPTOOL_PORT=/dev/tty.usbserial-DN01AXWS
export ESPTOOL_BAUD=921600
