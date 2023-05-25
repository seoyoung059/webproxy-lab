#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *Accept_hdr = "    Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";

void *thread(void *vargp);

int main(int argc, char **argv) {
  // main 함수에서는 client와 connect하고, doit 함수를 실행하여 요청을 받는다.
  int listenfd, clientfd;
  char clientname[MAXLINE], clientport[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, clientname, MAXLINE, clientport, MAXLINE,0);
    printf("Accepted connection from (%s, %s)\n", clientname, clientport);

    // doit(clientfd);
    // Close(clientfd);
    Pthread_create(&tid, NULL, thread, &clientfd);
  }
  return 0;
}


void *thread(void *vargp){
  int clientfd = *((int *)vargp);
  Pthread_detach((pthread_self()));
  doit(clientfd);
  Close(clientfd);
}

void doit(int client_fd){
  rio_t client_rio, server_rio;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char servername[MAXLINE], serverpath[MAXLINE], serverhdr[MAXLINE];
  int server_fd;
  int port=80;

  // 1.클라이언트와의 fd를 클라이언트용 rio에 연결(Rio_readinitb)한다.
  Rio_readinitb(&client_rio, client_fd);

  // 2.클라이언트의 요청을 한줄 읽어들여서(Rio_readlineb) 메서드와 URI, HTTP 버전을 얻고,
  // URI에서 목적지 호스트와 포트를 뽑아낸다.
  //요청 헤더 첫 줄(Method, URI, HTTP version)
  Rio_readlineb(&client_rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);

  // 501 요청 처리
  if (strcasecmp(method,"GET") && strcasecmp(method,"HEAD")) {     
      printf("[PROXY]501 ERROR\n");
      clienterror(client_fd, method, "501", "잘못된 요청",
              "501 에러. 올바른 요청이 아닙니다.");
      return;
  } 

  // URI로부터 서버명, 서버 경로, 포트를 분리
  parse_uri(uri, servername, serverpath, &port);

  


  // 3.목적지 호스트와 포트를 가지고 서버용 fd를 생성하고, 서버용 rio에 연결(Rio_readinitb)한다.
  // HTTP 지정포트인 80번 포트로 지정
  char port_value[100];
  sprintf(port_value,"%d",port);
  server_fd = Open_clientfd(servername, port_value); 
  Rio_readinitb(&server_rio, server_fd);
  printf("server connection: (%s, %s)\n", servername, "80");

  // 4.클라이언트가 보낸 첫줄을 이미 읽어 유실되었고, HTTP 버전을 바꾸거나 추가 헤더를 붙일
  // 필요가 있으므로, 클라이언트가 보내는 메시지를 한줄씩 읽어들이면서(Rio_readlineb) 재조합하여
  // 서버에 보낼 HTTP 요청메시지를 새로 생성해준다.
  if (!make_request(&client_rio, servername, serverpath, port, serverhdr, method)) {
    clienterror(client_fd, method, "501", "request header error",
            "Request header is wrong");      
  }
  
  // 5.서버에 요청메시지를 보낸다.(Rio_writen)
  Rio_writen(server_fd, serverhdr, strlen(serverhdr));
  
  size_t n;
  // 6.서버 응답이 오면 클라이언트에게 전달한다. (Rio_readnb, Rio_writen)
  while ((n = Rio_readlineb(&server_rio, buf,MAXLINE)) >  0){
    Rio_writen(client_fd,buf, n);
  }
  Close(server_fd);

}

void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void parse_uri(char *uri,char *hostname, char *path, int *port) {
  /*
   uri가  
   / , /cgi-bin/adder 이렇게 들어올 수도 있고,
   http://11.22.33.44:5001/home.html 이렇게 들어올 수도 있다.

   알맞게 파싱해서 hostname, port로, path 나누어주어야 한다!
  */

  *port = 80;

  printf("uri=%s\n", uri);
  
  // '//' 뒤에서부터 서버명, 서버 경로, 포트 추출
  char *parsed;
  parsed = strstr(uri, "//");

  if (parsed == NULL) {
    parsed = uri;
  }
  else {
    parsed = parsed + 2;  // 포인터 두칸 이동 
  }

  // ':' 뒤에 오는 포트번호 추출
  char *parsed2 = strstr(parsed, ":");
  if (parsed2 == NULL) {
    // ':' 이후가 없다면, port가 없음
    // '/' 이후로 오는 경로 추출
    parsed2 = strstr(parsed, "/");
    if (parsed2 == NULL) {      /* 경로, 포트번호 없이 서버명만 주어진 URI*/
      sscanf(parsed,"%s",hostname);
    } 
    else {            /* 서버명과 경로가 주어진 URI 추출*/
        *parsed2 = '\0';
        sscanf(parsed,"%s",hostname);
        *parsed2 = '/';
        sscanf(parsed2,"%s",path);
    }

  } else {
      // ':' 이후가 있으므로 port가 있음
      *parsed2 = '\0';
      sscanf(parsed, "%s", hostname);
      sscanf(parsed2+1, "%d%s", port, path);
  }
  printf("hostname=%s port=%d path=%s\n", hostname, *port, path);
}


int make_request(rio_t* client_rio, char *hostname, char *path, int port, char *hdr, char *method) {
  // 프록시서버로 들어온 요청을 서버에 전달하기 위해 HTTP 헤더 생성
  char req_hdr[MAXLINE], additional_hdf[MAXLINE], host_hdr[MAXLINE];
  char buf[MAXLINE];
  char *HOST = "Host";
  char *CONN = "Connection";
  char *UA = "User-Agent";
  char *P_CONN = "Proxy-Connection";
  sprintf(req_hdr, "%s %s HTTP/1.0\r\n",method, path);

  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
    if (!strcmp(buf,"\r\n")) break;  // buf == EOL => EOF

    if (!strncasecmp(buf, HOST, strlen(HOST))) {
      // 호스트 헤더 지정
      strcpy(host_hdr, buf);
      continue;
    }

    if (strncasecmp(buf, CONN, strlen(CONN)) && strncasecmp(buf, UA, strlen(UA)) && strncasecmp(buf, P_CONN, strlen(P_CONN))) {
      // 미리 준비된 헤더가 아니면 추가 헤더에 추가 
      strcat(additional_hdf, buf);  
    }
  }

  if (!strlen(host_hdr)) {
    sprintf(host_hdr, "Host: %s\r\n", hostname);
  }

  sprintf(hdr, "%s%s%s%s%s%s\r\n", 
    req_hdr,   // METHOD URL VERSION
    host_hdr,   // Host header
    user_agent_hdr,
    Accept_hdr,
    connection_hdr,
    proxy_connection_hdr
  );
  if (strlen(hdr))
    return 1;
  return 0;
}