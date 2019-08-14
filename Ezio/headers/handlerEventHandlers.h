//
// Created by ezio on 28/07/19.
//

#include <wait.h>



void sig_chl_handler(int signum)
{
    int status;
    pid_t pid;

    while((pid = waitpid(WAIT_ANY, &status, WNOHANG)) > 0){
        printf("Serer branch %d has terminated\n", pid);
        return;
    }
}



void clients_has_changed()
{
    //the 2 branches with less number of connected clients will be selected
    //and eventually used in merge_branches

    //before create or merge branches, the logger manager has to finish its job
    //or the number of branches (and loggers) may increment or decrement while
    //the loggerManager is working on them, and nothing will work anymore
    //printf("Recived SIGUSR1 in handler, client has changed\n");

    //blocking SIGUSR1 in order to complete the function and not being interrupted
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = SIG_IGN;
    act.sa_flags = 0;

    if(sigaction(SIGUSR1, &act, NULL) == -1){
        perror("Error in sigaction (SIGUSR1, SIG_IGN): ");
        exit(-1);
    }

    //after the execution of the handler we have to check if other SIGUSR1 has arrived
    sigset_t set;
    sigaddset(&set,SIGUSR1);
    sigprocmask(SIG_BLOCK,&set,NULL);

    int min = MAX_CLI_PER_SB+1;
    int veryMin = min;
    pid_t minPid = -1, veryMinPid = -1;

    struct branch_handler_communication *minAddr = NULL, *veryMinAddr = NULL;

    int connectedClients = 0;

    int temp;

    //looking for the number of ALL the connected clients
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

        connectedClients += temp;

        if(temp < veryMin ) {
            min = veryMin;
            minPid = veryMinPid;
            minAddr = veryMinAddr;
            veryMin = temp;
            veryMinPid = (current->info)->branch_pid;
            veryMinAddr = current->info;
        }else if(temp < min){
            min = temp;
            minPid = (current->info)->branch_pid;
            minAddr = current->info;
        }
    }

    //NUM_INIT_SB should never be less than 2, or
    //this logic does not work anymore.

    //if connected clients are grater than the 80% of acceptable clients then
    //create a new branch,
    //if connected clients are less than the 10% of acceptable clients and
    //there are more server branches than NUM_INIT_SB then
    //merge the 2 branches which have the less number of connected clients
    if(connectedClients > NEW_SB_PERC*MAX_CLI_PER_SB*actual_branches_num){

        if(create_new_branch()){
            printf("Error in merge_branches\n");
            exit(-1);
        }

    }else if(connectedClients < MERGE_SB_PERC*MAX_CLI_PER_SB*actual_branches_num
             && actual_branches_num > NUM_INIT_SB){

        //before checking if merge operation is needed, the loggerManager
        //has to finish the sorting operation of the logs
        //printf("Waiting on semLoggerManaher_Handler for merge, handler\n");
        if(sem_wait(&semLoggerManaher_Handler) == -1){
            perror("Error in sem_wait (semLoggerManaher_Handler) handler");
            return;
        }

        //reciver is the minPidClient
        //sender is the veryMinClient
        printf("Merging between sender %d, and reciver %d\n", veryMinPid, minPid);
        if(merge_branches(minPid, minAddr, veryMinPid, veryMinAddr)){
            printf("Error in merge_branches\n");
            exit(-1);
        }
        //printf("Merge ended, actual_branches_num: %d\n", actual_branches_num);

        //printf("Posting on semLoggerManaher_Handler, handler\n");
        if(sem_post(&semLoggerManaher_Handler) == -1){
            perror("Error in sem_post (semLoggerManaher_Handler) handler");
            return;
        }

    }


    //restoring event hadnler
    act.sa_handler = clients_has_changed;

    if(sigaction(SIGUSR1, &act, NULL) == -1){
        perror("Error in sigaction (SIGUSR1, restoring): ");
        exit(-1);
    }

    //checking if any SIGUSR1 has arrived during the execution of this handler
    if(sigismember(&set, SIGUSR1)){
        sigemptyset(&set);
        sigaddset(&set,SIGUSR1);
        //printf("another SIGUSR has arrived while executing its handler\n");
        sigprocmask(SIG_UNBLOCK,&set,NULL);
    }

    //printf("clients_has_changed ended \n");
    //branchesStatus();
}
