#pragma once
#include "../update_state_internal.h"
#include <stdio.h>
#include <string.h>
typedef struct { L4Layout layout;wchar_t root[MAX_PATH]; } UpdateFixture;
static void update_fixture_remove(const wchar_t* root){
    wchar_t pattern[MAX_PATH],child[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);
    WIN32_FIND_DATAW data;HANDLE search=FindFirstFileW(pattern,&data);
    if(search!=INVALID_HANDLE_VALUE){do{
        if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L".."))continue;
        swprintf_s(child,MAX_PATH,L"%ls\\%ls",root,data.cFileName);
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
            if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)RemoveDirectoryW(child);else update_fixture_remove(child);
        }else DeleteFileW(child);
    }while(FindNextFileW(search,&data));FindClose(search);}RemoveDirectoryW(root);
}
static bool update_fixture_init(UpdateFixture* fixture){
    memset(fixture,0,sizeof(*fixture));wchar_t temp[MAX_PATH],programs[MAX_PATH],data[MAX_PATH];
    if(!GetTempPathW(MAX_PATH,temp))return false;
    if(swprintf_s(fixture->root,MAX_PATH,L"%lsl4update-state-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64())<0 || !CreateDirectoryW(fixture->root,NULL))return false;
    swprintf_s(programs,MAX_PATH,L"%ls\\Programs",fixture->root);swprintf_s(data,MAX_PATH,L"%ls\\Data",fixture->root);
    return l4_layout_from_roots(&fixture->layout,programs,data,L"1.13.2") && l4_layout_prepare(&fixture->layout);
}
static bool update_fixture_put(UpdateFixture* fixture,DWORD window){
    L4UpdateState state={0};
    if(window){strcpy_s(state.owner,40,"17730000-0000-4000-8000-000000000001");state.generation=1;state.plan_sequence=64;state.deadline_utc=1;state.window=window;}
    BYTE bytes[L4_UPDATE_STATE_SIZE];return l4_update_state_encode(&state,bytes) && l4_update_state_replace(&fixture->layout,bytes,false);
}
static __inline bool update_fixture_live(UpdateFixture* fixture,L4UpdateState* state){
    if(!update_fixture_put(fixture,1) || !l4_update_state_read(&fixture->layout,state))return false;
    FILETIME time;GetSystemTimeAsFileTime(&time);
    state->deadline_utc=(((ULONGLONG)time.dwHighDateTime<<32)|time.dwLowDateTime)+600000000ull;
    BYTE bytes[L4_UPDATE_STATE_SIZE];return l4_update_state_encode(state,bytes) && l4_update_state_replace(&fixture->layout,bytes,false);
}
static bool update_fixture_dispose(UpdateFixture* fixture){
    wchar_t temp[MAX_PATH],absolute[MAX_PATH];
    if(!GetTempPathW(MAX_PATH,temp) || !GetFullPathNameW(fixture->root,MAX_PATH,absolute,NULL) || wcscmp(absolute,fixture->root) ||
        _wcsnicmp(temp,absolute,wcslen(temp)) || wcsncmp(absolute+wcslen(temp),L"l4update-state-",15))return false;
    update_fixture_remove(fixture->root);return GetFileAttributesW(fixture->root)==INVALID_FILE_ATTRIBUTES;
}
