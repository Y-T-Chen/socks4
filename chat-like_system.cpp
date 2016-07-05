#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <map>
#include <string>
#include <iostream>
#include <vector>
#include <list>
#include <sstream>
#include <algorithm>

using namespace std;

#define NORMAL_FILE 1
#define TEMP_FILE 2

#define NO_PIPE -1
#define OUTPUT_TO_END -2
#define OUT_TO_RANGE -3

#define MAX_USER_NUM 30
//--------------------------------------------------------------------------------------------------------------------
int file_table[MAX_USER_NUM][MAX_USER_NUM];

typedef struct id_table
{
    int sockfd;
    string name;
    string ip_port;
    map<string,string> env;
    bool used_flag;
    static int user_num;

    id_table()
    {
        sockfd = 0;
        name = "";
        ip_port = "";
        env.clear();
        used_flag = false;
    }

}ID_TABLE;
int ID_TABLE::user_num = 0;
ID_TABLE id_table1[MAX_USER_NUM];

typedef struct file_write
{
    string file_name;
    int file_kind;
    int from_id;
    int to_id;
    int file_fd;
    bool file_write_flag;

    file_write()
    {
        file_name = "";
        file_kind = 0;
        from_id = 0;
        to_id = 0;
        file_fd = 0;
        file_write_flag = false;
    }

}FILE_WRITE;

typedef struct command_struct
{
    vector<string> command;
    int read_pipe_no;
    int first_write_pipe_no;
    int second_write_pipe_no;

    command_struct()
    {
        command.clear();
        read_pipe_no = NO_PIPE;
        first_write_pipe_no = NO_PIPE;
        second_write_pipe_no = NO_PIPE;
    }

}COMMAND_STRUCT;

typedef struct pipe_struct
{
    int pipe_fd[2];
    bool open_flag;

    pipe_struct()
    {
        pipe_fd[0] = NO_PIPE;
        pipe_fd[1] = NO_PIPE;
        open_flag = false;
    }

}PIPE_STRUCT;

//--------------------------------------------------------------------------------------------------------------------

void broadcast(const char *str)
{
    for(int i=0;i<MAX_USER_NUM;++i)
    {
        if( id_table1[i].used_flag==true )
        {
            write( id_table1[i].sockfd, str, strlen(str));
        }
    }

    return;
}

bool splitcommand( int id, const string &com, vector<vector<string>> &com_mul, FILE_WRITE &file_write_struct)
{
    int i,j,k;
    int charcount;
    char *token;
    char *str = new char[com.size()];
    vector<string> tmp;

    charcount = 0;

    strcpy( str, com.c_str());

    token = strtok(str," ");
    while (token != NULL)
    {
        tmp.push_back(string(token));
        charcount++;

        token = strtok(NULL," ");
    }
    delete[] str;

    vector<string>  single_command;
    for(i=0;i<charcount;i++)
    {
        if(tmp[i][0]=='|')
        {
            com_mul.push_back(single_command);
            single_command.clear();

            if( tmp[i].size()!=1 )
            {
                single_command.push_back(string("|"));
                single_command.push_back(tmp[i].substr(1));

                com_mul.push_back(single_command);
                single_command.clear();
            }
        }
        else if(tmp[i][0]=='>')
        {
            if( tmp[i].size()==1 )
            {
                file_write_struct.file_kind = NORMAL_FILE;

                file_write_struct.file_name = tmp[i+1];
                file_write_struct.file_write_flag = true;

                ++i;  //跳過指令中的[檔案名稱]
            }
            else    //將檔名做好
            {
                int toid;
                toid = atoi(tmp[i].substr(1).c_str());

                stringstream ss;
                ss << "../tmp/" << id << "_" << toid;
                file_write_struct.file_name = ss.str();
                file_write_struct.from_id = id;
                file_write_struct.to_id = toid;

                if(file_table[toid-1][id-1]==0)
                {
                    if(id_table1[toid-1].used_flag==false)
                    {
                        char tmp1[100];

                        sprintf(tmp1,"*** Error: user #<%d> does not exist yet. ***\n",toid);
                        write(id_table1[id-1].sockfd,tmp1,strlen(tmp1));
                        return false;
                    }
                    file_write_struct.file_kind = TEMP_FILE;
                    file_write_struct.file_write_flag = true;

                    file_table[toid-1][id-1] = 1;
                }
                else
                {
                    char tmp1[100];

                    sprintf(tmp1,"*** Error: the pipe #%d->#%d already exists. ***\n",id,toid);
                    write(id_table1[id-1].sockfd,tmp1,strlen(tmp1));

                    return false;
                }
            }
        }
        else
        {
            single_command.push_back(tmp[i]);
        }

    }
    com_mul.push_back(single_command);

    return true;
}

