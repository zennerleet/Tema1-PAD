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


FileDetails files[1024]; // max files on the server
int filesCount;
char *rootPath;
FileDetails *updatableFiles;
int updatableCount;

int isFile(struct stat buf)
{
  if(S_ISDIR(buf.st_mode))
    return 0;//directory
  return 1;//file
}

char *formatdate(time_t val)
{
	char *str;
	str = (char*)malloc(50);
  strftime(str, 50, "%Y%m%d%H%M%S", localtime(&val));
  return str;
}

void getAllFiles(char *serverFolderName)
{
	DIR* director;
  struct dirent * intrare;
 	director = opendir(serverFolderName);

	if(director == NULL)
	{
      printf("[-] Directory %s couldn't be opened", serverFolderName);
      exit(1);
	}

 	while((intrare= readdir(director)))
  {
    if(intrare->d_name[0] == '.') continue;
    struct stat buf;
    char cale[257];
    snprintf(cale,sizeof(cale),"%s/%s",serverFolderName,intrare->d_name);
    stat(cale,&buf);
	  strcpy(files[filesCount].path, cale);
	  strcpy(files[filesCount].dateTime, formatdate(buf.st_mtime));
	  files[filesCount].type = isFile(buf);
	  files[filesCount].size = buf.st_size;
	  filesCount++;

    if (S_ISDIR(buf.st_mode))
        getAllFiles(cale);
  }
  closedir(director);
}

void sendFilesToClient(int clientSocket)
{
	char buffer[5];
	int i;
	snprintf (buffer, sizeof(buffer), "%d",filesCount);
	send(clientSocket, &buffer, strlen(buffer), 0);

	for (i = 0; i < filesCount; i++)
		send(clientSocket, &files[i], sizeof(FileDetails), 0);
}

void getFilesFromClient(int clientSocket)
{
	char buffer[5];
	int i;

	recv(clientSocket, &buffer, sizeof(buffer), 0);
	updatableCount = atoi(buffer);

	updatableFiles = (FileDetails *)malloc(updatableCount*sizeof(FileDetails));

	for (i = 0; i < updatableCount; i++)
		recv(clientSocket, &updatableFiles[i], sizeof(FileDetails), 0);
}

void sendFile(int clientSocket, FileDetails file)
{
	int fd = open(file.path, O_RDONLY);
	int byte, size = file.size;
	char buffer[100];

	printf("Sending file %s to the client, size: %d\n", file.path, file.size);

	while(size>0)
	{
		int read_bytes = sizeof(buffer);
		if (size < sizeof(buffer))
			read_bytes = size;
		read(fd, &buffer, read_bytes);
		send(clientSocket, &buffer, read_bytes, 0);
		size-=read_bytes;
		memset(buffer, 0, sizeof(buffer));
	}

	close(fd);
}

void processUpdates(int clientSocket)
{
	int i;
	printf("-> %d files need to be updated\n", updatableCount);
	for (i = 0; i < updatableCount; i++)
		sendFile(clientSocket, updatableFiles[i]);
}
int main(int argc, char * argv[])
{
	if (argc != 2)
	{
		printf("Usage: ./name <ServerFolderName>\n");
		exit(1);
	}

	rootPath = (char*) malloc(strlen(argv[1]));
	strcpy(rootPath,argv[1]);
	int sockfd, ret, newSocket;
	struct sockaddr_in serverAddr, newAddr;

	socklen_t addr_size;

	char buffer[1024];
	pid_t childpid;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if(sockfd < 0)
	{
		printf("[-]Error in connection.\n");
		exit(1);
	}
	printf("Server is online.\n");

	memset(&serverAddr, '\0', sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	ret = bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
	if(ret < 0)
	{
		printf("[-]Error in binding.\n");
		exit(1);
	}
	printf("Bind to port %d\n", PORT);

	if(listen(sockfd, 10) == 0)
	{
		printf("Listening....\n");
	}
	else
	{
		printf("[-]Error in listening.\n");
	}

	while(1)
	{
		newSocket = accept(sockfd, (struct sockaddr*)&newAddr, &addr_size);
		if(newSocket < 0)
			exit(1);
		if(ntohs(newAddr.sin_port) == 0)
			close(newSocket);
		printf("Connection accepted from %s:%d\n", inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));

		if((childpid = fork()) == 0)
		{
			close(sockfd);

			// code for update files
			getAllFiles(rootPath);
			sendFilesToClient(newSocket);
			printf("Getting files\n");
			getFilesFromClient(newSocket);
			processUpdates(newSocket);
			printf("Disconnected from %s:%d\n\n", inet_ntoa(newAddr.sin_addr), ntohs(newAddr.sin_port));
			break;
		}
	}
	close(newSocket);
	return 0;
}
