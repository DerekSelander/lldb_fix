# lldb_fix


## Huh? 
This is a fix which patches LLDB v`lldb-1000.11.37.1`, which has a nasty little bug that incorrectly imports the wrong SDK when debugging via a Terminal session. For example, if you were to debug an iOS Simulator application via Terminal, LLDB will import the wrong SDK headers which prevents you from executing code correctly in that process. By default, this version of LLDB will import the MacOSX headers resulting in many scripts breaking in [Facebook's Chisel repository](https://github.com/facebook/chisel) as well as [many of my "Advanced Debugging and Reverse Engineering" scripts of my own found here](https://github.com/DerekSelander/LLDB/)

To see if you affected, open up a Terminal window and check your LLDB version:
```none
~$ lldb
(lldb) version
lldb-1000.11.37.1
  Swift-4.2
(lldb) q
Quitting LLDB will detach from one or more processes. Do you really want to proceed: [Y/n]  Y
```

If you have the same version (or maybe one coming from Xcode 10.1/10.2) this will happen when debugging any iOS Simulator application:

```none
~$ lldb -n MachOFun #The name of an application on the iOS Simulator that is running
(lldb) po @import UIKit
error: while importing modules:
error: Header search couldn't locate module UIKit
```

This is because LLDB is looking in the MacOS SDK directory for the UIKit headers. This also means many LLDB scripts which rely on this feature will also fail. For example, check out my [search](https://github.com/DerekSelander/LLDB/blob/master/lldb_commands/search.py) command which enumerates for instances of a class when used against this problematic version of LLDB

```none
(lldb) search UIViewController    # Enumerates the heap for all alive instances of UIViewController
error: 
**************************************
error: error: error: unknown type name 'CFMutableSetRef'
error: unknown type name 'CFMutableSetRef'
error: unknown type name 'CFMutableArrayRef'
error: unknown type name 'CFMutableSetRef'
error: use of undeclared identifier 'CFMutableSetRef'
error: use of undeclared identifier 'CFMutableSetRef'
error: use of undeclared identifier 'CFMutableArrayRef'
error: 'NSClassFromString' has unknown return type; cast the call to its declared return type
```

This is a big problem, since this also cripples a couple chapters in my [Advanced Apple Debugging and Reverse Engineering](https://store.raywenderlich.com/products/advanced-apple-debugging-and-reverse-engineering) :[

This means that users would need to use a different version of LLDB or find a way to get around this problem...

[You can see a tweet thread about this here](https://twitter.com/LOLgrep/status/1055172805535264768)

## How

The TLDR: This code will hunt for the location of a problematic c++ function and attempts to overwrite the pointer in a c++ vtable to code I control, correctly setting the SDK type bassed upon the environment variable **LLDB_SDK**

If no `LLDB_SDK` environment variable is set, the execution will behave normally in the buggy fashion. 

### Installation

1) clone/copy repo
2) cd into repo directory
3) run `make install`

This will compile a dylib in the current `$PWD` called lldb_fix.dylib and add the following line of code to `~/.lldbinit`

```
plugin load $(PWD)/lldb_fix.dylib
```

LLDB will call this code when it starts executing which performs the lookup of the problematic function and patches it provided you specify a valid `LLDB_SDK` environment variable

You should make sure the plugin is referenced in ~/.lldbinit with the following command:

```none
cat ~/.lldbinit | tail -1
```

### Uninstall

`make clean`

### Just build it without adding to `~/.lldbinit`

`make`

## Usage

This code will only spring to life provided you have the `LLDB_SDK` environment variable. LLDB_SDK will expect either **sim**, **mac**, or **ios**

So if you wanted to attach to an application running on the Simulator called MachOFun, then you can do the following:

```
LLDB_SDK=sim lldb -n MachOFun
```

You'll see this plugin is rather chatty... 

```
Found "AddClangModuleCompilationOptionsForSDKType" at: 0x1146c1aac
Found problematic function "AddClangModuleCompilationOptions" at: 0x1145c67fe
Found "PlatformMacOSX" vtable c++ class at: 0x115b87c58
Found problematic function at: PlatformMacOSX`<+0x160> ...patching
Success!
```

Now try a command that imports a module:
```
(lldb) po @import UIKit
Caught problematic function, changing "MacOSX" SDK to "iPhoneSimulator"
```

No error is a good thing! Now Chisel's and my commands are fair game:

```
(lldb) search UIViewController
<BrowserRootViewController: 0x7fc43946ccc0>

<BrowserContainerViewController: 0x7fc43a821200>

<UIInputWindowController: 0x7fc43b077800>
```

TADA!!!!!!!!!!!!!!!!!!! 



