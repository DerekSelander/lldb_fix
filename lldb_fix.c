//
//  lldb_fix.c
//
//
//  Created by Derek Selander on 10/16/18.
//  Copyright © 2018 Selander. All rights reserved.
//

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

////////////////// Declarations ////////////////////////////

/// Found here https://github.com/llvm-mirror/lldb/blob/818d9df928bde22243bf9603809868869676074a/source/Plugins/Platform/MacOSX/PlatformDarwin.h#L98-L102
typedef enum {
  MacOSX = 0,
  iPhoneSimulator,
  iPhoneOS,
} SDKType;

/// Will contain pointer to PlatformDarwin::AddClangModuleCompilationOptionsForSDKType
static void* (*AddClangModuleFunc)(void *platform, void * target, void *junk, long sdktype) = NULL;

static SDKType ResolvedSDK = MacOSX;

static void dsprintf(const char *format, ...) {
  static int quiet_mode = 0;
  if (quiet_mode == 0) {
    quiet_mode = getenv("DS_QUIET") ? 1 : -1;
    return;
  }
  if (quiet_mode == 1) { return; }
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end( args );
}

////////////////////////////////////////////////////////////

/// Function to intercept "bad" PlatformMacOSX::AddClangModuleCompilationOptions
__attribute__((used)) static void *hellz_yeah(void *platform, void * target, void *junk, SDKType sdktype) {
  
  dsprintf("caught problematic function changing MacOSX SDK to ");
  switch (ResolvedSDK) {
    case MacOSX:
      dsprintf("MacOSX\n");
    case iPhoneSimulator:
      dsprintf("iPhoneSimulator\n");
    case iPhoneOS:
      dsprintf("iPhoneOS\n");
      break;
  }
  
  return AddClangModuleFunc(platform, target, junk, ResolvedSDK);
}

// expects code to be linked to /usr/lib/xcselect.dylib ("xcode-select -p" equivalent)
static char *xcode_path() {
  char *input = calloc(0x400, sizeof(char));
  char dunno1, dunno2, dunno3;
  void* xcselect_get_developer_dir_path(char *ptr, size_t length, char *a, char*b, char *c);
  xcselect_get_developer_dir_path(input, 0x400, &dunno1, &dunno2, &dunno3);
  return input;
}

/// Searches in LLDB.framework for symbols
/// Doesn't use dlopen/dlsym since these functions are non exported :[
static uintptr_t address_for_function(char *symbol_name) {
  static void *handle = NULL;
  static int LLDBIndex = 0;
  static struct mach_header_64* header;
  static struct symtab_command *symtab_cmd = NULL;
  static struct segment_command_64 *linkedit_cmd = NULL;
  static intptr_t slide;
  static long onceToken;
  static const char *fullpathToLLDB = NULL;
  /////////////////////////////////
  
  // a poor man's dispatch_once :]
  if (onceToken == 0) {
    onceToken = -1;
    
    // First need to determine the path of Xcode, don't assume it's always /Applications/Xcode.app/
    char path[0x400] = {};
    strcpy(path, xcode_path());
    // This will give me something like /Applications/Xcode.app/Contents/Developer
    // replace the ./Developer with the ./SharedFrameworks/LLDB...
    char *offset = strstr(path, "/Developer");
    if (!offset) {
      dsprintf("unable to parse \"%s\"\n", path);
      return 0;
    }
    strcpy(offset, "/SharedFrameworks/LLDB.framework/Versions/A/LLDB");
    handle = dlopen(path, RTLD_NOW);
    if (!handle) {
      dsprintf("unable find LLDB.framework at \"%s\"\n", path);
      return 0;
    }
    
    // Find the LLDB.framework loaded into the LLDB process
    for (int i = 0; i < _dyld_image_count(); i++) {
      const char *imagePath = _dyld_get_image_name(i);
      if (strstr(imagePath, "SharedFrameworks/LLDB.framework/Versions/A/LLDB") != NULL) {
        LLDBIndex = i;
        fullpathToLLDB = imagePath;
        break;
      }
    }
    
    if (!fullpathToLLDB) {
      dsprintf("unable find path to LLDB.framework\n");
      return 0;
    }
    
    // Get the Mach-O header's memory address of LLDB.framework
    header = (struct mach_header_64*)_dyld_get_image_header(LLDBIndex);
    if (header->magic != MH_MAGIC_64) {
      dsprintf("LLDB.framework header messed up\n");
      return 0;
    }
    
    slide = _dyld_get_image_vmaddr_slide(LLDBIndex);
    uintptr_t cur_pointer = (uintptr_t)header + sizeof(struct mach_header_64);
    
    // Get the LC_SYMTAB Load Command and linkedit section Load Command
    for (int i = 0; i < header->ncmds; i++) {
      struct load_command *cur_cmd =  (struct load_command *)cur_pointer;
      
      // LC_SYMTAB
      if (cur_cmd->cmd == LC_SYMTAB) {
        symtab_cmd = (struct symtab_command *)cur_cmd;
      }
      
      // LC_SEGMENT_64
      if (cur_cmd->cmd == LC_SEGMENT_64) {
        struct segment_command_64 *segment_cmd = (struct segment_command_64*)cur_cmd;
        if (strcmp(segment_cmd->segname, SEG_LINKEDIT) == 0) {
          linkedit_cmd = segment_cmd;
        }
      }
      
      cur_pointer += cur_cmd->cmdsize;
    }
    
    // Make sure we have the address to both of those Load Commands
    if (!symtab_cmd || !linkedit_cmd) {
      dsprintf("unable find the appropriate commands to find variables\n");
      return 0;
    }
  } // </poor man's dispatch_once>
  
  
  // If we got here, we found the location of the symbol table, search for the function
  uintptr_t base_pointer =  slide + linkedit_cmd->vmaddr - linkedit_cmd->fileoff;
  struct nlist_64* nlists = (struct nlist_64 *)(base_pointer + symtab_cmd->symoff);
  char* strtab = (char*)(base_pointer + symtab_cmd->stroff);
  int numSymbols = symtab_cmd->nsyms;
  
  for (int i = 0; i < numSymbols; i++) {
    struct nlist_64 symbol = nlists[i];
    char *str = &strtab[symbol.n_un.n_strx];
    if (strcmp(str, symbol_name) == 0) {
      uintptr_t resolved_address = (((uintptr_t)header) + symbol.n_value);
      return resolved_address;
    }
  }
  dsprintf("unable to find symbol \"%s\"\n", symbol_name);
  return 0;
}


