usbfs was disabled because it conflicts with udev. 
udev is a device manager for the Linux kernel introduced in Linux 2.5.
Alternate to "cat /proc/bus/usb/devices" --- "sudo cat /sys/kernel/debug/usb/devices"

VendorID / ProductID
0781:5406 SanDisk Corp. Cruzer Micro U3
abcd:1234 Multimedia Games USB Drive

// Multimedia Games USB Drive
T:  Bus=02 Lev=01 Prnt=01 Port=00 Cnt=01 Dev#=  3 Spd=480  MxCh= 0
D:  Ver= 2.00 Cls=00(>ifc ) Sub=00 Prot=00 MxPS=64 #Cfgs=  1
P:  Vendor=abcd ProdID=1234 Rev= 1.00
S:  Manufacturer=Chipsbnk
S:  Product=UDisk           
S:  SerialNumber=1106142026054503246804
C:* #Ifs= 1 Cfg#= 1 Atr=80 MxPwr=100mA
I:* If#= 0 Alt= 0 #EPs= 2 Cls=08(stor.) Sub=06 Prot=50 Driver=usb-storage
E:  Ad=01(O) Atr=02(Bulk) MxPS= 512 Ivl=0ms
E:  Ad=81(I) Atr=02(Bulk) MxPS= 512 Ivl=0ms

// SanDisk Cruzer USB Drive
T:  Bus=01 Lev=01 Prnt=01 Port=00 Cnt=01 Dev#=  4 Spd=480  MxCh= 0
D:  Ver= 2.00 Cls=00(>ifc ) Sub=00 Prot=00 MxPS=64 #Cfgs=  1
P:  Vendor=0781 ProdID=5406 Rev= 0.10
S:  Manufacturer=SanDisk
S:  Product=U3 Cruzer Micro
S:  SerialNumber=000018394775098A
C:* #Ifs= 1 Cfg#= 1 Atr=80 MxPwr=200mA
I:* If#= 0 Alt= 0 #EPs= 2 Cls=08(stor.) Sub=06 Prot=50 Driver=usb-storage
E:  Ad=81(I) Atr=02(Bulk) MxPS= 512 Ivl=0ms
E:  Ad=01(O) Atr=02(Bulk) MxPS= 512 Ivl=125us

1) run make file to create pen_register.ko
2) remove normal usb-driver - sudo rmmod usb-storage
3) add pen_register driver - sudo insmod usb-storage
4) use lsmod to check that driver is correctly loaded
5) remove pen_register driver - sudo rmmod pen_register

