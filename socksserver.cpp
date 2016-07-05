#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <iostream>
#include <sys/time.h>

#define SOCKS4_REQUEST_SIZE 200
#define READ_BUFFER_SIZE 4096

using namespace std;

map<string,string> env;
int server_sockfd;
int connection_count = 0;
string server_ip;
int server_port;
bool config_file;
string network_segment;


void sig_int(int sig)
{
    shutdown( server_sockfd, SHUT_RDWR);
    close(server_sockfd);
    exit(0);
}

void reaper( int signo)
{
    int status;

    while( waitpid( -1, &status, WNOHANG)>0 )
    {
        --connection_count;
    }

    return;
}

void reject_and_exit( int client_sockfd, unsigned char reject_package[8])
{
    printf("Request reject\n");
    write( client_sockfd, reject_package, 8);     //reply 돌 client
    shutdown( client_sockfd, SHUT_RDWR);
    close(client_sockfd);
    exit(0);
}

void connect_request( int client_sockfd, int connect_sockfd)
{
    fd_set rfds;
    fd_set rs;
    int nfds;

    FD_ZERO(&rfds);
    FD_ZERO(&rs);

    FD_SET(client_sockfd,&rs);
    nfds = max( nfds, client_sockfd);

    FD_SET(connect_sockfd,&rs);
    nfds = max( nfds, connect_sockfd);

    char bufferread[READ_BUFFER_SIZE];
    int numread;

    while(true)
    {
        memcpy( &rfds, &rs, sizeof(rfds));

        struct timeval timev;
        timev.tv_sec = 120;
        timev.tv_usec = 0;
        if( select( nfds+1, &rfds, (fd_set*)0, (fd_set*)0, &timev) < 0 )
        {
            fprintf(stderr,"timeout or select error\n");
            break;
        }

        //client's data is ready
        if( FD_ISSET( client_sockfd, &rfds) )
        {

            if( (numread = read( client_sockfd, bufferread, READ_BUFFER_SIZE)) > 0 )
            {
                //write to connect_sockfd error or closed
                if( write( connect_sockfd, bufferread, numread) != numread )
                {
                    break;
                }
            }
            //client_sockfd error or closed
            else
            {
                break;
            }
        }
        //destination host's data is ready
        else if( FD_ISSET( connect_sockfd, &rfds) )
        {
            if( (numread = read( connect_sockfd, bufferread, READ_BUFFER_SIZE)) > 0 )
            {
                //write to client_sockfd error or closed
                if( write( client_sockfd, bufferread, numread) != numread )
                {
                    break;
                }
            }
            //connect_sockfd error or closed
            else
            {
                break;
            }
        }
    }
    shutdown(client_sockfd,SHUT_RDWR);
    shutdown(connect_sockfd,SHUT_RDWR);
    close(client_sockfd);
    close(connect_sockfd);
    exit(0);
}

void bind_request( int client_sockfd, int bind_newsockfd)
{
    fd_set rfds;
    fd_set rs;
    int nfds;

    FD_ZERO(&rfds);
    FD_ZERO(&rs);

    FD_SET( client_sockfd, &rs);
    nfds = max( nfds, client_sockfd);

    FD_SET( bind_newsockfd, &rs);
    nfds = max( nfds, bind_newsockfd);

    char bufferread[READ_BUFFER_SIZE];
    int numread;

    while(true)
    {
        memcpy( &rfds, &rs, sizeof(rfds));

        struct timeval timev;
        timev.tv_sec = 120;
        timev.tv_usec = 0;
        if( select( nfds+1, &rfds, (fd_set*)0, (fd_set*)0, &timev) < 0 )
        {
            fprintf( stderr, "timeout or select error\n");
            break;
        }

        //client's data is ready
        if( FD_ISSET( client_sockfd, &rfds) )
        {
            if( (numread = read( client_sockfd, bufferread, READ_BUFFER_SIZE)) > 0 )
            {
                //write to bind_newsockfd error or closed
                if( write( bind_newsockfd, bufferread, numread) != numread )
                {
                    break;
                }
            }
            //client_sockfd error or closed
            else
            {
                break;
            }
        }
        //destination host's data is ready
        else if( FD_ISSET( bind_newsockfd, &rfds) )
        {
            if( (numread = read( bind_newsockfd, bufferread, READ_BUFFER_SIZE)) > 0 )
            {
                //write to client_sockfd error or closed
                if( write( client_sockfd, bufferread, numread) != numread )
                {
                    break;
                }
            }
            //bind_newsockfd error or closed
            else
            {
                break;
            }
            ;
        }
    }
    shutdown(client_sockfd,SHUT_RDWR);
    shutdown(bind_newsockfd,SHUT_RDWR);
    close(client_sockfd);
    close(bind_newsockfd);
    exit(0);
}