//////////////////////////////////////////////////////////////////////
// LLDB calls lldb::PluginInitialize(lldb::SBDebugger) on startup   //
//////////////////////////////////////////////////////////////////////
__attribute__((used)) int _ZN4lldb16PluginInitializeENS_10SBDebuggerE(void *dbg) {
  
  char * override = getenv("LLDB_SDK");
  if (override == NULL) { return 0; }
  if (strcmp(override, "ios") == 0) {
    ResolvedSDK = iPhoneOS;
  } else if (strcmp(override, "sim") == 0) {
    ResolvedSDK = iPhoneSimulator;
  } else if (strcmp(override, "mac") == 0) {
    ResolvedSDK = MacOSX;
  } else {
    dsprintf("couldn't resolve LLDB_SDK environment variable to a known type, use \"ios\", \"sim\" or \"mac\"\n");
    return 0;
  }
  
  // check for the problematic LLDB version
  char *lldb_version = (char*)address_for_function("_LLDBVersionString");
  if (!lldb_version) { return 0; }
  if (strcmp(lldb_version, "@(#)PROGRAM:LLDB  PROJECT:lldb-1000.11.37.1\n") != 0) {
    dsprintf("cowardly refusing to patch LLDB.framework, problem version is 1000.11.37.1, you are running %s\n", lldb_version);
    return 0;
  }
  
  // the address we want to call... AddClangModuleCompilationOptionsForSDKType
  //PlatformMacOSX::AddClangModuleCompilationOptions eventually calls this
  AddClangModuleFunc = (void*)address_for_function("__ZN14PlatformDarwin42AddClangModuleCompilationOptionsForSDKTypeEPN12lldb_private6TargetERNSt3__16vectorINS3_12basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEENS8_ISA_EEEENS_7SDKTypeE");
  if (!AddClangModuleFunc) { return 0; }
  dsprintf("Found \"AddClangModuleCompilationOptionsForSDKType\" at: %p\n", AddClangModuleFunc);
  
  // the address we want to intercept... which sets rcx to 0, then jmps to above function
  uintptr_t buggyFunction = address_for_function("__ZN14PlatformMacOSX32AddClangModuleCompilationOptionsEPN12lldb_private6TargetERNSt3__16vectorINS3_12basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEENS8_ISA_EEEE");
  if (!buggyFunction) { return 0; }
  dsprintf("Found problematic function \"AddClangModuleCompilationOptions\" at: %p\n", (void *)buggyFunction);
  
  // the vtable for PlatformMacOSX, start here and look for the "buggy function" address
  uintptr_t platformMacOSXClassBase = address_for_function("__ZTV14PlatformMacOSX");
  if (!platformMacOSXClassBase) { return 0; }
  dsprintf("Found \"PlatformMacOSX\" vtable c++ class at: %p\n", (void *)platformMacOSXClassBase);
  
  // If this class has more than 100 members, screw it, I give up
  for (int i = 0; i < 100 * sizeof(void *); i+=sizeof(void*)) {
    if (*(long *)(platformMacOSXClassBase + i) == buggyFunction) {
      dsprintf("Found problematic function at: PlatformMacOSX`<+0x%x> ...patching\n", i);
      *(long *)(platformMacOSXClassBase + i) = (long)&hellz_yeah;
      dsprintf("Success!\n");
    }
  }
  return 1;
}

