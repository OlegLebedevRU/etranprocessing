#include "ui.h"
#include "engine.h"
#include "unpack.h"
#include "log.h"
#include "version.h"
#include "../res/resource.h"
#include <commctrl.h>
#pragma warning(push)
#pragma warning(disable:4201)
#include <richedit.h>
#pragma warning(pop)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winver.h>

#define WM_SETUP_PHASE_UPDATE (WM_APP+1)
#define WM_SETUP_SERVICE_UPDATE (WM_APP+2)
#define WM_SETUP_NOTICE (WM_APP+3)
#define WM_SETUP_LOG (WM_APP+4)
#define WM_SETUP_FINISHED (WM_APP+5)
#define WM_SETUP_CHECKED (WM_APP+6)
static SetupContext context;
static HANDLE worker;
static HFONT body_font, title_font, bold_font, mono_font;
static UINT dpi=96;
static DWORD elapsed;
static bool running, finished, checked, check_failed, preview;
static int severity, errors, warnings;
static ServiceLifecycleStatus service_status[4];
typedef struct { int phase; wchar_t name[64], text[256]; } PhaseMessage;
typedef struct { int index; ServiceLifecycleStatus status; wchar_t text[128]; } ServiceMessage;

static int px(int value) { return MulDiv(value,dpi,96); }
static bool high_contrast(void) {
    HIGHCONTRASTW value={sizeof(value)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(value),&value,0) && (value.dwFlags&HCF_HIGHCONTRASTON);
}
static COLORREF accent(int level) {
    if(high_contrast()) return GetSysColor(COLOR_WINDOWTEXT);
    return level==2 ? RGB(180,35,35) : level==1 ? RGB(145,87,0) : RGB(24,79,145);
}
static void place(HWND dialog,int id,int x,int y,int w,int h) {
    MoveWindow(GetDlgItem(dialog,id),x,y,w>0?w:1,h>0?h:1,TRUE);
}
static void layout(HWND dialog) {
    RECT rc; GetClientRect(dialog,&rc);
    int margin=px(24), gap=px(24), left=px(330), right=margin+left+gap;
    bool compact=rc.bottom<px(670);
    if(rc.right<px(1000)) {left=px(300);right=margin+left+gap;}
    int bottom=rc.bottom-px(66), log_width=rc.right-right-margin;
    place(dialog,IDC_STATIC_HEADER,margin,px(22),rc.right-2*margin,px(42));
    place(dialog,IDC_STATIC_NOTICE,margin,px(69),rc.right-2*margin,px(34));
    place(dialog,IDC_STATIC_VER_INFO,margin,px(compact?110:119),left,px(compact?58:72));
    place(dialog,IDC_STATIC_PATH_INFO,margin,px(compact?174:195),left,px(compact?40:54));
    place(dialog,IDC_STATIC_SERVICES_LBL,margin,px(compact?219:260),left,px(32));
    const int ids[]={IDC_SVC_LEO4PROXY,IDC_SVC_MOSQUITTO,IDC_SVC_L4CON,IDC_SVC_L4SUPERV};
    for(int i=0;i<4;i++) place(dialog,ids[i],margin,px(compact?254+i*29:299+i*33),left,px(29));
    place(dialog,IDC_STATIC_PHASE,margin,px(compact?390:445),left-px(55),px(29));
    place(dialog,IDC_STATIC_PERCENT,margin+left-px(55),px(compact?390:445),px(55),px(29));
    place(dialog,IDC_PROGRESS_BAR,margin,px(compact?423:478),left,px(13));
    place(dialog,IDC_STATIC_STATUS,margin,px(compact?451:509),left,px(61));
    place(dialog,IDC_STATIC_ELAPSED,margin,bottom,left,px(35));
    place(dialog,IDC_STATIC_LOG_TITLE,right,px(119),log_width-px(245),px(29));
    place(dialog,IDC_BTN_NETWORK,rc.right-margin-px(236),px(114),px(100),px(31));
    place(dialog,IDC_BTN_DETAILS,rc.right-margin-px(128),px(114),px(128),px(31));
    place(dialog,IDC_EDIT_DETAILS,right,px(159),log_width,bottom-px(168));
    place(dialog,IDC_STATIC_LOG_COUNTS,right,bottom-px(1),log_width-px(370),px(35));
    place(dialog,IDC_BTN_ACTION,rc.right-margin-px(300),bottom,px(180),px(40));
    place(dialog,IDCANCEL,rc.right-margin-px(108),bottom,px(108),px(40));
}
static BOOL CALLBACK set_body_font(HWND control,LPARAM font) {
    SendMessageW(control,WM_SETFONT,(WPARAM)font,TRUE); return TRUE;
}
static HFONT make_font(int points,int weight,const wchar_t* face) {
    return CreateFontW(-MulDiv(points,dpi,72),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);
}
static void fonts(HWND dialog) {
    HFONT old[]={body_font,title_font,bold_font,mono_font};
    body_font=make_font(11,FW_NORMAL,L"Segoe UI");
    title_font=make_font(22,FW_SEMIBOLD,L"Segoe UI");
    bold_font=make_font(11,FW_SEMIBOLD,L"Segoe UI");
    mono_font=make_font(10,FW_NORMAL,L"Consolas");
    EnumChildWindows(dialog,set_body_font,(LPARAM)body_font);
    SendDlgItemMessageW(dialog,IDC_STATIC_HEADER,WM_SETFONT,(WPARAM)title_font,TRUE);
    const int ids[]={IDC_STATIC_SERVICES_LBL,IDC_STATIC_LOG_TITLE,IDC_STATIC_PHASE,IDC_STATIC_STATUS,IDC_BTN_ACTION};
    for(int i=0;i<5;i++) SendDlgItemMessageW(dialog,ids[i],WM_SETFONT,(WPARAM)bold_font,TRUE);
    SendDlgItemMessageW(dialog,IDC_EDIT_DETAILS,WM_SETFONT,(WPARAM)mono_font,TRUE);
    for(int i=0;i<4;i++) if(old[i]) DeleteObject(old[i]);
}
static void progress(HWND dialog,int percent) {
    if(percent<0) percent=0; if(percent>100) percent=100;
    SendDlgItemMessageW(dialog,IDC_PROGRESS_BAR,PBM_SETPOS,percent,0);
    wchar_t text[32]; swprintf_s(text,32,L"%d%%",percent);
    SetDlgItemTextW(dialog,IDC_STATIC_PERCENT,text);
}
static void status(HWND dialog,const wchar_t* text,int level) {
    severity=level;
    SetDlgItemTextW(dialog,IDC_STATIC_STATUS,text);
    InvalidateRect(GetDlgItem(dialog,IDC_STATIC_STATUS),NULL,TRUE);
    SendDlgItemMessageW(dialog,IDC_PROGRESS_BAR,PBM_SETSTATE,level==2?PBST_ERROR:level==1?PBST_PAUSED:PBST_NORMAL,0);
}
static void append_log(HWND dialog,const wchar_t* text) {
    HWND edit=GetDlgItem(dialog,IDC_EDIT_DETAILS);
    int level=wcsstr(text,L"[ERROR]")?2:wcsstr(text,L"[WARN]")?1:0;
    if(level==2) errors++; if(level==1) warnings++;
    wchar_t counts[128]; swprintf_s(counts,128,L"Ошибки: %d   Предупреждения: %d",errors,warnings);
    SetDlgItemTextW(dialog,IDC_STATIC_LOG_COUNTS,counts);
    int length=GetWindowTextLengthW(edit);
    CHARRANGE selection; SendMessageW(edit,EM_EXGETSEL,0,(LPARAM)&selection);
    bool follow=(selection.cpMin==selection.cpMax && selection.cpMax>=length);
    if(length>300000) {
        SendMessageW(edit,EM_SETSEL,0,50000); SendMessageW(edit,EM_REPLACESEL,FALSE,(LPARAM)L"[... предыдущие строки доступны в l4setup.log ...]\r\n");
        length=GetWindowTextLengthW(edit); follow=true;
    }
    SendMessageW(edit,EM_SETSEL,length,length);
    POINT scroll; SendMessageW(edit,EM_GETSCROLLPOS,0,(LPARAM)&scroll);
    CHARFORMAT2W format={0}; format.cbSize=sizeof(format);
    format.dwMask=CFM_COLOR|CFM_BOLD;
    format.crTextColor=level?accent(level):GetSysColor(COLOR_WINDOWTEXT);
    format.dwEffects=level?CFE_BOLD:0;
    SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
    SendMessageW(edit,EM_REPLACESEL,FALSE,(LPARAM)text);
    SendMessageW(edit,EM_REPLACESEL,FALSE,(LPARAM)L"\r\n");
    if(follow) SendMessageW(edit,EM_SCROLLCARET,0,0);
    else {SendMessageW(edit,EM_EXSETSEL,0,(LPARAM)&selection);SendMessageW(edit,EM_SETSCROLLPOS,0,(LPARAM)&scroll);}
}
static void post_text(HWND dialog,UINT message,const char* text) {
    if(!text) return;
    int count=MultiByteToWideChar(CP_UTF8,0,text,-1,NULL,0);
    if(count<=0) return;
    wchar_t* copy=(wchar_t*)malloc((size_t)count*sizeof(wchar_t));
    if(!copy) return;
    MultiByteToWideChar(CP_UTF8,0,text,-1,copy,count);
    if(!PostMessageW(dialog,message,0,(LPARAM)copy)) free(copy);
}
static void on_log(const char* line,void* data) { post_text((HWND)data,WM_SETUP_LOG,line); }
static void on_notice(const char* line,void* data) { post_text((HWND)data,WM_SETUP_NOTICE,line); }
static void on_phase(SetupPhase phase,const char* name,const char* text,void* data) {
    PhaseMessage* message=(PhaseMessage*)calloc(1,sizeof(*message)); if(!message) return;
    message->phase=phase;
    MultiByteToWideChar(CP_UTF8,0,name,-1,message->name,64);
    MultiByteToWideChar(CP_UTF8,0,text,-1,message->text,256);
    if(!PostMessageW((HWND)data,WM_SETUP_PHASE_UPDATE,0,(LPARAM)message)) free(message);
}
static void on_service(int index,const wchar_t* name,ServiceLifecycleStatus value,DWORD seconds,const char* notice,void* data) {
    (void)seconds;
    ServiceMessage* message=(ServiceMessage*)calloc(1,sizeof(*message)); if(!message) return;
    message->index=index; message->status=value;
    const wchar_t* label=L"Ожидание";
    switch(value) {
        case SVC_STATUS_STARTING:label=L"Запуск";break;
        case SVC_STATUS_RUNNING:label=L"Работает";break;
        case SVC_STATUS_STOPPING:label=L"Остановка";break;
        case SVC_STATUS_STOPPED:label=L"Остановлена";break;
        case SVC_STATUS_CHECKING:label=L"Проверка";break;
        case SVC_STATUS_READY:label=L"Готова";break;
        case SVC_STATUS_FAILED:label=L"ОШИБКА";break;
        default:break;
    }
    swprintf_s(message->text,128,L"%ls: %ls",name,label);
    if(notice && *notice) post_text((HWND)data,WM_SETUP_LOG,notice);
    if(!PostMessageW((HWND)data,WM_SETUP_SERVICE_UPDATE,0,(LPARAM)message)) free(message);
}
static DWORD WINAPI run_check(void* data) {
    bool result=engine_phase_check(&context);
    PostMessageW((HWND)data,WM_SETUP_CHECKED,result,0); return 0;
}
static DWORD WINAPI run_setup(void* data) {
    int result=engine_run_pipeline(&context);
    PostMessageW((HWND)data,WM_SETUP_FINISHED,result,0); return 0;
}
static void close_worker(void) {
    if(worker) { WaitForSingleObject(worker,INFINITE); CloseHandle(worker); worker=NULL; }
}
static const wchar_t* operation_name(void) {
    return context.op_type==OP_UPGRADE?L"Обновить":context.op_type==OP_REPAIR?L"Восстановить":context.op_type==OP_VERIFY?L"Проверить":L"Установить";
}
static void versions(HWND dialog) {
    SetDlgItemTextW(dialog,IDC_STATIC_HEADER,context.op_type==OP_UPGRADE?L"Обновление L4 Tools":context.op_type==OP_REPAIR?L"Восстановление L4 Tools":context.op_type==OP_VERIFY?L"Проверка L4 Tools":L"Установка L4 Tools");
    wchar_t text[256];
    swprintf_s(text,256,L"Пакет L4 Tools %hs\r\nУстановлен: %hs\r\nДействие: %ls",context.target_version,
        context.installed_version[0]?context.installed_version:"нет",operation_name());
    SetDlgItemTextW(dialog,IDC_STATIC_VER_INFO,text);
}
static void installed_versions(HWND dialog) {
    static const wchar_t* paths[]={L"l4capture\\bin\\l4capture.exe",L"l4con\\l4con.exe",L"l4desk\\l4desk.exe",
        L"l4pin\\l4pin.exe",L"l4sql\\l4sql.exe",L"l4superv\\l4superv.exe",L"leo4proxy\\leo4proxy.exe",L"mosquitto\\mosquitto.exe"};
    for(int i=0;i<8;i++) {
        wchar_t path[MAX_PATH],text[512];
        if(swprintf_s(path,MAX_PATH,L"%ls\\%ls",context.opts->dest,paths[i])<0) continue;
        DWORD ignored=0,size=GetFileVersionInfoSizeW(path,&ignored);
        DWORD attrs=GetFileAttributesW(path);
        const wchar_t* value=attrs!=INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)
            ? L"файл установлен; версия не указана в EXE" : L"не установлен"; wchar_t version[64];
        if(size && size<=65536) {
            BYTE* block=(BYTE*)malloc(size); VS_FIXEDFILEINFO* info=NULL; UINT count=0;
            if(block && GetFileVersionInfoW(path,0,size,block) && VerQueryValueW(block,L"\\",(void**)&info,&count) &&
                count>=sizeof(*info) && info->dwSignature==0xFEEF04BD) {
                swprintf_s(version,64,L"%u.%u.%u.%u",HIWORD(info->dwFileVersionMS),LOWORD(info->dwFileVersionMS),
                    HIWORD(info->dwFileVersionLS),LOWORD(info->dwFileVersionLS)); value=version;
            } free(block);
        }
        swprintf_s(text,512,L"[INFO] %ls: %ls",paths[i],value); append_log(dialog,text);
    }
}
static void copy_log(HWND dialog) {
    HWND edit=GetDlgItem(dialog,IDC_EDIT_DETAILS); int count=GetWindowTextLengthW(edit)+1;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(size_t)count*sizeof(wchar_t)); if(!memory) return;
    wchar_t* text=(wchar_t*)GlobalLock(memory); if(!text) {GlobalFree(memory);return;}
    GetWindowTextW(edit,text,count); GlobalUnlock(memory);
    if(OpenClipboard(dialog)) {
        EmptyClipboard(); if(SetClipboardData(CF_UNICODETEXT,memory)) memory=NULL; CloseClipboard();
    }
    if(memory) GlobalFree(memory);
}
static void request_close(HWND dialog) {
    if(running) {
        context.cancel_requested=true;
        status(dialog,L"Завершаем текущий этап безопасно. Дождитесь остановки операции.",1);
        EnableWindow(GetDlgItem(dialog,IDCANCEL),FALSE);
    } else EndDialog(dialog,preview?0:finished||check_failed?context.final_exit_code:31);
}
static bool start_worker(HWND dialog,bool check) {
    close_worker(); running=true; elapsed=0;
    EnableWindow(GetDlgItem(dialog,IDC_BTN_ACTION),FALSE);
    SetDlgItemTextW(dialog,IDC_BTN_ACTION,L"Выполняется…");
    EnableWindow(GetDlgItem(dialog,IDCANCEL),TRUE);
    SetTimer(dialog,1,1000,NULL);
    worker=CreateThread(NULL,0,check?run_check:run_setup,dialog,0,NULL);
    if(!worker) {
        running=false; KillTimer(dialog,1); context.final_exit_code=1;
        status(dialog,L"Не удалось запустить операцию. Закройте окно и повторите запуск.",2);
        SetDlgItemTextW(dialog,IDCANCEL,L"Закрыть"); return false;
    } return true;
}
static INT_PTR CALLBACK network_proc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
    (void)lParam;
    const int ids[]={IDC_NETWORK_MQTT,IDC_NETWORK_HTTP,IDC_NETWORK_STREAM,IDC_NETWORK_RTP};
    if (message==WM_INITDIALOG) {
        CliOptions view=*context.opts;
        if(!view.network_specified && context.installed_version[0] &&
            !services_read_network_options(view.dest,&view)) {
            MessageBoxW(dialog,L"Не удалось прочитать текущую конфигурацию Leo4Proxy из SCM.",L"Сеть",MB_OK|MB_ICONWARNING);
            EndDialog(dialog,0);return TRUE;
        }
        SetDlgItemTextW(dialog,IDC_NETWORK_IP,view.policy_bootstrap_ip);
        for(int c=0;c<4;c++) SetDlgItemTextW(dialog,ids[c],view.remote_endpoints[c]);
        CheckDlgButton(dialog,IDC_NETWORK_NO_SRV,view.no_srv?BST_CHECKED:BST_UNCHECKED);
        return TRUE;
    }
    if (message==WM_COMMAND && LOWORD(wParam)==IDCANCEL) { EndDialog(dialog,0); return TRUE; }
    if (message==WM_COMMAND && LOWORD(wParam)==IDOK) {
        wchar_t values[5][256]; GetDlgItemTextW(dialog,IDC_NETWORK_IP,values[0],256);
        for(int c=0;c<4;c++) GetDlgItemTextW(dialog,ids[c],values[c+1],256);
        wchar_t* args[16]={L"l4setup",L"--resolve-auto"}; int count=2;
        if(values[0][0]) {args[count++]=L"--policy-bootstrap-ip";args[count++]=values[0];}
        const wchar_t* flags[]={L"--mqtt-remote",L"--http-remote",L"--stream-remote",L"--rtp-remote"};
        for(int c=0;c<4;c++) if(values[c+1][0]) {args[count++]=(wchar_t*)flags[c];args[count++]=values[c+1];}
        if(IsDlgButtonChecked(dialog,IDC_NETWORK_NO_SRV)==BST_CHECKED) args[count++]=L"--no-srv";
        CliOptions parsed; char error[256]={0};
        if(!cli_parse(count,args,&parsed,error,sizeof(error))) {
            MessageBoxW(dialog,L"Введите IP и host:port без схемы; порт 1–65535.",L"Неверный адрес",MB_OK|MB_ICONWARNING);return TRUE;
        }
        memcpy(context.opts->remote_endpoints,parsed.remote_endpoints,sizeof(parsed.remote_endpoints));
        wcscpy_s(context.opts->policy_bootstrap_ip,16,values[0]); context.opts->no_srv=parsed.no_srv;
        context.opts->network_specified=true;
        if(context.op_type==OP_VERIFY)context.op_type=OP_REPAIR;
        EndDialog(dialog,1); return TRUE;
    }
    return FALSE;
}
static INT_PTR CALLBACK dialog_proc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
        case WM_INITDIALOG: {
            context.user_data=dialog; context.hWndParent=dialog;
            if (context.opts->smoke_only) EnableWindow(GetDlgItem(dialog,IDC_BTN_NETWORK),FALSE);
            HDC dc=GetDC(dialog); dpi=(UINT)GetDeviceCaps(dc,LOGPIXELSY); ReleaseDC(dialog,dc);
            if(!dpi) dpi=96;
            fonts(dialog);
            HWND action=GetDlgItem(dialog,IDC_BTN_ACTION);
            SetWindowLongPtrW(action,GWL_STYLE,(GetWindowLongPtrW(action,GWL_STYLE)&~(LONG_PTR)0xF)|BS_OWNERDRAW);
            SendMessageW(dialog,DM_SETDEFID,IDC_BTN_ACTION,0);
            SendDlgItemMessageW(dialog,IDC_EDIT_DETAILS,EM_EXLIMITTEXT,0,350000);
            SendDlgItemMessageW(dialog,IDC_EDIT_DETAILS,EM_SETBKGNDCOLOR,0,GetSysColor(COLOR_WINDOW));
            RECT work; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
            int width=px(1120),height=px(730);
            if(width>work.right-work.left-px(32)) width=work.right-work.left-px(32);
            if(height>work.bottom-work.top-px(32)) height=work.bottom-work.top-px(32);
            SetWindowPos(dialog,NULL,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,
                width,height,SWP_NOZORDER);
            layout(dialog);
            wchar_t path[MAX_PATH+32]; swprintf_s(path,MAX_PATH+32,L"Папка установки\r\n%ls",context.opts->dest);
            SetDlgItemTextW(dialog,IDC_STATIC_PATH_INFO,path);
            if(preview) {
                strcpy_s(context.target_version,32,L4SETUP_VERSION_STRING);
                strcpy_s(context.installed_version,32,"1.9.3"); context.op_type=OP_UPGRADE;
                checked=true; versions(dialog); SetDlgItemTextW(dialog,IDC_BTN_ACTION,L"Обновить");
                SetDlgItemTextW(dialog,IDC_STATIC_NOTICE,L"Предпросмотр интерфейса. Файлы, сертификаты и службы не изменяются.");
                status(dialog,L"Готово к обновлению. Пример предупреждения показан в журнале.",0);
                append_log(dialog,L"[INFO] Проверка установленных компонентов завершена.");
                append_log(dialog,L"[WARN] Для активации терминала потребуется сертификат iot.leo4.ru.");
                append_log(dialog,L"[ERROR] Пример: служба не ответила в отведённое время. Подробности доступны здесь.");
                SetFocus(GetDlgItem(dialog,IDC_BTN_ACTION)); return FALSE;
            }
            log_set_callback(on_log,dialog);
            status(dialog,L"Проверяем систему и установленный пакет…",0);
            start_worker(dialog,true); SetFocus(GetDlgItem(dialog,IDCANCEL)); return FALSE;
        }
        case WM_SIZE: layout(dialog); return TRUE;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* limits=(MINMAXINFO*)lParam;
            limits->ptMinTrackSize.x=px(900); limits->ptMinTrackSize.y=px(620); return TRUE;
        }
        case WM_DPICHANGED: {
            dpi=HIWORD(wParam); fonts(dialog); RECT* rect=(RECT*)lParam;
            SetWindowPos(dialog,NULL,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER);
            layout(dialog); return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            int id=GetDlgCtrlID((HWND)lParam); int level=id==IDC_STATIC_STATUS?severity:0;
            if(id>=IDC_SVC_LEO4PROXY && id<=IDC_SVC_L4SUPERV && service_status[id-IDC_SVC_LEO4PROXY]==SVC_STATUS_FAILED) level=2;
            SetTextColor((HDC)wParam,level||id==IDC_STATIC_HEADER||id==IDC_STATIC_PHASE?accent(level):GetSysColor(COLOR_WINDOWTEXT));
            SetBkMode((HDC)wParam,TRANSPARENT); return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* item=(DRAWITEMSTRUCT*)lParam;
            if(item->CtlID!=IDC_BTN_ACTION) break;
            bool disabled=(item->itemState&ODS_DISABLED)!=0;
            bool pressed=(item->itemState&ODS_SELECTED)!=0;
            COLORREF background=high_contrast()?GetSysColor(COLOR_HIGHLIGHT):disabled?RGB(228,231,235):pressed?RGB(16,63,117):RGB(25,92,165);
            HBRUSH brush=CreateSolidBrush(background);FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);
            SetBkMode(item->hDC,TRANSPARENT);
            SetTextColor(item->hDC,disabled?GetSysColor(COLOR_GRAYTEXT):high_contrast()?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(255,255,255));
            HGDIOBJ previous=SelectObject(item->hDC,bold_font);
            wchar_t text[128];GetWindowTextW(item->hwndItem,text,128);
            RECT rect=item->rcItem;DrawTextW(item->hDC,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(item->itemState&ODS_FOCUS){InflateRect(&rect,-px(4),-px(4));DrawFocusRect(item->hDC,&rect);}
            SelectObject(item->hDC,previous);return TRUE;
        }
        case WM_ERASEBKGND: {
            RECT rect;GetClientRect(dialog,&rect);FillRect((HDC)wParam,&rect,GetSysColorBrush(COLOR_WINDOW));return TRUE;
        }
        case WM_TIMER: {
            if(running) {wchar_t text[64];swprintf_s(text,64,L"Прошло: %lu с",++elapsed);SetDlgItemTextW(dialog,IDC_STATIC_ELAPSED,text);} return TRUE;
        }
        case WM_SETUP_PHASE_UPDATE: {
            PhaseMessage* value=(PhaseMessage*)lParam;
            if(value) {
                static const wchar_t* names[]={L"Проверка",L"Подготовка",L"Остановка служб",L"Обновление файлов",L"Запуск служб",L"Проверка работы",L"Завершение"};
                static const int percents[]={5,15,30,55,70,90,100};
                if(value->phase>=0 && value->phase<7) {SetDlgItemTextW(dialog,IDC_STATIC_PHASE,names[value->phase]);progress(dialog,percents[value->phase]);}
                if(!context.cancel_requested) status(dialog,value->text,0); free(value);
            } return TRUE;
        }
        case WM_SETUP_SERVICE_UPDATE: {
            ServiceMessage* value=(ServiceMessage*)lParam;
            if(value) {
                if(value->index>=0 && value->index<4) {
                    service_status[value->index]=value->status;
                    SetDlgItemTextW(dialog,IDC_SVC_LEO4PROXY+value->index,value->text);
                    InvalidateRect(GetDlgItem(dialog,IDC_SVC_LEO4PROXY+value->index),NULL,TRUE);
                } free(value);
            } return TRUE;
        }
        case WM_SETUP_NOTICE: {
            wchar_t* text=(wchar_t*)lParam;if(text){SetDlgItemTextW(dialog,IDC_STATIC_NOTICE,text);free(text);}return TRUE;
        }
        case WM_SETUP_LOG: {
            wchar_t* text=(wchar_t*)lParam;if(text){append_log(dialog,text);free(text);}return TRUE;
        }
        case WM_SETUP_CHECKED: {
            close_worker();running=false;KillTimer(dialog,1);checked=true;check_failed=!wParam;
            versions(dialog);installed_versions(dialog);
            SetDlgItemTextW(dialog,IDC_BTN_ACTION,operation_name());
            EnableWindow(GetDlgItem(dialog,IDC_BTN_ACTION),wParam && !context.cancel_requested);
            EnableWindow(GetDlgItem(dialog,IDCANCEL),TRUE);
            if(context.cancel_requested) request_close(dialog);
            else if(wParam) {status(dialog,L"Проверка завершена. Можно начать выбранное действие.",0);if(GetFocus()==GetDlgItem(dialog,IDCANCEL))SetFocus(GetDlgItem(dialog,IDC_BTN_ACTION));}
            else {status(dialog,L"Не удалось подготовить установку. Ошибка выделена в журнале справа.",2);SetDlgItemTextW(dialog,IDCANCEL,L"Закрыть");}
            return TRUE;
        }
        case WM_SETUP_FINISHED: {
            close_worker();running=false;finished=true;KillTimer(dialog,1);
            int result=context.final_exit_code;
            bool warning=result==10||result==11||result==12;
            bool retry=result!=0 && !warning && result!=31;
            severity=result==0?0:warning||result==31?1:2;
            status(dialog,result==0?L"Готово. L4 Tools установлены и проверены.":warning?L"Операция завершена с ограничениями. Проверьте предупреждения в журнале.":
                result==31?L"Операция отменена на безопасной границе этапа.":L"Операция не завершена. Исправьте причину ошибки перед повтором.",severity);
            wchar_t line[512];swprintf_s(line,512,L"[%ls] Результат: %hs; код %d; причина: %hs",severity==2?L"ERROR":severity==1?L"WARN":L"INFO",context.summary.status,result,context.summary.error_reason);
            append_log(dialog,line);
            SetDlgItemTextW(dialog,IDC_BTN_ACTION,retry?L"Повторить":L"Готово");
            EnableWindow(GetDlgItem(dialog,IDC_BTN_ACTION),TRUE);
            SetDlgItemTextW(dialog,IDCANCEL,L"Закрыть");EnableWindow(GetDlgItem(dialog,IDCANCEL),TRUE);
            if(result==0) progress(dialog,100);
            if(unpack_read_installed_version(context.opts->dest,context.installed_version,sizeof(context.installed_version))) versions(dialog);
            installed_versions(dialog);return TRUE;
        }
        case WM_COMMAND: {
            int id=LOWORD(wParam);
            if(id==IDC_BTN_NETWORK && !running && !finished && !context.opts->smoke_only) {
                if(DialogBoxParamW(context.hInstance,MAKEINTRESOURCEW(IDD_NETWORK_DIALOG),dialog,network_proc,0)==1) {
                    versions(dialog);SetDlgItemTextW(dialog,IDC_BTN_ACTION,operation_name());
                }
                return TRUE;
            }
            if(id==IDC_BTN_DETAILS){copy_log(dialog);return TRUE;}
            if(id==IDCANCEL){request_close(dialog);return TRUE;}
            if(id==IDC_BTN_ACTION && !running && checked) {
                if(preview) {status(dialog,L"Предпросмотр: обновление не запускается.",0);return TRUE;}
                if(finished && (context.final_exit_code==0||context.final_exit_code==10||context.final_exit_code==11||context.final_exit_code==12||context.final_exit_code==31)) {request_close(dialog);return TRUE;}
                if(finished) {
                    if(context.hMutex){CloseHandle(context.hMutex);context.hMutex=NULL;}
                    memset(&context.summary,0,sizeof(context.summary));
                    context.final_exit_code=0;context.pending_reboot=false;context.disk_space_ok=false;context.sn[0]=0;
                    context.cancel_requested=false; context.retry_requested=true;
                    finished=false;checked=false;start_worker(dialog,true);
                } else {context.cancel_requested=false;start_worker(dialog,false);}
                return TRUE;
            }break;
        }
        case WM_CLOSE:request_close(dialog);return TRUE;
        case WM_DESTROY: {
            log_set_callback(NULL,NULL);
            HFONT handles[]={body_font,title_font,bold_font,mono_font};for(int i=0;i<4;i++)if(handles[i])DeleteObject(handles[i]);
            body_font=title_font=bold_font=mono_font=NULL;return TRUE;
        }
    } return FALSE;
}
int ui_run_interactive_setup(HINSTANCE instance,CliOptions* opts) {
    if(!opts) return 1;
    HMODULE rich=LoadLibraryW(L"Msftedit.dll");if(!rich)return 1;
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES|ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    memset(&context,0,sizeof(context)); context.opts=opts;context.hInstance=instance;context.is_interactive=true;
    context.on_phase_change=on_phase;context.on_service_status=on_service;context.on_notice=on_notice;context.on_log_line=on_log;
    preview=opts->preview_ui;running=finished=checked=check_failed=false;errors=warnings=severity=0;elapsed=0;
    memset(service_status,0,sizeof(service_status));
    INT_PTR result=DialogBoxParamW(instance,MAKEINTRESOURCEW(IDD_MAIN_DIALOG),NULL,dialog_proc,0);
    close_worker(); if(context.hMutex)CloseHandle(context.hMutex);
    FreeLibrary(rich); return (int)result;
}