void client_connection( int client_sockfd, struct sockaddr_in &cli_addr)
{

    unsigned char read_buffer[SOCKS4_REQUEST_SIZE];
    read( client_sockfd, read_buffer, SOCKS4_REQUEST_SIZE);

    unsigned char vn = read_buffer[0];
    unsigned char cd = read_buffer[1];
    unsigned int dst_port = (read_buffer[2]<<8) | read_buffer[3];
    unsigned int dst_ip = (read_buffer[4]<<24) | (read_buffer[5]<<16) | (read_buffer[6]<<8) | read_buffer[7];
    char *user_id = (char*)read_buffer + 8;
    char *domain_name = user_id + strlen(user_id)+1;
    struct hostent *dst_host_struct;
    struct in_addr **dst_addr_list;

    int dst_ip_hostForm;

    //use domain name to get IP address
    if( read_buffer[4]==0 && read_buffer[5]==0 && read_buffer[6]==0 )
    {
        dst_host_struct = gethostbyname(domain_name);
        dst_addr_list = (struct in_addr **)dst_host_struct->h_addr_list;

        dst_ip_hostForm = dst_addr_list[0]->s_addr;
    }
    else
    {
        dst_ip_hostForm = ntohl(dst_ip);
    }
    string dst_ip_str = inet_ntoa(*(struct in_addr*)&dst_ip_hostForm);

    cout << "\nConnection NUM: " << connection_count << endl;
    printf("VN: %u, CD: %u, DST IP: %s, DST PORT: %u, USERID: %s\n",vn,cd, dst_ip_str.c_str(),dst_port,user_id);


    if( config_file==true )
    {
        cout << "Only Allow: " << "[" << network_segment << "]\n";
    }

    unsigned char reject_package[8];

    //reject IP and port no. of destination which is the same as socks server's.
    //reject IP and port no. of destination which is not involved in socks.conf
    if( ( inet_addr(server_ip.c_str())==dst_ip_hostForm && dst_port==server_port ) ||
       (config_file==true && strncmp( network_segment.c_str(), dst_ip_str.c_str(), network_segment.size())!=0) )
    {
        printf("Request reject\n");

        reject_package[0] = 0;
        reject_package[1] = 91;
        reject_package[2] = dst_port / 256;
        reject_package[3] = dst_port % 256;
        reject_package[4] = dst_ip >> 24;
        reject_package[5] = (dst_ip >> 16) & 0xFF;
        reject_package[6] = (dst_ip >> 8) & 0xFF;
        reject_package[7] = dst_ip & 0xFF;

        reject_and_exit( client_sockfd, reject_package);
    }

    printf("Permit Src = %s(%u), ",inet_ntoa(cli_addr.sin_addr),cli_addr.sin_port);
    printf("Dst = %s(%u)\n",inet_ntoa(*(struct in_addr*)&dst_ip_hostForm),dst_port);

    if( cd==1 )   //connect
    {
        printf("SOCKS_CONNECT GRANTED ...\n");

        int connect_sockfd;
        struct sockaddr_in connect_serv_addr;

        bzero((char*)&connect_serv_addr,sizeof(connect_serv_addr));
        connect_serv_addr.sin_family = AF_INET;
        connect_serv_addr.sin_addr.s_addr = dst_ip_hostForm;
        connect_serv_addr.sin_port = htons(dst_port);

        if( (connect_sockfd = socket(AF_INET,SOCK_STREAM,0))<0 )
        {
            fprintf(stderr,"connect: can't open datagram socket\n");
            reject_and_exit( client_sockfd, reject_package);
        }
        if( connect(connect_sockfd,(struct sockaddr*)&connect_serv_addr,sizeof(connect_serv_addr))<0 )
        {
            fprintf(stderr,"connect: can't connect to server\n");
            reject_and_exit( client_sockfd, reject_package);
        }

        unsigned char package[8];
        package[0] = 0;
        package[1] = 90;
        package[2] = dst_port / 256;
        package[3] = dst_port % 256;
        package[4] = dst_ip >> 24;
        package[5] = (dst_ip >> 16) & 0xFF;
        package[6] = (dst_ip >> 8) & 0xFF;
        package[7] = dst_ip & 0xFF;
        write(client_sockfd,package,8);     //reply 돌 client

        connect_request( client_sockfd, connect_sockfd);
    }
    else if( cd==2 )  //bind
    {
        printf("SOCKS_BIND GRANTED ...\n");

        int bind_sockfd, bind_newsockfd, bind_clilen, bind_servlen;
        struct sockaddr_in bind_cli_addr, bind_serv_addr;

        if( (bind_sockfd = socket(AF_INET,SOCK_STREAM,0)) < 0 )
        {
            fprintf(stderr,"bind: can't open datagram socket\n");
            reject_and_exit( client_sockfd, reject_package);
        }
        bzero( (char*)&bind_serv_addr, sizeof(bind_serv_addr));
        bind_serv_addr.sin_family = AF_INET;
        bind_serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        bind_serv_addr.sin_port = htons(0);

        if( bind( bind_sockfd, (struct sockaddr*)&bind_serv_addr, sizeof(bind_serv_addr)) < 0 )
        {
            fprintf(stderr,"bind: can't bind local address\n");
            reject_and_exit( client_sockfd, reject_package);
        }
        bind_servlen = sizeof(bind_serv_addr);
        getsockname( bind_sockfd, (struct sockaddr*)&bind_serv_addr, (socklen_t*)&bind_servlen);
        unsigned short unuesd_port = htons(bind_serv_addr.sin_port);  //get unused port determined by system.

        listen(bind_sockfd,30);

        unsigned char package[8];
        package[0] = 0;
        package[1] = 90;
        package[2] = unuesd_port / 256;
        package[3] = unuesd_port % 256;
        package[4] = 0;
        package[5] = 0;
        package[6] = 0;
        package[7] = 0;

        write(client_sockfd,package,8);     //reply 돌 client

        bind_clilen = sizeof(bind_cli_addr);
        bind_newsockfd = accept( bind_sockfd, (struct sockaddr*)&bind_cli_addr, (socklen_t*)&bind_clilen);
        if( bind_newsockfd<0 )
        {
            fprintf(stderr,"bind: accept error\n");
            reject_and_exit( client_sockfd, reject_package);
        }
        write(client_sockfd,package,8);     //reply 돌 client

        bind_request( client_sockfd, bind_newsockfd);
    }

}

