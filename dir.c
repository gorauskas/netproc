#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include <dirent.h>
#include <ctype.h>

#define MAX_PROCESS 1024
#define PROCESS_DIR "/proc/"

#define MAX_INODES 1024
#define PATH_INODE "/proc/net/tcp"

#define MAX_FD 50000

#define MAX_NAME 1024

struct process
{
  char name[MAX_NAME]; // nome do processo
  int pid;             // pid do processo
  int fds[MAX_FD];     // todos os file descriptos - link simbolico - do processo
};


static bool is_number(const char *caracter)
{
  while (*caracter)
    {
      if (!isdigit(*caracter))
        return false;

      caracter++;
    }

  return true;
}



// recebe o nome do diretorio dos processos, um buffer para armazenar
// os nomes dos diretorios e um tamanho maximo para o buffer.
// retorna o total de diretorios encontrados, -1 em caso de falha.
int get_numeric_directory(const char *process_dir, int *buffer, const int lenght)
{
  DIR *dir;
  struct dirent *directory = NULL;
  int count = 0;

  if ((dir = opendir(process_dir)) == NULL)
    return 0;

  while ((directory = readdir(dir)) != NULL && count < lenght)
    {
      if (is_number(directory->d_name))
        {
          buffer[count] = atoi(directory->d_name);
          count++;
        }

    }

  return count;
}


// le o arquivo onde fica salva as conexoes
// '/proc/net/tcp', recebe o local do arquivo, um buffer para armazenar
// o inode e o tamanho do buffer, retorna a quantidade de registros encontrada
// ou zero em caso de erro
int get_inodes(const char *inode_file, unsigned int *buffer, const int lenght)
{

  FILE *arq;
  char *line;
  size_t len = 0;
  ssize_t nread;

  int count = 0;

  unsigned int id,
           local_address,
           local_port,
           remote_address,
           remote_port,
           con_state,
           tx_queue,
           rx_queue,
           timer_active,
           tm_when,
           retrnsmt,
           uid,
           timeout,
           inode;


  if ((arq = fopen(inode_file, "r")) == NULL)
    return 0;

  // ignore first line
  if ((getline(&line, &len, arq)) == -1)
    return 0;

  while ((nread = getline(&line, &len, arq)) != -1 && count < lenght)
    {
      sscanf(line, "%d: %x:%x %x:%x %x %x:%x %d:%x %x %x %d %d",
             &id, &local_address, &local_port, &remote_address, &remote_port,
             &con_state, &tx_queue, &rx_queue, &timer_active, &tm_when,
             &retrnsmt, &uid, &timeout, &inode);

      //printf("local addres - %d \tinode - %d\n", local_address, inode);

      buffer[count] = inode;
      count++;
    }

  return count;
}



int main(void)
{

  struct process processos[MAX_PROCESS];

  int tot_process;
  int process_pids[MAX_PROCESS] = {0};

  tot_process = get_numeric_directory(PROCESS_DIR, process_pids, MAX_PROCESS);


  int tot_inodes;
  unsigned int inodes[MAX_INODES] = {0};

  tot_inodes =  get_inodes(PATH_INODE, inodes, MAX_INODES);


  int tot_fd_in_process = 0;
  int fds_process_buff[1000];
  char path_fd[100];
  char process[100];
  char socket[100];

  char temp_buff[1000];
  ssize_t len_link;

  /*
   percorre todos os processos encontrados no diretório '/proc/',
   em cada processo encontrado armazena todos os file descriptors do processo em fds_process_buff,
   depois compara o link simbolico apontado pelo FD com 'socket:[inode]', sendo inode coletado
   do arquivo '/proc/net/tcp', caso a comparação seja igual, encontramos o processo que corresponde
   ao inode (conexão).
  */
  for (int i = 0; i < tot_process; i++)
    {
      // pega todos os file descriptors dos processos encontrados em '/proc/$id/fd'
      snprintf(process, 100, "/proc/%d/fd/", process_pids[i]);
      tot_fd_in_process = get_numeric_directory(process, fds_process_buff, 1000);

      while (tot_fd_in_process)
        {
          // isola um link simbolico por vez do processo
          snprintf(path_fd, 100, "/proc/%d/fd/%d", process_pids[i], fds_process_buff[tot_fd_in_process - 1]);

          // para cada inode compara o conteudo do link simbolico isolado
          // anteriormente
          for (int j = 0; j < tot_inodes; j++)
            {
              snprintf(socket, 100, "socket:[%d]", inodes[j]);
              // printf("%s\n", socket);
              len_link = readlink(path_fd, temp_buff, 99);
              temp_buff[len_link] = '\0';

              // se o conteudo de socket - socket:[$inode] - for igual
              // ao valor lido do link simbolico do processo,
              // encontrado de qual processo o inode corresponde
              if (!strncmp(socket, temp_buff, len_link))
                printf("process pid - \t%d\ninode - \t%d\n\n", process_pids[i], inodes[j]);
            }

          // printf("%s\n", path_fd);
          tot_fd_in_process--;
        }

    }


}
