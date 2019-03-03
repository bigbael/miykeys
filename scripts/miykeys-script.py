#!/usr/bin/python

from kbmap import kbmap
import argparse
import time

parser = argparse.ArgumentParser(description='Send keystrokes to a host computer (via a USB HID device)')
parser.add_argument('--string',help='String you want to send')
parser.add_argument('--file',help='File containing commands to send')

args = parser.parse_args()

#print args.string
buf = bytearray(8)
kb = open('/dev/hidg0', 'w', buffering=0)
command = 0
cmd = 0
cmdstring = ""
key = ""
x = 0
cline = ""

def string(x):
	while x < (len(cline) - 1):
		buf[2], buf[0] = kbmap[cline[x]]
		kb.write(buf)
		buf[2], buf[0] = 0x00, 0x00
		kb.write(buf)
		x += 1
		
def meta(x):
	buf[2], buf[0] = kbmap[cline[x]]
	buf[0] = kbmap['META']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)
		
def enter(x):
	#Send Enter Key
	buf[2], buf[0] = 0x28, 0x00
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def cad(x):
	buf[2], buf[0] = kbmap['DEL']
	#Code for Ctrl & Alt
	buf[0] = 0x05
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)
	
def up(x):
	buf[2], buf[0] = kbmap['UP']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def down(x):
	buf[2], buf[0] = kbmap['DOWN']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def left(x):
	buf[2], buf[0] = kbmap['LEFT']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def right(x):
	buf[2], buf[0] = kbmap['RIGHT']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def sleep(x):
	length = ""
	while x < (len(cline) - 1):
		length += cline[x]
		x += 1
	time.sleep(float(length))

def rem(x):
	#Do NOTHING
	x = 0	
		
def menu(x):
	#Right click context menu
	buf[2], buf[0] = kbmap['MENU']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)
	
def brpause(x):
	buf[2], buf[0] = kbmap['BRPAUSE']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def delete(x):
	buf[2], buf[0] = kbmap['DELETE']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)
	
def end(x):
	buf[2], buf[0] = kbmap['END']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def home(x):
	buf[2], buf[0] = kbmap['HOME']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)
	
def esc(x):
	buf[2], buf[0] = kbmap['ESC']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def pageup(x):
	buf[2], buf[0] = kbmap['PAGEUP']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def pagedown(x):
	buf[2], buf[0] = kbmap['PAGEDOWN']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def printscreen(x):
	buf[2], buf[0] = kbmap['PRINTSCREEN']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def space(x):
	buf[2], buf[0] = kbmap['SPACE']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def tab(x):
	buf[2], buf[0] = kbmap['TAB']
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)	

def shift(x):
	string = ""
	while x < (len(cline) - 1):
		string += cline[x]
		x += 1
	buf[2], buf[0] = kbmap[string]
	buf[0] = 0x02
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def alt(x):
	string = ""
	while x < (len(cline) - 1):
		string += cline[x]
		x += 1
	buf[2], buf[0] = kbmap[string]
	buf[0] = 0x04
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)

def control(x):
	string = ""
	while x < (len(cline) - 1):
		string += cline[x]
		x += 1
	buf[2], buf[0] = kbmap[string]
	buf[0] = 0x01
	kb.write(buf)
	buf[2], buf[0] = 0x00, 0x00
	kb.write(buf)


def parsefile(x):
	global cline
	global cmd
	global cmdstring
	
	fh = open(args.file)
	cline = fh.readline()
	#cline = cline.strip('\n')
		
	while cline:

		#Read command
		while (x < (len(cline) - 1)) and (cmd == 0):
			if cline[x] == " ":
				cmd = 1
			else:
				cmdstring += cline[x]
			x += 1
		print cmdstring
		
		#Branch based on command
		switcher = {
			"STRING": string,
			"META": meta,
			"GUI": meta,
			"WIN": meta,
			"ENTER": enter,
			"SLEEP": sleep,
			"UP": up,
			"DOWN": down,
			"LEFT": left,
			"RIGHT": right,
			"REM": rem,
			"MENU": menu,
			"BRPAUSE": brpause,
			"DELETE": delete,
			"END": end,
			"ESC": esc,
			"HOME": home,
			"PAGEUP": pageup,
			"PAGEDOWN": pagedown,
			"PRINTSCREEN": printscreen,
			"SPACE": space,
			"TAB": tab,
			"SHIFT": shift,
			"ALT": alt,
			"CONTROL": control,
			"CTRL": control,
			"CAD": cad
			}
		func = switcher.get(cmdstring, "nothing")
		func(x)
		
		cline = fh.readline()
		x = 0
		cmd = 0
		cmdstring = ""

	fh.close()

def parsestring(x):
	while x < (len(args.string)):
		buf[2], buf[0] = kbmap[args.string[x]]
		kb.write(buf)
		buf[2], buf[0] = 0x00, 0x00
		kb.write(buf)
		x += 1
	enter(x)
		
if args.file is not None:
	parsefile(x)

if args.string is not None:
	parsestring(x)
	