//
// Created by ezio on 16/07/19.
//

#include "const.h"

int actual_branches_num = 0;

struct handler_info *info;

struct branches_info_list{
    struct branch_handler_communication *info;
    struct branches_info_list *next;
    struct branches_info_list *prev;
};

struct branches_info_list *first_branch_info;
struct branches_info_list *last_branch_info;

struct branch_handler_communication *array_hb;



//Used for testing, remove before deploy
void branchesStatus()
{
    int temp;

    for(struct branches_info_list *current = first_branch_info; current != NULL; current = current->next){

        if(sem_wait(&((current->info)->sem_toNumClients)) == -1){
            perror("Error in sem_wait (on counting connected clients): ");
            exit(-1);
        }

        temp = (current->info)->active_clients;

        if(sem_post(&((current->info)->sem_toNumClients)) == -1){
            perror("Error in sem_post (on counting connected clients): ");
            exit(-1);
        }

        printf("Server branch with pid: %d has %d clients\n", (current->info)->branch_pid, temp);
    }
}



int look_for_first_array_pos()
{
    for(int i = 0; i < actual_branches_num+1; ++i){

        if((array_hb+i)->branch_pid == -1)
            return i;
    }
    return -1;
}



int create_new_branch()
{
    ///last_branch_info is the pointer to the struct that has to be
    ///filled with the information used by the server branch
    ///which we are going to create right now

    struct branches_info_list *new_entry;

    //finding a position of the array_hb...
    int pos = look_for_first_array_pos();
    
    //printf("\n\nFound %d POSITION, giving from (array+pos): %p to (attay+pos+1): %p\n\n", pos, array_hb+pos, array_hb+pos+1);

    //preparing data for the new entry
    if((new_entry = (struct branches_info_list *)malloc(sizeof(struct branches_info_list))) == NULL){
        perror("Unable to malloc (creating new entry in branches_info): ");
        exit(-1);
    }

    //...and memorizing it into the information list
    new_entry->info = (array_hb + pos);

    memset(new_entry->info, 0, sizeof(struct branch_handler_communication));

    //marking the pid in this memory as 'used' by simply giving to him  a value != -1
    (new_entry->info)->branch_pid = 0;

    //linking the element in the list
    if(first_branch_info == NULL){
        first_branch_info = new_entry;
        first_branch_info->prev = NULL;
        first_branch_info->next = NULL;
        last_branch_info = first_branch_info;
    }else{
        //if there are other server branches
        last_branch_info->next = new_entry;
        new_entry->prev = last_branch_info;
        last_branch_info = last_branch_info->next;
        last_branch_info->next = NULL;
    }

    //when the branches handler has to check the global number of client
    //connected, it has to wait for this semaphore for each branch
    //N.B.:last_brench_info->info has to point to some (array_hb + i) where i
    //is the position that has been (already) chosen
    if(sem_init(&((last_branch_info->info)->sem_toNumClients), 1, 1) == -1){
        perror("Error in sem_init (sem_tolistenfd): ");
        exit(-1);
    }

    //giving the position of the array_hb that the branch will own
    char memAddr[5];
    memset(memAddr, 0, sizeof(memAddr));
    sprintf(memAddr, "%d", pos);

    if(!fork()){
        execl("./ServerBranch", "ServerBranch.out", memAddr, NULL);
        perror("Error in execl: ");
        exit(-1);
    }

    actual_branches_num++;

    printf("\t\t\tNew server branch generated\n");

    return 0;
}



int merge_branches(int pid_clientReciver, struct branch_handler_communication *reciver_addr,
                        int pid_clientSender, struct branch_handler_communication *sender_addr)
{
    //the process relative to the 2 branches with less number of clients
    //have to know which one is going to be the reciver and which one
    //is going to be the sender

    reciver_addr->recive_clients = 1;

    sender_addr->recive_clients = 0;

    if(kill(pid_clientSender, SIGUSR2) == -1){
        perror("Error in killing sender (SIGUSR2)");
        return -1;
    }

    if(kill(pid_clientReciver, SIGUSR2) == -1){
        perror("Error in killing reciver (SIGUSR2)");
        return -1;
    }

    //waiting that both the reciver and transmitter
    //has finished to transmit connections (file descriptor)
    if(sem_wait(&(info->sem_transfClients)) == -1){
        perror("Error in sem_post (on counting connected clients): ");
        exit(-1);
    }
    if(sem_wait(&(info->sem_transfClients)) == -1){
        perror("Error in sem_post (on counting connected clients): ");
        exit(-1);
    }

    //printf("Clients of process '%d' were transmitted to process '%d'\n", pid_clientSender, pid_clientReciver);

    //removing sender branch information from the structure branches_info
    for(struct branches_info_list *current = first_branch_info; ; current = current->next){

        if(current->info == sender_addr) {

            //marking as 'usable' the struct in shared memory (IPC)
            (current->info)->branch_pid = -1;

            if(current->prev == NULL){
                //if the information were stored in the first element of the list
                first_branch_info = current->next;
                first_branch_info->prev = NULL;

            }else if(current->next == NULL){
                //if the information were stored in the last position of the list
                last_branch_info = current->prev;
                last_branch_info->next = NULL;
            }else{
                (current->prev)->next = current->next;
                (current->next)->prev = current->prev;
            }

            free(current);

            break;
        }

    }

    actual_branches_num--;

    printf("Merge function ended\n");

    return 0;
}



