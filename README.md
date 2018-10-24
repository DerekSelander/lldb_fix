# lldb_fix


## Huh? 
This is a fix which patches LLDB v`lldb-1000.11.37.1`, which has a nasty little bug that incorrectly imports the wrong SDK when debugging via a Terminal session. For example, if you were to debug a iOS Simulator application via Terminal, LLDB will import the wrong SDK headers which prevents you from executing code correctly in that process. By default, this version of LLDB will import the MacOSX headers resulting in many scripts breaking in [Facebook's Chisel repository](https://github.com/facebook/chisel) as well as [many of my "Advanced Debugging and Reverse Engineering" scripts of my own found here](https://github.com/DerekSelander/LLDB/)

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

This is because LLDB is looking in the MacOS SDK directory for the UIKit headers. This also means many LLDB scripts rely on this feature to execute some advanced scripts. Check out my [search](https://github.com/DerekSelander/LLDB/blob/master/lldb_commands/search.py) command which enumerates the heap for this problematic version of LLDB

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
error: unknown type name 'CFMutableSetRef'
error: unknown type name 'CFMutableSetRef'
error: unknown type name 'CFMutableArrayRef'
error: use of undeclared identifier 'CFIndex'
```

This is a big problem, since this also cripples a couple chapters in my [Advanced Apple Debugging and Reverse Engineering](https://store.raywenderlich.com/products/advanced-apple-debugging-and-reverse-engineering) :[

## How

This code will hunt for the location of a problematic c++ function and attempts to overwrite the pointer in a c++ vtable to code I control which correctly sets the SDK type bassed upon the environment variable **LLDB_SDK**