int readline( int fd, string &input_str)
{
    int n,rc;
    char c;

    for(n=1;n<=input_str.max_size();n++)
    {
        if( (rc = read(fd,&c,1))==1 )
        {
            if( c=='\n' )
            {
                break;
            }
            else
            {
                input_str += c;
            }
        }
        else if(rc==0)
        {
            if(n==1)
            {
                return 0;
            }
            else
            {
                break;
            }
        }
        else
        {
            return -1;
        }
    }

    return n;
}

int fileornot( const vector<COMMAND_STRUCT> &cmd_struct,int sockfd)
{
    int i,j;
    DIR *dir;
    struct dirent *ptr;
    char path[50][50];
    char str[500];
    char *token;
    int path_count;
    int flag;
    int error;

    strcpy(str,getenv("PATH"));

    path_count = 0;
    token = strtok(str,":");
    while (token != NULL)
    {
        strcpy(path[path_count],token);
        path_count++;

        token = strtok(NULL," ");
    }

//    for(j=0;j<path_count;j++)
//    {
//        printf("%s\n",path[j]);
//    }

    error = 0;      //有無不存在檔案
    for(i=0;i<cmd_struct.size();i++)    //跑每個指令com_mul[i][0]
    {

        if( cmd_struct[i].command[0][0]=='|' )
        {
            continue;
        }
        flag = 0;

        for(j=0;j<path_count;j++)   //跑每個資料夾path[j]
        {

            dir = opendir(path[j]);

            while( (ptr = readdir(dir))!=NULL )     //跑path[j]資料夾裡的每個檔案
            {
                if(strcmp(cmd_struct[i].command[0].c_str(),ptr->d_name)==0)
                {
                    flag = 1;
                    break;
                }
            }
            closedir(dir);

            if(flag==1)     //有找到指令com_mul[i][0]
            {               //若沒有找到則繼續找下一個資料夾
                break;
            }

        }

        if(flag==0)     //跑完所有資料夾裡的每個檔案都沒發現指令com_mul[i][0]
        {
            char tmp[100];
            error = 1;
            sprintf(tmp,"Unknow command: [%s].\n",cmd_struct[i].command[0].c_str());
            write(sockfd,tmp,strlen(tmp));
        }
    }
    return error;
}

int find_dst_pipe_no( int cmd_no, int pipe_step, vector<vector<string>> &command_mul)
{
    int step_count = 0;
    for(int i=cmd_no+1;i<command_mul.size();++i)
    {
        if( i<command_mul.size() && command_mul[i][0][0] == '|' )
        {
            continue;
        }
        else
        {
            ++step_count;
        }

        if( step_count==pipe_step )
        {
            if( i==command_mul.size()-1 )
            {
                return OUTPUT_TO_END;
            }
            else
            {
                if( command_mul[i+1][0][0] == '|' )  //跳過 |N 前的 pipe
                {
                    return i+1;
                }
                else
                {
                    return i;
                }
            }
        }
    }

    return OUT_TO_RANGE;
}

void create_command_struct( vector<COMMAND_STRUCT> &cmd_struct, vector<vector<string>> &command_mul)
{
    for(int i=0;i<command_mul.size();++i)
    {
        COMMAND_STRUCT tmp_struct;
        tmp_struct.command = command_mul[i];
        if( i>0 )
        {
            tmp_struct.read_pipe_no = i-1;
        }
        else
        {
            tmp_struct.read_pipe_no = NO_PIPE;
        }

        if( i < command_mul.size()-1 )
        {
            tmp_struct.first_write_pipe_no = i;
        }
        else
        {
            tmp_struct.first_write_pipe_no = OUTPUT_TO_END;
        }

        if( command_mul[i][0][0]=='|' )
        {
            int pipe_step = atoi(command_mul[i][1].c_str());
            tmp_struct.second_write_pipe_no = find_dst_pipe_no( i, pipe_step, command_mul);
        }
        else
        {
            tmp_struct.second_write_pipe_no = NO_PIPE;
        }

        cmd_struct.push_back(tmp_struct);
    }

    return;
}

