#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define PORT 9002

typedef struct {
	char path[256];
	char dateTime[48];
	int type;
	int size;
}FileDetails;

FileDetails ownFiles[1024]; // files on the client
FileDetails serverFiles[1024]; // files on the server
int filesToBeUpdated[1024]; // file indexes
int updatedCount;
int ownFilesCount;
int serverFilesCount;

int isFile(struct stat buf)
{
  if(S_ISDIR(buf.st_mode))
    return 0;//directory
  return 1;//file
}

int removeDirectory(char *path)
{
    char command[255];
    int status, exitcode;

    snprintf(command, sizeof(command), "rm -rf \"%s\"", path);
    status = system(command);
    exitcode = WEXITSTATUS(status);
    return exitcode;
}

int removeFile(char *path)
{
    char command[255];
    int status, exitcode;

    snprintf(command, sizeof(command), "rm \"%s\"", path);
    status = system(command);
    exitcode = WEXITSTATUS(status);
    return exitcode;
}

char *formatdate(time_t val)
{
    char *str;
    str = (char*)malloc(50);
    strftime(str, 50, "%Y%m%d%H%M%S", localtime(&val));
    return str;
}

void getAllFiles(char * clientFolderName)
{
	DIR* director;
  struct dirent * intrare;
 	director = opendir(clientFolderName);

	if(director == NULL)
	{
      printf("[-] Directory %s couldn't be opened",clientFolderName);
      exit(1);
	}

 	while((intrare= readdir(director)))
  {
    if(intrare->d_name[0] == '.') continue;
    struct stat buf;
    char cale[257];
    snprintf(cale,sizeof(cale),"%s/%s",clientFolderName,intrare->d_name);
    stat(cale,&buf);
	  strcpy(ownFiles[ownFilesCount].path, cale);
	  strcpy(ownFiles[ownFilesCount].dateTime, formatdate(buf.st_mtime));
	  ownFiles[ownFilesCount].type = isFile(buf);
	  ownFiles[ownFilesCount].size = buf.st_size;
	  ownFilesCount++;

    if (S_ISDIR(buf.st_mode))
        getAllFiles(cale);
  }
  closedir(director);
}

void getRemoteFiles(int serverSocket, char *rootFolderName)
{
	char buffer[5];
  int i;

	recv(serverSocket, &buffer, sizeof(buffer), 0);
	serverFilesCount = atoi(buffer);

	for (i = 0; i < serverFilesCount; i++)
  {
		recv(serverSocket, &serverFiles[i], sizeof(FileDetails), 0);
    if(serverFiles[i].path[0]=='/')
    {
      printf("[-] There was a problem at the server. Try again.\n");
      exit(5);
    }
	}
}

int contains(FileDetails *array, int no, char *path, int type)
{
	int i;
	for (i = 0; i < no; i++)
		if(strcmp(array[i].path, path) == 0)
			return i;
	return -1;
}

void sendFilesToServer(int serverSocket)
{
	char buffer[5];
  int i;

	snprintf (buffer, sizeof(buffer), "%d",updatedCount);
	send(serverSocket, &buffer, strlen(buffer), 0);
  usleep(100);
  printf("-> %d file(s) need(s) to be updated\n",updatedCount);
	for (i = 0; i < updatedCount; i++)
		send(serverSocket, &serverFiles[filesToBeUpdated[i]], sizeof(FileDetails), 0);
}

void acceptFile(int serverSocket)
{
	int i, size, fd;
	char buffer[100];
	for (i = 0; i < updatedCount; i++)
  {
		size = serverFiles[filesToBeUpdated[i]].size;
		printf("Fetch: %s from server, size: %d\n",serverFiles[filesToBeUpdated[i]].path, size);
		fd = open(serverFiles[filesToBeUpdated[i]].path, O_CREAT | O_TRUNC | O_WRONLY, 0777);
    while(size>0)
    {
      int read_bytes = sizeof(buffer);
      if (size < sizeof(buffer))
        read_bytes = size;
  		recv(serverSocket, &buffer, read_bytes, 0);
  		write(fd, &buffer, read_bytes);
      size-=read_bytes;
      memset(buffer, 0, sizeof(buffer));
    }
		close(fd);
	}
}

void checkFiles()
{
	int i, fileIdx;
	printf("Checking the version..\n");
	for (i = 0; i < ownFilesCount; i++)
  {
		fileIdx = contains(serverFiles, serverFilesCount, ownFiles[i].path, ownFiles[i].type);
		if (fileIdx == -1)
    {
      printf("\033[0;31m");
			if (ownFiles[i].type == 0)
      {
				printf("Directory %s does not exist on the server, must be deleted.\n", ownFiles[i].path);
				removeDirectory(ownFiles[i].path);
			}
			else
      {
				printf("File %s does not exist on the server, must be deleted.\n", ownFiles[i].path);
				removeFile(ownFiles[i].path);
			}
			continue;
		}
		if ( (atol(ownFiles[i].dateTime) < atol(serverFiles[fileIdx].dateTime)) || (ownFiles[i].size != serverFiles[fileIdx].size))
    {
			if (ownFiles[i].type == 1)
      {
        printf("\033[0;32m");
				printf("File %s needs to be updated!\n", ownFiles[i].path);
				removeFile(ownFiles[i].path);
				filesToBeUpdated[updatedCount++] = fileIdx;
			}
		}
	}
  printf("\033[0;34m");
	for (i = 0; i < serverFilesCount; i++)
  {
		fileIdx = contains(ownFiles, ownFilesCount, serverFiles[i].path, serverFiles[i].type);
		if (fileIdx != -1) continue;
		if (serverFiles[i].type == 1)
    {
			printf("File %s does not exist on the client, must be fetched.\n", serverFiles[i].path);
			filesToBeUpdated[updatedCount++] = i;
		}
		else
    {
      printf("Directory %s does not exist on the client, must be created.\n", serverFiles[i].path);
			mkdir(serverFiles[i].path, 0777);
		}
	}
  printf("\033[0m");
}

int main(int argc, char * argv[])
{
	if (argc != 2)
  {
		printf("Usage: ./name <ClientFolderName>\n");
		exit(1);
	}

	int serverSocket, ret;
	struct sockaddr_in serverAddr;
	char buffer[1024];

	serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(serverSocket < 0)
  {
		printf("[-] Error in connection for socket.\n");
		exit(1);
	}
	printf("Client Socket is created.\n");

	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	ret = connect(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

	if(ret < 0)
  {
		printf("[-] Error in connection for connect.\n");
		exit(1);
	}
	printf("Connected to Server.\n");

	getRemoteFiles(serverSocket,argv[1]);

  if(serverFilesCount == 0)
  {
    printf("[-] There was a connection problem. Try again.\n");
    close(serverSocket);
    return 0;
  }

	getAllFiles(argv[1]);
	checkFiles();

  if(updatedCount == 0)
  {
    printf("All files are up-to-date.\n");
  	close(serverSocket);
    return 0;
  }

	sendFilesToServer(serverSocket);
	acceptFile(serverSocket);
	close(serverSocket);
	return 0;
}
