#include "../src/installed_source.h"
#include <stdio.h>
#include <objbase.h>
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return ERROR_INVALID_PARAMETER;
 L4Layout layout;L4Journal* owner=NULL;SetupInstalledSource* source=NULL;DWORD error=0;const char* stage="known-folders";
 wchar_t directory[MAX_PATH],journal[MAX_PATH];bool created=false,clean=false;
 if(!l4_layout_resolve(&layout,L"0.0.0")){error=GetLastError();goto report;}
 if(swprintf_s(directory,MAX_PATH,L"%ls\\%ls",layout.operations,argv[1])<0 || GetFileAttributesW(directory)!=INVALID_FILE_ATTRIBUTES){error=ERROR_ALREADY_EXISTS;goto report;}
 stage="owner-journal";
 if(!l4_journal_open(&layout,argv[1],true,&owner)){error=GetLastError();goto report;}created=true;
 if(!setup_installed_source_open(owner,&source)){stage=setup_installed_source_stage();error=GetLastError();goto report;}
 stage="repeat-verification";if(!setup_installed_source_verify(source)){error=GetLastError();goto report;}stage="accepted";
report:;
 FILE* out=NULL;if(_wfopen_s(&out,argv[2],L"wb") || !out){setup_installed_source_close(source);l4_journal_close(owner);return ERROR_WRITE_FAULT;}
 fprintf(out,"{\"stage\":\"%s\",\"error\":%lu,\"candidate\":\"%ls\"",stage,error,setup_installed_source_candidate());
 if(source)fprintf(out,",\"version\":\"%s\",\"arch\":\"%s\",\"source_operation\":\"%ls\"",setup_installed_source_version(source),setup_installed_source_arch(source),setup_installed_source_operation(source));
 setup_installed_source_close(source);l4_journal_close(owner);
 if(created && swprintf_s(journal,MAX_PATH,L"%ls\\journal.bin",directory)>0){clean=DeleteFileW(journal)&&RemoveDirectoryW(directory);}
 fprintf(out,",\"own_journal_cleanup\":%s}\n",clean?"true":"false");fclose(out);
 return error;
}