int cat_from_others( vector<vector<string>> &command_mul, const string &command, int id, vector<string> &broadcastStr_afterExe)
{
    int error_no = 0;
    broadcastStr_afterExe.clear();

    for(int i=0;i<command_mul.size();i++)        //將 <# 取代成要讀取的檔名
    {
        if( command_mul[i].size()>1 && command_mul[i][1][0]=='<' && command_mul[i][1].size()>1 )
        {

            char tmpid[5];
            int fromid;
            char filename[30];

            fromid = atoi(command_mul[i][1].substr(1).c_str());

            if(file_table[id-1][fromid-1]==1)
            {
                sprintf(filename,"../tmp/%s_%d",command_mul[i][1].substr(1).c_str(),id);

                command_mul[i][1] = string(filename);

                file_table[id-1][fromid-1] = 0;

                char tmp[200];

                sprintf(tmp,"*** %s (#%d) just received from %s (#%d) by '%s' ***\n",id_table1[id-1].name.c_str(),id,id_table1[fromid-1].name.c_str(),fromid,command.c_str());
                broadcastStr_afterExe.push_back(string(tmp));
            }
            else
            {
                char tmp[200];

                sprintf(tmp,"*** Error: the pipe #%d->#%d does not exist yet. ***\n",fromid,id);
                write(id_table1[id-1].sockfd,tmp,strlen(tmp));
                error_no = -1;
            }
        }
    }

    return error_no;
}

void create_pipe( int i, vector<COMMAND_STRUCT> &cmd_struct, PIPE_STRUCT *pipeStructs)
{
    if( cmd_struct[i].read_pipe_no != NO_PIPE
        && pipeStructs[cmd_struct[i].read_pipe_no].open_flag == false )
    {
        if( pipe(pipeStructs[cmd_struct[i].read_pipe_no].pipe_fd)<0 )    //creat  pipe.
        {
            fprintf( stderr, "Can not creat pipe.\n");
        }
        else
        {
            pipeStructs[cmd_struct[i].read_pipe_no].open_flag = true;
        }
    }

    if( cmd_struct[i].first_write_pipe_no != NO_PIPE
        && cmd_struct[i].first_write_pipe_no != OUTPUT_TO_END
        && pipeStructs[cmd_struct[i].first_write_pipe_no].open_flag == false )
    {
        if( pipe(pipeStructs[cmd_struct[i].first_write_pipe_no].pipe_fd)<0 )    //creat  pipe.
        {
            fprintf( stderr, "Can not creat pipe.\n");
        }
        else
        {
            pipeStructs[cmd_struct[i].first_write_pipe_no].open_flag = true;
        }
    }

    if( cmd_struct[i].second_write_pipe_no != NO_PIPE
        && cmd_struct[i].second_write_pipe_no != OUTPUT_TO_END
        && cmd_struct[i].second_write_pipe_no != OUT_TO_RANGE
        && pipeStructs[cmd_struct[i].second_write_pipe_no].open_flag == false )
    {
        if( pipe(pipeStructs[cmd_struct[i].second_write_pipe_no].pipe_fd)<0 )    //creat  pipe.
        {
            fprintf( stderr, "Can not creat pipe.\n");
        }
        else
        {
            pipeStructs[cmd_struct[i].second_write_pipe_no].open_flag = true;
        }
    }

    return;
}