string get_local_IP()
{
    struct ifaddrs * ifAddrStruct=NULL;
    void * tmpAddrPtr=NULL;

    getifaddrs(&ifAddrStruct);

    while( ifAddrStruct!=NULL )
    {
        if( ifAddrStruct->ifa_addr->sa_family==AF_INET )
        { // check it is IP4
          // is a valid IP4 Address
            tmpAddrPtr = &((struct sockaddr_in *)ifAddrStruct->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop( AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            if( strcmp( addressBuffer, "127.0.0.1")!=0 )
            {
                return string(addressBuffer);
            }
        }
        ifAddrStruct=ifAddrStruct->ifa_next;
    }

}

bool read_configfile( string &network_segment)
{
    char str[100];
    int configfile_flag = 0;
    char config[100];
    FILE *configfile_fd = fopen("socks.conf","r");

    if( configfile_fd==NULL )
    {
        return false;
    }

    fgets(str,100,configfile_fd);
    str[strlen(str)-1] = '\0';


    if(strncmp("ON",str,2)==0)
    {
        configfile_flag = 1;
        fgets(str,100,configfile_fd);
        str[strlen(str)-1] = '\0';
        if( str[strlen(str)-1]=='\r' || str[strlen(str)-1]=='\n' )
        {
            str[strlen(str)-1] = '\0';
            network_segment = str;
        }
        fclose(configfile_fd);
        return true;
    }
    else
    {
        fclose(configfile_fd);
        return false;
    }
}

int main(int argc, char* argv[], char *envp[])
{
    struct sockaddr_in serv_addr;

    server_ip = get_local_IP();
    server_port = atoi(argv[1]);

    config_file = read_configfile(network_segment);

    signal( SIGINT, sig_int);
    signal( SIGCHLD, reaper);

    if( (server_sockfd = socket(AF_INET,SOCK_STREAM,0))<0 )
    {
        fprintf(stderr,"server: can't open datagram socket\n");
        exit(EXIT_FAILURE);
    }
    bzero((char*)&serv_addr,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(server_port);

    if( bind( server_sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))<0 )
    {
        fprintf(stderr,"server: can't bind local address\n");
        exit(EXIT_FAILURE);
    }
    listen( server_sockfd, 30);

    while(true)
    {
        int childpid;
        struct sockaddr_in cli_addr;
        int clilen = sizeof(cli_addr);

        int newsockfd = accept( server_sockfd, (struct sockaddr*)&cli_addr, (socklen_t*)&clilen);
        if( newsockfd<0 )
        {
            fprintf(stderr,"server: accept error\n");
            continue;
        }
        else
        {
            ++connection_count;
        }

        if( (childpid = fork())<0 )
        {
            fprintf(stderr,"server: fork error\n");
            --connection_count;
        }
        else if( childpid==0 )
        {
            close(server_sockfd);
            client_connection( newsockfd, cli_addr);
        }
        else
        {
            close(newsockfd);
        }

    }


    return 0;
}