#include "handlerEventHandlers.h"



int main(int argc, char **argv) {
    int id_info, id_hb;
    pid_t my_pid;
    sem_t sem_tolistenfd, sem_trasfclients, sem_sendrecive;

    int listen_fd;
    struct sockaddr_in address;
    int socket_opt = 1;

    //initializing cache address
    if (shmget(IPC_CACHE_KEY, CACHE_BYTES, IPC_CREAT|0666) == -1) {
        perror("Error in shmget (cache): ");
        exit(-1);
    }

    //initializing address of the struct that will be used by all the server branches
    //to acquire handler creator informations
    if ((id_info = shmget(IPC_HNDLR_INFO_KEY, sizeof(struct handler_info), IPC_CREAT|0666)) == -1) {
        perror("Error in shmget (handler info): ");
        exit(-1);
    }

    //initializing memory that will be used for the transmission of the information
    //between handler and branch
    if ((id_hb = shmget(IPC_BH_COMM_KEY, sizeof(struct branch_handler_communication) * MAX_BRANCHES,
                        IPC_CREAT | 0666)) == -1) {
        perror("Error in shmget (handler-branch)");
        exit(-1);
    }

    //attaching the memory starting from array_hb. This way we can see this memory
    //as it was an array_hb[MAX_BRANCHES].
    if ((array_hb = shmat(id_hb, NULL, SHM_R | SHM_W)) == (void *) -1) {
        perror("Error in shmat (array_hb)");
        exit(-1);
    }

    if ((info = shmat(id_info, NULL, SHM_R | SHM_W)) == (void *) -1) {
        perror("Error in shmat");
        exit(-1);
    }

    //initializing semaphore which will be used by all server branches to
    //acquire the listening socket and serve clients
    if (sem_init(&sem_tolistenfd, 1, 1) == -1) {
        perror("Error in sem_init (sem_tolistenfd): ");
        exit(-1);
    }

    //initializing the semaphore that the branches handler will post 2 times
    //after when he decide to merge 2 branches.
    //Each branch involved in this procedure will post this semaphore when
    //the transmission of the clients is completed
    if (sem_init(&sem_trasfclients, 1, 0) == -1) {
        perror("Error in sem_init (sem_trasfclients): ");
        exit(-1);
    }

    //initislizing semaphore used to syncronize sender and reciver during
    //the transmission of the clients from a branch server to the other.
    //the reciver will wait this semaphore until the sender has created
    //the unix socket used to transfer clients
    if (sem_init(&sem_sendrecive, 1, 0) == -1) {
        perror("Error in sem_init (sem_sendrecive): ");
        exit(-1);
    }

    //initializing listening socket
    memset(&address, 0, sizeof(address));
    address.sin_addr.s_addr = htonl(SERVER_ADDR);
    address.sin_port = htons(SERVER_PORT);
    address.sin_family = AF_INET;

    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error in socket: ");
        exit(-1);
    }

    if(signal(SIGCHLD, sig_chl_handler) == SIG_ERR){
        perror("Error in signal (SIGCHL)");
        exit(-1);
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &socket_opt, sizeof(socket_opt)) == -1) {
        perror("Error in setsockopt: ");
        exit(-1);
    }

    //binding address
    if(bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) == -1){
        perror("Error in bind: ");
        exit(-1);
    }

    if (listen(listen_fd, BACKLOG) == -1) {
        perror("Error in listen: ");
        exit(-1);
    }

    my_pid = getpid();

    //inserting handler information into the shared structure
    info->pid = my_pid;
    info->listen_fd = listen_fd;
    info->sem_toListenFd = sem_tolistenfd;
    info->sem_transfClients = sem_trasfclients;
    info->sem_sendRecive = sem_sendrecive;

    //when the number of the active clients of a branch get to the 10%, 50% and 80%
    //of MAX_CLI_PER_SB, it signals the creator with SIGUSR1.
    //The branches handler will decide whether merge or create branches.
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = clients_has_changed;
    act.sa_flags = 0;

    if(sigaction(SIGUSR1, &act, NULL) == -1){
        perror("Error in sigaction (SIGUSR1): ");
        exit(-1);
    }

    //initializing the list of branches
    first_branch_info = NULL;
    last_branch_info = first_branch_info;

    //initializing the array. Operation needed to distinguish
    //free positions
    printf("Initializing shared memory\n");
    for (int i = 0; i < MAX_BRANCHES; ++i){
        (array_hb +i)->branch_pid = -1;
    }
    printf("Shared memory initialized\n");

    printf("CHK_PERC_EACH: %d\n", CHECK_PERC_EACH);

    //generating the initial server branches
    for(int i = 0; i < NUM_INIT_SB; ++i) {
        if (create_new_branch()) {
            printf("Error in create_new_branch (generating point)\n");
            exit(-1);
        }
    }

    //waiting for signals to come
    while(1)
        pause();
}