void redirect_input_and_output( int i, int id, vector<COMMAND_STRUCT> &cmd_struct, PIPE_STRUCT *pipeStructs, FILE_WRITE &file_write_struct)
{
    if( cmd_struct[i].read_pipe_no != NO_PIPE )
    {
        dup2( pipeStructs[cmd_struct[i].read_pipe_no].pipe_fd[0], 0);
    }

    if( cmd_struct[i].first_write_pipe_no != NO_PIPE )
    {
        if( cmd_struct[i].first_write_pipe_no != OUTPUT_TO_END )
        {
            dup2( pipeStructs[cmd_struct[i].first_write_pipe_no].pipe_fd[1], 1);
        }
        else
        {
            if( file_write_struct.file_write_flag == true )
            {
                dup2( file_write_struct.file_fd, 1);
            }
            else
            {
                dup2( id_table1[id-1].sockfd, 1);
            }
        }
    }

    if( cmd_struct[i].second_write_pipe_no != NO_PIPE
        && cmd_struct[i].second_write_pipe_no != OUT_TO_RANGE )
    {
        if( cmd_struct[i].second_write_pipe_no != OUTPUT_TO_END )
        {
            dup2( pipeStructs[cmd_struct[i].second_write_pipe_no].pipe_fd[1], 2);
        }
        else
        {
            if( file_write_struct.file_write_flag == true )
            {
                dup2( file_write_struct.file_fd, 2);
            }
            else
            {
                dup2( id_table1[id-1].sockfd, 2);
            }
        }
    }
    else if( cmd_struct[i].second_write_pipe_no == OUT_TO_RANGE )
    {
        close(2);
    }

    return;
}

void command_exit( int id)
{
    id_table1[id-1].used_flag = false;      //id_table1[id-1] 設為無人使用
    --(ID_TABLE::user_num);
    id_table1[id-1].sockfd = 0;
    id_table1[id-1].ip_port = "";
    id_table1[id-1].env.clear();

    for(int j=0;j<MAX_USER_NUM;j++)
    {
        file_table[id-1][j] = 0;
        file_table[j][id-1] = 0;
    }

    char tmp[100];

    sprintf(tmp,"*** User '%s' left. ***\n",id_table1[id-1].name.c_str());
    broadcast(tmp);

    id_table1[id-1].name = "";

    return;
}

void command_printenv( int id, const vector<string> &command)
{
    if( fork()==0 )
    {
        dup2(id_table1[id-1].sockfd,1);

        if( command.size()>1 )
        {
            if( id_table1[id-1].env.find(command[1].c_str()) != id_table1[id-1].env.end() )
            {
                char tmp[100];

                strcpy(tmp,command[1].c_str());
                strcat(tmp,"=");

                write( id_table1[id-1].sockfd, tmp, strlen(tmp));
            }
            execl("/usr/bin/printenv","printenv",command[1].c_str(),(char*)0);
        }
        else
        {
            execl("/usr/bin/printenv","printenv",(char*)0);
        }
    }
    else
    {
        wait(NULL);
    }

    return;
}

void command_setenv( int id, const vector<string> &command)
{
    setenv( command[1].c_str(), command[2].c_str(),1);
    id_table1[id-1].env[command[1].c_str()] = command[2].c_str();

    return;
}

void command_tell( int id, int toid, const string &command_str)
{

    if(id_table1[toid-1].used_flag==true)
    {
        char tmp[1200];
        char telltmp[1200];
        int j;

        for(j=0;command_str[j]!=' ';j++);   //遇到空白停下來
        for(j=j;command_str[j]==' ';j++);   //遇到非空白停下來
        for(j=j;command_str[j]!=' ';j++);   //遇到空白停下來
        for(j=j;command_str[j]==' ';j++);   //遇到非空白停下來

        strcpy( telltmp, command_str.substr(j).c_str());

        sprintf( tmp, "*** %s told you ***:  %s\n", id_table1[id-1].name.c_str(), telltmp);
        write( id_table1[toid-1].sockfd, tmp, strlen(tmp));
    }
    else
    {
        char tmp[1200];

        sprintf( tmp, "*** Error: user #<%d> does not exist yet. ***\n", toid);
        write( id_table1[id-1].sockfd, tmp, strlen(tmp));
    }

    return;
}

void command_yell( int id, const string &command_str)
{
    char tmp[1200];
    char yelltmp[1200];
    int j;

    for(j=0;command_str[j]!=' ';j++);

    strcpy(yelltmp,&command_str[j+1]);

    sprintf(tmp,"*** %s yelled ***:  %s\n",id_table1[id-1].name.c_str(),yelltmp);
    broadcast(tmp);

    return;
}

