/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void echo(int connfd);
void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }


  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    //repeatedly accept connection request
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    // performing a transaction
    doit(connfd);   // line:netp:tiny:doit
    echo(connfd);
    // closing its end of the connection
    Close(connfd);  // line:netp:tiny:close
  }
}

void echo(int connfd)
{
  size_t n;
  char buf[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, connfd);
  while ((n = Rio_readlineb(&rio, buf, MAXLINE))!=0){
    printf("server received %d bytes\n", (int)n);
    Rio_writen(connfd, buf, n);
  }
}

/*
 * doit - handles one HTTP transaction
 */
void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  int n;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  n = Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  // Rio_writen(fd, buf, n);
  if (n!=0) printf("server received %d bytes\n\n", (int)n );

  sscanf(buf, "%s %s %s", method, uri, version);  // parse request lines
  // Supports only the GET method - if client requests another method, ERROR
  // send error message and return to the main routine
  // then closes the connection and await next connection request
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented",
                "Tiny does not implement this method");
    return;
  }
  // read and ignore any request headers
  read_requesthdrs(&rio);

  /* Parse URI from GET request */
  // parse the URI into filename and possibly empty CGI arg string
  // set flag that indicates whether the request is static or dynamic
  is_static = parse_uri(uri, filename, cgiargs);
  // if the file does not exist on disk, ERROR
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found",
                "Tiny couldn't find this file");
    return;
  }


  if (is_static) {  /*serve static content */
  // verify if the file is executable
  // - that the file is a regular file and we have read permission ?????
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))  {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't read the file");
      return;
    }
    // if it is executable, serve static content to the client
    serve_static(fd, filename, sbuf.st_size);
  }
  else {  /* Serve dynamic content */
  //verify if the file is executable
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))  {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't run the CGI program");
      return;
    }
    // serve the dynamic content to the client
    serve_dynamic(fd, filename, cgiargs);
  }
}


/*
 * clienterror - sends an HTTP response to the client with status code and 
 *               status message in the response line along with HTML file in body
 *               that explains error
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* buld the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n",body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n",body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-lenght: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/*
 * read_requesthdrs - read the info in request headers
 */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];
  int n;

  Rio_readlineb(rp, buf, MAXLINE);
  // check the empty text line (\r - carriage return, \n line feed)
  // that terminates request headers
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/*
 * parse_uri - parses URI into a filename and optional CGI argrment string
 */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  // if the request is for static content
  if (!strstr(uri, "cgi-bin"))  {     /* Static content */
    // clear CGI arguement string
    strcpy(cgiargs, "");
    // convert URI into a relative Linux pathname
    strcpy(filename, ".");
    strcat(filename, uri);
    // if URI ends with / character, append the default filename
    if (uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }

  // if  the request is for dynamic content
  else {    /* Dynamic content */
    // extract CGI arguments
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs,ptr+1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    // convert the remaining portion of the URI to a relative Linux filename
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

/*
 * serve_static - sends an HTTP response whose body contains the contents of a local file
 */
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* send response headers to client */
  // determine file type by inspecting the suffix in the filename
  get_filetype(filename,filetype);
  // send the response line and response headers to the client
  sprintf(buf, "HTTP/1.0 200 OK \r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers: \n");
  printf("%s",buf);

  /* send response body to client */
  // opens filename for reading and get its file descriptor
  srcfd = Open(filename, O_RDONLY, 0);
  // Linux mmap function maps the requested file to a virtual memory area
  //    *call to mmap maps the first filesize bytes of file srcfd to a private
  //     read-only area of virtual memory that starts at address srcp
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  
  srcp = Malloc(filesize);
  Rio_readn(srcfd, srcp, filesize);
  // after mapping, don't need its descriptor
  // close the file - if fail, fatal memory leak
  Close(srcfd);
  // transfer of the file to the client
  //    rio_writen copies the filesize bytes starting at location srcp to the
  //    client connected descriptor
  Rio_writen(fd, srcp, filesize);
  // frees the mapped virtual memory area
  // Munmap(srcp, filesize);
  Free(srcp);
}

/*
 * get_filetype - determine file type by inspecting the suffix in the filename
 */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename,".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if(strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if(strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else
    strcpy(filetype, "text/plain");
}

/*
 * serve_dynamic - serve dynamic content by forking a child process and then
 *                 running a CGI program in the context of the child
 */
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[]={NULL};

  /* return first part of HTTP response */
  // send response line indeicating success to the client
  // along with an informational Server header
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // fork a new child proces
  if (Fork() == 0) { /* Child */
    /* real server would set all CGI vars here */
    // initialize the QUERY_STRING environment variable with
    // the CGI arguemnts from the request URI
    setenv("QUERY_STRING", cgiargs, 1);
    // redirects the child's standard output to the connected file descriptor
    Dup2(fd, STDOUT_FILENO);                /* redirect stdout to client */
    // loads and runs the CGI program
    Execve(filename, emptylist, environ);   /* run cgi program */
  }
  // parent wait to reap the child when the child terminates
  Wait(NULL);
}
