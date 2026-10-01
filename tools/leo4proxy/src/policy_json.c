#include "policy_json.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

static void whitespace(const char* s, int len, int* p) {
    while (*p < len && (s[*p]==' ' || s[*p]=='\t' || s[*p]=='\r' || s[*p]=='\n')) ++*p;
}
static int hex(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static int value(PolicyJson* j, int len, int* p, int depth) {
    whitespace(j->text,len,p);
    if (*p>=len || depth>16 || j->count>=512) return -1;
    int n=j->count++;
    PolicyJsonToken* t=&j->tokens[n];
    t->start=*p; t->type=j->text[*p];
    const char* s=j->text;
    if (t->type=='{' || t->type=='[') {
        char end=t->type=='{' ? '}' : ']'; ++*p; whitespace(s,len,p);
        if (*p<len && s[*p]==end) ++*p;
        else for (;;) {
            if (t->type=='{') {
                whitespace(s,len,p);
                if (*p>=len || s[*p]!='"' || value(j,len,p,depth+1)<0) return -1;
                whitespace(s,len,p);
                if (*p>=len || s[(*p)++]!=':') return -1;
            }
            if (value(j,len,p,depth+1)<0) return -1;
            whitespace(s,len,p);
            if (*p>=len) return -1;
            char c=s[(*p)++];
            if (c==end) break;
            if (c!=',') return -1;
        }
    } else if (t->type=='"') {
        ++*p;
        while (*p<len && s[*p]!='"') {
            unsigned char c=(unsigned char)s[(*p)++];
            if (c<32) return -1;
            if (c=='\\') {
                if (*p>=len) return -1;
                c=(unsigned char)s[(*p)++];
                if (c=='u') {
                    for (int k=0;k<4;k++) if (*p>=len || hex(s[(*p)++])<0) return -1;
                } else if (!strchr("\"\\/bfnrt",c)) return -1;
            }
        }
        if (*p>=len) return -1;
        ++*p;
    } else if (t->type=='t' || t->type=='f' || t->type=='n') {
        const char* word=t->type=='t'?"true":t->type=='f'?"false":"null";
        int length=(int)strlen(word);
        if (len-*p<length || memcmp(s+*p,word,(size_t)length)) return -1;
        *p+=length;
    } else {
        t->type='0';
        if (s[*p]=='-') ++*p;
        if (*p>=len) return -1;
        if (s[*p]=='0') ++*p;
        else {
            if (s[*p]<'1' || s[*p]>'9') return -1;
            while (*p<len && s[*p]>='0' && s[*p]<='9') ++*p;
        }
        if (*p<len && s[*p]=='.') {
            ++*p; int first=*p;
            while (*p<len && s[*p]>='0' && s[*p]<='9') ++*p;
            if (*p==first) return -1;
        }
        if (*p<len && (s[*p]=='e' || s[*p]=='E')) {
            ++*p;
            if (*p<len && (s[*p]=='+' || s[*p]=='-')) ++*p;
            int first=*p;
            while (*p<len && s[*p]>='0' && s[*p]<='9') ++*p;
            if (*p==first) return -1;
        }
    }
    t->end=*p; t->next=j->count;
    return n;
}
bool policy_json_string(const PolicyJson* j, int n, char* out, size_t size) {
    if (n<0 || n>=j->count || j->tokens[n].type!='"' || !size) return false;
    size_t used=0;
    for (int p=j->tokens[n].start+1;p<j->tokens[n].end-1;p++) {
        unsigned char c=(unsigned char)j->text[p];
        if (c=='\\') {
            c=(unsigned char)j->text[++p];
            if (c=='u') {
                unsigned int code=0;
                for (int k=0;k<4;k++) code=code*16+(unsigned int)hex(j->text[++p]);
                /* Identity and fact codes are ASCII; do not silently truncate Unicode. */
                if (!code || code>127) return false;
                c=(unsigned char)code;
            } else if (c=='b') c='\b'; else if (c=='f') c='\f';
            else if (c=='n') c='\n'; else if (c=='r') c='\r'; else if (c=='t') c='\t';
        }
        if (!c || used+1>=size) return false;
        out[used++]=(char)c;
    }
    out[used]=0; return true;
}
bool policy_json_parse(PolicyJson* j, const char* s, size_t len) {
    if (!s || !len || len>16384 || memchr(s,0,len)) return false;
    memset(j,0,sizeof(*j)); j->text=s; int p=0;
    if (value(j,(int)len,&p,0)!=0) return false;
    whitespace(s,(int)len,&p);
    if (p!=(int)len || j->tokens[0].type!='{') return false;
    for (int n=0;n<j->count;n++) if (j->tokens[n].type=='{') {
        for (int a=n+1;a<j->tokens[n].next;) {
            char key[256];
            if (!policy_json_string(j,a,key,sizeof(key))) return false;
            int after=j->tokens[a+1].next;
            for (int b=after;b<j->tokens[n].next;b=j->tokens[b+1].next) {
                char other[256];
                if (!policy_json_string(j,b,other,sizeof(other)) || !strcmp(key,other)) return false;
            }
            a=after;
        }
    }
    return true;
}
int policy_json_field(const PolicyJson* j, int n, const char* key) {
    if (n<0 || n>=j->count || j->tokens[n].type!='{') return -1;
    for (int a=n+1;a<j->tokens[n].next;a=j->tokens[a+1].next) {
        char found[256];
        if (policy_json_string(j,a,found,sizeof(found)) && !strcmp(found,key)) return a+1;
    }
    return -1;
}
bool policy_json_bool(const PolicyJson* j, int n, bool* out) {
    if (n<0 || n>=j->count) return false;
    if (j->tokens[n].type!='t' && j->tokens[n].type!='f') return false;
    *out=j->tokens[n].type=='t'; return true;
}
bool policy_json_uint(const PolicyJson* j, int n, unsigned long long* out) {
    if (n<0 || n>=j->count || j->tokens[n].type!='0') return false;
    int len=j->tokens[n].end-j->tokens[n].start;
    if (len<1 || len>20) return false;
    char number[21]; memcpy(number,j->text+j->tokens[n].start,(size_t)len); number[len]=0;
    for (int k=0;k<len;k++) if (number[k]<'0' || number[k]>'9') return false;
    errno=0; char* end=NULL; *out=strtoull(number,&end,10);
    return errno!=ERANGE && end && !*end;
}