void command_who( int id)
{
    char tmp[100];

    sprintf( tmp, "<ID>\t<nickname>\t<IP/port>\t<indicate me>\n");
    write( id_table1[id-1].sockfd, tmp,strlen(tmp));

    for(int j=0;j<MAX_USER_NUM;++j)
    {
        if(id_table1[j].used_flag==true)
        {
            if( j==id-1 )
            {
                sprintf(tmp,"%d\t%s\t%s\t<-me\n",j+1,id_table1[j].name.c_str(),id_table1[j].ip_port.c_str());
                write(id_table1[id-1].sockfd,tmp,strlen(tmp));
            }
            else
            {
                sprintf(tmp,"%d\t%s\t%s\n",j+1,id_table1[j].name.c_str(),id_table1[j].ip_port.c_str());
                write(id_table1[id-1].sockfd,tmp,strlen(tmp));
            }
        }
    }

    return;
}

void command_name( int id, const vector<string> &command)
{

    char tmp[100];

    for(int j=0;j<MAX_USER_NUM;++j)
    {
        if(id_table1[j].used_flag==true)
        {
            if( command[1] == id_table1[j].name )
            {
                sprintf(tmp,"*** User '%s' already exists. ***\n",command[1].c_str());
                write(id_table1[id-1].sockfd,tmp,strlen(tmp));
                return;
            }
        }
    }

    id_table1[id-1].name = command[1];

    sprintf(tmp,"*** User from %s is named '%s'. ***\n",id_table1[id-1].ip_port.c_str(),id_table1[id-1].name.c_str());
    broadcast(tmp);

    return;
}

void ltrim( string &command_str)
{
    int i;
    for(i=0;command_str[i]==' ';++i);
    command_str = command_str.substr(i);
}

