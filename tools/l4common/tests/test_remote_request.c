#include "../remote_request.h"
#include "../journal_internal.h"
#include <assert.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
int wmain(void){
    wchar_t temp[MAX_PATH],root[MAX_PATH],programs[MAX_PATH],data[MAX_PATH];assert(GetTempPathW(MAX_PATH,temp));
    swprintf_s(root,MAX_PATH,L"%lsL4RemoteRequest-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());assert(CreateDirectoryW(root,NULL));
    swprintf_s(programs,MAX_PATH,L"%ls\\PF",root);swprintf_s(data,MAX_PATH,L"%ls\\PD",root);L4Layout layout;
    assert(l4_layout_from_roots(&layout,programs,data,L"1.13.6") && l4_layout_prepare(&layout));
    L4Journal* journal=NULL;const wchar_t* uuid=L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df";
    assert(l4_journal_open(&layout,uuid,true,&journal));L4RemoteRequest first,again;
    assert(l4_remote_request_save(journal,L4_REMOTE_SUITE,"latest",&first) && first.accepted_utc && journal->sequence==1);
    assert(l4_remote_request_save(journal,L4_REMOTE_SUITE,"latest",&again) && !memcmp(&first,&again,sizeof(first)) && journal->sequence==1);
    assert(!l4_remote_request_save(journal,L4_REMOTE_UPDATER,"latest",&again) && GetLastError()==ERROR_ALREADY_EXISTS);
    assert(!l4_remote_request_save(journal,L4_REMOTE_SUITE,"1.13.7",&again) && GetLastError()==ERROR_ALREADY_EXISTS);
    const char* bad[]={"", "../1.13.7", "01.13.7", "1.13.7-beta", "LATEST", "1.13", "1.13.7.0", "1000000000.0.0"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)assert(!l4_remote_request_save(journal,L4_REMOTE_SUITE,bad[i],&again));
    l4_journal_close(journal);journal=NULL;assert(l4_journal_open(&layout,uuid,false,&journal));
    assert(l4_remote_request_load(journal,&again) && !memcmp(&first,&again,sizeof(first)) && journal->sequence==1);
    BYTE duplicate[56]={0};memcpy(duplicate,"L4RPC031",8);l4_store_u32(duplicate+8,1);l4_store_u32(duplicate+12,1);strcpy_s((char*)duplicate+16,32,"latest");l4_store_u64(duplicate+48,first.accepted_utc);
    assert(l4_journal_append(journal,L4_RECORD_REMOTE_REQUEST,duplicate,sizeof(duplicate),NULL));
    assert(!l4_remote_request_load(journal,&again) && GetLastError()==ERROR_INVALID_DATA);
    l4_journal_close(journal);journal=NULL;
    assert(l4_journal_open(&layout,L"22222222-2222-4222-8222-222222222222",true,&journal));
    assert(l4_remote_request_save(journal,L4_REMOTE_UPDATER,"1.13.7",&again));l4_journal_close(journal);
    /* Fixed generated fixture only: leaves first, no recursive shell deletion. */
    wchar_t file[MAX_PATH],directory[MAX_PATH];const wchar_t* ids[]={uuid,L"22222222-2222-4222-8222-222222222222"};
    for(unsigned i=0;i<2;i++){swprintf_s(directory,MAX_PATH,L"%ls\\%ls",layout.operations,ids[i]);swprintf_s(file,MAX_PATH,L"%ls\\journal.bin",directory);assert(DeleteFileW(file));assert(RemoveDirectoryW(directory));}
    swprintf_s(file,MAX_PATH,L"%ls\\deployment.lock",layout.operations);assert(DeleteFileW(file));
    assert(RemoveDirectoryW(layout.operations));assert(RemoveDirectoryW(layout.cache));assert(RemoveDirectoryW(layout.staging));
    swprintf_s(directory,MAX_PATH,L"%ls\\update",layout.data);assert(RemoveDirectoryW(directory));
    assert(RemoveDirectoryW(layout.config));assert(RemoveDirectoryW(layout.state));assert(RemoveDirectoryW(layout.logs));
    swprintf_s(directory,MAX_PATH,L"%ls\\releases",layout.binaries);assert(RemoveDirectoryW(directory));
    assert(RemoveDirectoryW(layout.launchers));assert(RemoveDirectoryW(layout.binaries));assert(RemoveDirectoryW(layout.data));assert(RemoveDirectoryW(root));
    puts("Remote request: durable original UUID, exact retry timestamp, altered input conflict, hostile version/duplicate refusal PASS");return 0;
}
