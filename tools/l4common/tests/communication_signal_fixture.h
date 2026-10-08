#pragma once
/* Synthetic signed-source Con proof for isolated communication fixtures only.
 * Not producer authentication or a production boot/signals adapter. */
static BYTE* fixture_communication_con(L4Journal* j,L4CommunicationPlan* p,BYTE operation[76]){
    L4ServiceSwitch s={0};L4Layout old;wchar_t image[MAX_PATH];
    CHECK(l4_layout_from_roots(&s.layout,j->layout.binaries,j->layout.data,L"1.13.3"));
    CHECK(l4_layout_from_roots(&old,j->layout.binaries,j->layout.data,L"1.13.2"));
    wcscpy_s(s.service,32,L"L4Con");s.before.installed=true;s.before.start_type=SERVICE_AUTO_START;
    wcscpy_s(s.before.account,256,L"LocalSystem");s.before_size=100;s.size=101;
    memset(s.before_sha256,1,32);memset(s.sha256,2,32);
    CHECK(l4_layout_component(&old,L"l4con",L"l4con.exe",image));swprintf_s(s.before.image_path,2048,L"\"%ls\" --fixture",image);
    CHECK(l4_layout_component(&s.layout,L"l4con",L"l4con.exe",image));swprintf_s(s.after,2048,L"\"%ls\" --fixture",image);
    CHECK(l4_journal_save_switch(j,&s,&p->con_sequence));BYTE* bytes=NULL;
    CHECK(l4_store_find_record(j,10,p->con_sequence,&bytes,&p->con_size));p->con_switch=bytes;
    p->con_pid=GetCurrentProcessId();FILETIME e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&p->con_created,&e,&k,&u));
    strcpy_s(p->thumbprint,64,"1111111111111111111111111111111111111111");
    l4_store_u64(operation+60,p->con_sequence);return bytes;
}
