#include "web/web_http.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
/* RFC 9110 If-None-Match uses weak comparison; lists stay in bounded storage. */
int web_http_etag_match(const char *value,const char *digest){const char*p=value,*start;int matched=0;
 while(*p==' '||*p=='\t')p++;
 if(*p=='*'){p++;while(*p==' '||*p=='\t')p++;return *p?-1:1;}
 while(*p){
  while(*p==' '||*p=='\t'||*p==',')p++;if(!*p)break;
  if(p[0]=='W'&&p[1]=='/')p+=2;if(*p++!='"')return -1;start=p;
  while(*p&&*p!='"'){if((unsigned char)*p<0x21||*p==0x7f)return -1;p++;}
  if(*p!='"')return -1;if((size_t)(p-start)==strlen(digest)&&!memcmp(start,digest,(size_t)(p-start)))matched=1;
  p++;while(*p==' '||*p=='\t')p++;if(*p&&*p!=',')return -1;
 }
 return matched;
}
static int cookie_value(const char *p,int tls,char *out){unsigned int found=0;const char*key=tls?"session=":"session_http=";size_t keylen=strlen(key);
 if(strlen(p)>160)return -1;
 while(*p){const char*end=strchr(p,';');size_t n=end?(size_t)(end-p):strlen(p);while(*p==' '){p++;n--;}
  if(n>=keylen&&!memcmp(p,key,keylen)){if(found++||n!=keylen+64)return -1;memcpy(out,p+keylen,64);out[64]=0;}
  else if(!((n==72&&!memcmp(p,"session=",8))||(n==77&&!memcmp(p,"session_http=",13))))return -1;
  if(!end)break;p=end+1;
 }return 0;
}
int web_http_parse(char *b,size_t n,const char *authority,int tls,web_http_t *out){char *end,*line,*next,*colon;size_t header;unsigned int length=0,have_length=0,host=0,origin=0,cookie=0,csrf=0,connection=0,etag=0;char expected[96],*space,*version;memset(out,0,sizeof(*out));if(n>WEB_HTTP_MAX||memchr(b,0,n))return -400;b[n]=0;end=strstr(b,"\r\n\r\n");if(!end)return n>=4096?-431:0;header=(size_t)(end-b)+4;if(header>4096)return -431;
 /* Parse a bounded copy so an incomplete body may be fed again. */
 {char h[4097];memcpy(h,b,header);h[header]=0;line=strstr(h,"\r\n");if(!line)return -400;*line=0;space=strchr(h,' ');if(!space)return -400;*space++=0;version=strchr(space,' ');if(!version)return -400;*version++=0;if(strcmp(version,"HTTP/1.1")||strlen(h)>7||strlen(space)>63||space[0]!='/')return -400;strcpy(out->method,h);strcpy(out->path,space);if(strcmp(h,"GET")&&strcmp(h,"POST")&&strcmp(h,"HEAD"))return -405;out->write=!strcmp(h,"POST");snprintf(expected,sizeof(expected),"%s://%s",tls?"https":"http",authority);line+=2;
  while(*line){next=strstr(line,"\r\n");if(!next)return -400;*next=0;if(!*line)break;colon=strchr(line,':');if(!colon||*line==' '||*line=='\t')return -400;*colon++=0;while(*colon==' '||*colon=='\t')++colon;
   if(!strcasecmp(line,"Host")){if(host++||strcmp(colon,authority))return -400;}
   else if(!strcasecmp(line,"Connection")){if(connection++|| (strcasecmp(colon,"close")&&strcasecmp(colon,"keep-alive")))return -400;out->close=!strcasecmp(colon,"close");}
   else if(!strcasecmp(line,"If-None-Match")){if(etag++||strlen(colon)>=sizeof(out->etag))return -400;strcpy(out->etag,colon);}
   else if(!strcasecmp(line,"Origin")){if(origin++||strcmp(colon,expected))return -403;}
   else if(!strcasecmp(line,"Content-Length")){if(have_length++||web_number(colon,WEB_BODY_MAX,&length))return -400;}
   else if(!strcasecmp(line,"Transfer-Encoding")||!strcasecmp(line,"Expect"))return -400;
   else if(!strcasecmp(line,"Cookie")){if(cookie++||cookie_value(colon,tls,out->session))return -400;}
   else if(!strcasecmp(line,"X-CSRF-Token")){if(csrf++||strlen(colon)!=64)return -400;strcpy(out->csrf,colon);}
   else if(!strcasecmp(line,"Content-Type")&&strcmp(colon,"text/plain;charset=UTF-8")&&strcmp(colon,"text/plain"))return -415;
   line=next+2;
  }
 }
 if(!host)return -400;if(out->write&&(!origin||!have_length))return -403;if(!out->write&&length)return -400;if(n<header+length)return 0;if(n!=header+length)return -400;out->body=b+header;out->length=length;return 1;}
int web_http_ipc(const web_http_t *r,char *out,size_t n){const char *op=NULL;size_t at;web_fields_t f;if(!strcmp(r->path,"/api/hello")&&!r->write)op="hello";else if(!strncmp(r->path,"/api/",5)){const char *p=r->path+5;const char *reads[]={"help","detail1","detail2","detail3","detail4","detail5","detail6","detail7","detail8","overview","ports","network","diagnostics","system","about",NULL};const char *writes[]={"recover","enroll","login","logout","password","port","network_review","network_apply","keep","revert","web","backlight","ntp","ntptest","time","gateway_stop",NULL};const char **list=r->write?writes:reads;while(*list){if(!strcmp(p,*list)){op=*list;break;}++list;}}
 if(!op)return -404;if(web_fields_parse(&f,r->body,r->length))return -400;if(*web_field(&f,"op")||*web_field(&f,"session")||*web_field(&f,"csrf"))return -400;
 at=(size_t)snprintf(out,n,"op=%s\n",op);if(strcmp(op,"hello")&&strcmp(op,"login")&&strcmp(op,"enroll")&&strcmp(op,"recover")){if(!r->session[0])return -401;at+=(size_t)snprintf(out+at,n-at,"session=%s\n",r->session);if(r->write){if(!r->csrf[0])return -403;at+=(size_t)snprintf(out+at,n-at,"csrf=%s\n",r->csrf);}}
 if(at+r->length>=n)return -413;memcpy(out+at,r->body,r->length);out[at+r->length]=0;return 0;}