int my_shell(int id)
{
    int n;

    char wellcom[] = "****************************************\n** Welcome to the information server. **\n****************************************\n";

    int command_count;
    int childid;

    string command;
    vector<vector<string>> command_mul;
    vector<COMMAND_STRUCT> cmd_struct;
    vector<string> broadcastStr_afterExe;
    FILE_WRITE file_write_struct;


    dup2( id_table1[id-1].sockfd, 2);

    clearenv();

    map<string,string>::iterator iter;
    for(iter=id_table1[id-1].env.begin();iter!=id_table1[id-1].env.end();iter++)
    {
        setenv((*iter).first.c_str(),(*iter).second.c_str(),1);
    }

    n = readline( id_table1[id-1].sockfd, command);
    command = command.substr( 0, command.size()-1);  //去掉 '\r'
    ltrim(command);

    if(n==0)
    {
        return 0;
    }
    else if(n<0)
    {
        fprintf(stderr,"str_echo: readline error\n");
    }

    cout << endl << "PATH=" << getenv("PATH") << endl;

    cout << "command: " << "{ " << command << " }" << endl;

    if( splitcommand( id, command, command_mul, file_write_struct)==false )   //切指令
    {
        return 0;
    }

    if( command_mul.size()==0 )      //無指令
    {
        return 0;
    }
//-----------------------------------------------------------------------------------------------------
    //將 cat <# 的 "<#" 替換為實際檔名
    if( cat_from_others(command_mul, command, id, broadcastStr_afterExe) == -1 )
    {
        return 0;
    }
//-----------------------------------------------------------------------------------------------------
    create_command_struct( cmd_struct, command_mul);  //建立指令結構

    cout << "command num: " << cmd_struct.size() << endl;
    for(int i=0;i<cmd_struct.size();++i)
    {
        for(int j=0;j<cmd_struct[i].command.size();++j)
        {
            cout << cmd_struct[i].command[j] << " ";
        }
        cout << endl;
    }

    if( file_write_struct.file_write_flag==true )
    {
        if( file_write_struct.file_kind==NORMAL_FILE )
        {
            file_write_struct.file_fd = open( file_write_struct.file_name.c_str(), O_CREAT|O_TRUNC|O_WRONLY);
        }
        else if( file_write_struct.file_kind==TEMP_FILE )
        {
            file_write_struct.file_fd = open( file_write_struct.file_name.c_str(), O_CREAT|O_TRUNC|O_WRONLY);
        }
        cout << "output to: " << file_write_struct.file_name << endl;
    }
    else
    {
        cout << "output to: stdout" << endl;
    }
    cout << "---------------------------" << endl;
//-----------------------------------------------------------------------------------------------------
    if(strcmp(cmd_struct[0].command[0].c_str(),"exit")==0)
    {
        command_exit(id);
        return 1;
    }
//-----------------------------------------------------------------------------------------------------
    if(strcmp(cmd_struct[0].command[0].c_str(),"printenv")==0)
    {
        command_printenv( id, cmd_struct[0].command);
        return 0;
    }

//-----------------------------------------------------------------------------------------------------
    if(strcmp(cmd_struct[0].command[0].c_str(),"setenv")==0)
    {
        command_setenv( id, cmd_struct[0].command);
        return 0;
    }
//-----------------------------------------------------------------------------------------------------
    if(strcmp(cmd_struct[0].command[0].c_str(),"tell")==0)
    {
        int toid = atoi(cmd_struct[0].command[1].c_str());
        command_tell( id, toid, command);
        return 0;
    }
//-----------------------------------------------------------------------------------------------------
    if(strcmp(cmd_struct[0].command[0].c_str(),"yell")==0)
    {
        command_yell( id, command);
        return 0;
    }
//-----------------------------------------------------------------------------------------------------
    if(strcmp(cmd_struct[0].command[0].c_str(),"who")==0)
    {
        command_who(id);
        return 0;
    }
//-----------------------------------------------------------------------------------------------------
    if(strcmp(cmd_struct[0].command[0].c_str(),"name")==0)
    {
        if( cmd_struct[0].command.size()==2 )
        {
            command_name( id, cmd_struct[0].command);
        }
        return 0;
    }
//-----------------------------------------------------------------------------------------------------
    fileornot( cmd_struct, id_table1[id-1].sockfd);  //判斷執行檔是否存在
//----------------------------------------------------------------------------------------------------- exec部分

    PIPE_STRUCT *pipeStructs = new PIPE_STRUCT[cmd_struct.size()-1];

    for(int i=0;i<cmd_struct.size();i++)
    {
        create_pipe( i, cmd_struct, pipeStructs);

        if( (childid=fork())<0 )
        {
            fprintf(stderr,"Can not fork.\n");
        }
        else if( childid==0 )
        {
            redirect_input_and_output( i, id, cmd_struct, pipeStructs, file_write_struct);

            if( cmd_struct[i].command[0][0]=='|' )       //clone 部分 ( 即 |N )
            {
                execlp("clone","clone","",(char *)0);
            }
            else
            {
                char **argument = new char*[cmd_struct[i].command.size()+1];
                int j,k;

                for(j=0;j<cmd_struct[i].command.size();j++)
                {
                    argument[j] = new char[cmd_struct[i].command[j].size()];
                    strcpy(argument[j],cmd_struct[i].command[j].c_str());
                }

                argument[j] = 0;

                if( execvp(argument[0],argument)==-1 )
                {
                    exit(0);
                }
            }

        }
        else
        {
            wait(NULL);

            // parent need to close pipes which are no longer needed.
            // pipe's read end can be read only when their write end is closed .
            if( cmd_struct.size()>1 )
            {
                if( i==0 )
                {
                    close(pipeStructs[i].pipe_fd[1]);
                }
                else if( i == cmd_struct.size()-1 )
                {
                    close(pipeStructs[i-1].pipe_fd[0]);
                }
                else
                {
                    close(pipeStructs[i-1].pipe_fd[0]);
                    close(pipeStructs[i].pipe_fd[1]);
                }
            }

            // did receive files from others.
            if( i == cmd_struct.size()-1 && broadcastStr_afterExe.size()>0 )
            {
                for(int i=0;i<broadcastStr_afterExe.size();++i)
                {
                    broadcast(broadcastStr_afterExe[i].c_str());
                }
            }

            // did redirect stdout to files.
            if( i == cmd_struct.size()-1 && file_write_struct.file_write_flag==true )
            {
                close(file_write_struct.file_fd);
                if( file_write_struct.file_kind==TEMP_FILE )
                {
                    int toid = file_write_struct.to_id;
                    char tmp1[200];
                    sprintf( tmp1,"*** %s (#%d) just piped '%s' to %s (#%d) ***\n",
                            id_table1[id-1].name.c_str(),id,command.c_str(),
                            id_table1[toid-1].name.c_str(),toid);

                    broadcast(tmp1);
                }
            }
        }
    }

    delete[] pipeStructs;

    return 0;
}
//-----------------------------------------------------------------------------------------------------
int server_sockfd;

void sig_int(int sig)
{
    shutdown( server_sockfd, SHUT_RDWR);
    close(server_sockfd);
    exit(0);
}

int main(int argc,char *argv[])
{
    int newsockfd;
    struct sockaddr_in cli_addr, serv_addr;

    if( (server_sockfd = socket(AF_INET,SOCK_STREAM,0))<0 )
    {
        fprintf(stderr,"server: can't open datagram socket\n");
    }
    bzero((char*)&serv_addr,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if( bind(server_sockfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr))<0 )
    {
        fprintf(stderr,"server: can't bind local address\n");
    }
    listen( server_sockfd, MAX_USER_NUM);
//-------------------------------------------------------------------------------------------------

    fd_set rfds;
    fd_set afds;
    int nfds;

    chdir("./ras");

    clearenv();
    setenv("PATH","bin:.",1);

    FD_ZERO(&afds);

    // add server_sockfd to monitor set
    FD_SET( server_sockfd, &afds);
    nfds = max( nfds, server_sockfd);  //the largest possible value for a file descriptor

    memset( file_table, 0, sizeof(file_table));

    while(true)
    {
        // file descriptors which need monitor for reading, will be set to rfds. [rfds <- afds]
        memcpy( &rfds, &afds, sizeof(rfds));

        // monitor if any file descriptor is ready for reading, will be set in rfds.
        if( select( nfds+1, &rfds, (fd_set*)0, (fd_set*)0, (struct timeval*)0)<0 )
        {
            fprintf(stderr,"select error\n");
        }

        // if someone is connecting to server_sockfd, and user num is less than 30.
        if( ID_TABLE::user_num<MAX_USER_NUM && FD_ISSET(server_sockfd,&rfds) )
        {
            int clilen = sizeof(cli_addr);

            // accept a new person to connect to the server.
            newsockfd = accept( server_sockfd, (struct sockaddr*)&cli_addr, (socklen_t*)&clilen);
            if(newsockfd<0)
            {
                fprintf(stderr,"server: accept error\n");
            }

            // add socket file descriptor of the new person [newsockfd] to the monitor set.
            FD_SET( newsockfd, &afds);
            nfds = max( nfds, newsockfd);

            for(int i=0;i<MAX_USER_NUM;++i)
            {
                if( id_table1[i].used_flag==false )
                {
                    id_table1[i].used_flag = true;
                    ++(ID_TABLE::user_num);

                    stringstream ss;
                    ss << inet_ntoa(cli_addr.sin_addr) << "/" << cli_addr.sin_port;

                    id_table1[i].ip_port = ss.str();
                    id_table1[i].name = string("(no name)");
                    id_table1[i].env.clear();
                    id_table1[i].env["PATH"] = "bin:.";
                    id_table1[i].sockfd = newsockfd;

                    char welcom[] = "****************************************\n** Welcome to the information server. **\n****************************************\n";
                    write( id_table1[i].sockfd, welcom, strlen(welcom));      //welcome

                    char new_client[100];
                    sprintf( new_client, "*** User '(no name)' entered from %s. ***\n", id_table1[i].ip_port.c_str());
                    broadcast(new_client);

                    write( id_table1[i].sockfd, "% ", 2);  //提示符號

                    break;
                }
            }

        }
        for(int fd=0;fd<nfds;fd++)
        {
            // find which [socket fds] are ready for reading, except server_sockfd which is for listening.
            if( fd!=server_sockfd && FD_ISSET(fd,&rfds) )
            {
                for(int j=0;j<MAX_USER_NUM;++j)       //找到 fd 所在 ID
                {
                    if( id_table1[j].sockfd==fd )
                    {
                        // my_shell() will read commands from socket fd [j], and execute its commands,
                        // then return.
                        if( my_shell(j+1)==1 ) // input argument user ID.
                        {
                            // user of socket fd [j] input [exit] command.
                            shutdown( fd, SHUT_RDWR);
                            close(fd);
                            FD_CLR( fd, &afds);  // remove from monitor set.
                        }
                        else
                        {
                            write( id_table1[j].sockfd, "% ", 2);
                        }

                        break;
                    }
                }
            }
        }
    }

    return 0;
}


