#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>
#include "support.h"

#define USER_FILE "users"
#define NODE_FILE "nodes"

void handle_signal(int signal);
int compare(const void * a, const void * b); 

int q_id,flag=1, ended_childs=0;

int main() {

    /* Variabili che poi verranno lette a runtime */
    int SO_USERS_NUM, SO_NODES_NUM, SO_BUDGET_INIT, SO_REWARD,
    SO_MIN_TRANS_GEN_NSEC, SO_MAX_TRANS_GEN_NSEC, SO_RETRY, SO_TP_SIZE, 
    SO_MIN_TRANS_PROC_NSEC, SO_MAX_TRANS_PROC_NSEC, SO_SIM_SEC;

    /*
    *    SO_USERS_NUM                  run time     100            1000             20
    *    SO_NODES_NUM                  run time     10             10               10
    *    SO_BUDGET_INIT                run time     1000           1000             10000
    *    SO_REWARD [0–100]             run time     1              20               1
    *    SO_MIN_TRANS_GEN_NSEC [nsec]  run time     100000000      10000000         10000000
    *    SO_MAX_TRANS_GEN_NSEC [nsec]  run time     200000000      10000000         20000000
    *    SO_RETRY                      run time     20             2                10
    *    SO_TP_SIZE                    run time     1000           20               100
    *    SO_MIN_TRANS_PROC_NSEC [nsec] run time     10000000       1000000
    *    SO_MAX_TRANS_PROC_NSEC [nsec] run time     20000000       1000000
    *    SO_SIM_SEC [sec]              run time     10             20               20
    */

    int i, j, k,child_pid, status,budget;
    struct shm_data * my_data;
    struct sigaction sa;
    long struct_size;
    int s_id, m_id;
    pid_t * users_pid, * nodes_pid;
    int * tx_nodes;
    struct sembuf sops;
    struct timespec tp;
    struct msg_pid msg_pid;
    clockid_t clk_id;
    int tm, rnd_user, rnd_node, user_rcv, num_op;
    char * args[5] = {USER_FILE};
    char s_id_str[20];
    char m_id_str[20];
    char q_id_str[20];
    struct msgbuff my_message;
    struct array_transaction block_message;
    sigset_t my_mask;
    pinfo * user_pinfo;
    pinfo * node_pinfo;
    FILE * config_file;

    config_file = fopen("config.txt", "r");

    if (config_file == NULL) {
        printf("MASTER %d: COULD NOT OPEN CONFIGURATION FILE\n", getpid());
        exit(-1);
    }

    fscanf(config_file, "%d %d %d %d %d %d %d %d %d %d %d" , &SO_USERS_NUM, &SO_NODES_NUM, &SO_BUDGET_INIT, &SO_REWARD,
    &SO_MIN_TRANS_GEN_NSEC, &SO_MAX_TRANS_GEN_NSEC, &SO_RETRY, &SO_TP_SIZE, &SO_MIN_TRANS_PROC_NSEC, &SO_MIN_TRANS_PROC_NSEC, &SO_SIM_SEC);

    m_id = shmget(IPC_PRIVATE, sizeof(*my_data), 0600); /* Creazione della shared memory */
    my_data = shmat(m_id, NULL, 0);
    shmctl(m_id,IPC_RMID,NULL);
    TEST_ERROR;

    /*  
     *  Allocazione dinamica di due array, che serviranno
     *  poi per stampare ogni secondo il budget e il pid
     *  degli utenti piu' rilevanti 
     */

    user_pinfo = malloc(sizeof(pinfo) * SO_USERS_NUM);
    node_pinfo = malloc(sizeof(pinfo) * SO_NODES_NUM);

    /* 
     *  Faccio l'attach della shared memory, inizializzo
     *  le variabili della shm a 0. Faccio gia' la detach,
     *  tanto non verra' effettuata subito ma solo al terminare
     *  di tutti i processi che avevano fatto l'attach.
     */

    my_data = shmat(m_id, NULL, 0);
    my_data->ledger_index = 0;
    my_data->premature = 0;
    shmctl(m_id, IPC_RMID, NULL);

    s_id = semget(IPC_PRIVATE, 6, 0600); /* Creazione dei semafori */
    TEST_ERROR;

    q_id = msgget(IPC_PRIVATE, IPC_CREAT | 0600); /* Creazione della message queue */
    TEST_ERROR;

    /* 
     * Impacchetto le risorse IPC, ossia 
     * shared memory, semaphores e message queue,
     * e li scrivo nell'array args che passero'
     * come parametro alla execve
     * 
     */

    sprintf(m_id_str, "%d", m_id);
    sprintf(s_id_str, "%d", s_id);
    sprintf(q_id_str, "%d", q_id);
    TEST_ERROR;

    args[1] = m_id_str;
    args[2] = s_id_str;
    args[3] = q_id_str;
    args[4] = NULL;
    TEST_ERROR;

    /*
     * Setto alcuni valori dei semafori.
     * I semafori settati a 1 indicano semafori per
     * la mutua esclusione.
     * I semafori settati a 0 indicano semafori per
     * la wait for zero (per permettere sincronizzazione).
     * Il primo semaforo (indice 0) serve per la sincronizzazione
     * tra processo master e processi user, per poter 
     * passare i pid di tutti i processi utente.
     * 
     */

    semctl(s_id, 0, SETVAL, SO_USERS_NUM); 
    semctl(s_id, 1, SETVAL, 1);
    semctl(s_id, 2, SETVAL, 0);
    semctl(s_id, 3, SETVAL, 0);
    semctl(s_id, 4, SETVAL, 0); 
    semctl(s_id, 5, SETVAL, 1);
    TEST_ERROR;

    /*
     *  Alloco dinamicamente l'array che conterra tutti i pid
     *  dei figli, dopodiche' faccio una fork per il totale 
     *  numero dei processi utente.
     * 
     */

    users_pid = malloc(SO_USERS_NUM * sizeof(* users_pid));

    for(i=0; i<SO_USERS_NUM; i++) {
        switch(users_pid[i] = fork()) {
            case -1:
                TEST_ERROR;
                break;
            case 0:
                execve(USER_FILE, args, NULL);
                TEST_ERROR;
                break;
            default:
                break;
        }
    }

    /* Stesso procedimento per la fork degli utenti, questa
     * parte si occupa del fork dei processi nodo, con 
     * allocamento dinamico di due array, uno per i pid dei
     * processi nodo, uno per il return state dei nodi.
     * 
     */


    args[0] = NODE_FILE;

    nodes_pid = malloc(SO_NODES_NUM * sizeof(* nodes_pid));
    tx_nodes = malloc(SO_NODES_NUM * sizeof(* tx_nodes));

    for(i=0; i<SO_NODES_NUM; i++) {
        switch(nodes_pid[i] = fork()) {
            case -1:
                TEST_ERROR;
                break;
            case 0:
                execve(NODE_FILE, args, NULL);
                TEST_ERROR;
                break;
            default:
                break;
        }
    }

    /*
     * Questa parte di codice si occupa dell'invio
     * dei pid dei processi (sia utente che nodo) ai 
     * processi utente, in modo che possano avere
     * una conoscenza totale dei processi vivi, in questo
     * modo possono estrarre gli utenti a cui inviare il
     * denaro e i nodi a cui inviare la transizione
     * che verra' poi elaborata.
     * 
     */


    for(j=0; j<SO_USERS_NUM; j++) {
        msg_pid.mtype = users_pid[j];
        for(i=0; i<SO_USERS_NUM; i++) {
            msg_pid.num_pid = users_pid[i];

            sops.sem_num=0;
            sops.sem_flg=0;
            sops.sem_op=-1; /* Diminuisco il semaforo del produttore */
            semop(s_id, &sops, 1);
            TEST_ERROR;

            msgsnd(q_id, &msg_pid, sizeof(msg_pid.num_pid), 0);
            TEST_ERROR;

            sops.sem_num=4;
            sops.sem_flg=0;
            sops.sem_op= 1; /* Incremento il semaforo del consumatore */
            semop(s_id, &sops, 1);
            TEST_ERROR;
        }
    }



    printf("-- MASTER [%d]: HO MANDATO TUTTI I PID FIGLI.\n", getpid());

    
    for(j=0; j<SO_USERS_NUM; j++) {
        msg_pid.mtype = users_pid[j]*2;
        for(i=0; i<SO_NODES_NUM; i++) {
            msg_pid.num_pid = nodes_pid[i];

            sops.sem_num=0;
            sops.sem_flg=0;
            sops.sem_op=-1; 
            semop(s_id, &sops, 1);
            TEST_ERROR;

            msgsnd(q_id, &msg_pid, sizeof(msg_pid.num_pid), 0);

            sops.sem_num=4;
            sops.sem_flg=0;
            sops.sem_op= 1; 
            semop(s_id, &sops, 1);
            TEST_ERROR;
        }
    }

    printf("-- MASTER [%d]: HO MANDATO TUTTI I PID NODO.\n", getpid());

    /* Semaforo per gli utenti, dato che potrebbero perdere tempo per
       ricevere tutti i pid, il master li aspetta. */

    sops.sem_num = 3;
    sops.sem_flg = 0;
    sops.sem_op = -(SO_USERS_NUM);
    semop(s_id, &sops, 1);
    TEST_ERROR;
    

    /*
     * Setto l'handler e la maschera per i segnali,
     * i segnali mascherati saranno SIGALRM e SIGINT:
     * SIGALRM verra' usato per gestire il timeout del timer,
     * SIGINT verra' usato invece per gestire la terminazione
     * della simulazione in caso di libro mastro pieno.
     * Infine setto il timer a SO_SIM_SEC secondi.
     * 
     */

    sa.sa_handler = handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&my_mask);
    sa.sa_mask = my_mask;
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    alarm(SO_SIM_SEC);

    printf("-- MASTER [%d]: FACCIO PARTIRE IL TIMER!\n", getpid());
    
    /* Semaforo che sblocca sia utenti che nodi, inizia la simulazione. */
    sops.sem_num = 2;
    sops.sem_flg = 0;
    sops.sem_op = SO_USERS_NUM + SO_NODES_NUM;
    semop(s_id, &sops, 1);
    TEST_ERROR;

    /*
     * Adesso entriamo nel ciclo while del master.
     *
     * Nella sezione (1) controllo che gli utenti terminati prematuramente non siano
     * uguali al numero totale di utenti, cio' significherebbe che
     * tutti gli utenti hanno terminato, e percio' bisogna terminare
     * la simulazione.
     * 
     * Nella sezione (2) ciclo tutto il libro mastro, ottenendo cosi' il budget di ogni
     * processo utente (attraverso le entrate e le uscite),
     * dopodiche' salvo in un array dinamico il process ID e il budget 
     * di ogni utente, ordino l'array (in modo crescente) cosi' da poter stampare
     * i processi utente piu' significativi, ossia i 4 utenti con piu' budget e i 4 utenti
     * con minor budget.
     * 
     * Nella sezione (3) effettuo la stessa operazione della sezione (2), con la differenza
     * che questa volta il budget calcolato e' quello dei nodi. 
     * 
     * Infine riposo per 1 secondo, cosi' da effetuare le stampe ogni secondo.
     * 
     * Il ciclo continua affinche' ($flag) sia vero (ossia > 0). ($flag) diventa falso (== 0)
     * solamente se si verificano una delle tre casistiche per cui la terminazione deve finire.
     * 
     */

    while(flag) {

        /*---------------(1)--------------------*/
        if (my_data->premature == SO_USERS_NUM) {
            printf("-- MASTER [%d]: TUTTI GLI USERS SONO USCITI, TERMINO LA SIMULAZIONE...\n", getpid());
            printf("\n");
            flag = 0;
            break;
        }
        /*------------END(1)END-----------------*/

        /*---------------(2)--------------------*/
        for(k=0; k < SO_USERS_NUM; k++) {
            budget = SO_BUDGET_INIT;
            for(i=0; i<SO_REGISTRY_SIZE; i++) {
                for (j=0; j<SO_BLOCK_SIZE-1; j++) {
                    if(my_data->ledger.matrix_tx[i].array_tx[j].receiver == users_pid[k]) {
                        budget = budget + my_data->ledger.matrix_tx[i].array_tx[j].quantity;
                    } else if (my_data->ledger.matrix_tx[i].array_tx[j].sender == users_pid[k]) {
                        budget = budget - (my_data->ledger.matrix_tx[i].array_tx[j].quantity + my_data->ledger.matrix_tx[i].array_tx[j].reward);
                    }
                } 
            }
            user_pinfo[k].num_pid = users_pid[k];
            user_pinfo[k].budget = budget;
        }

        qsort(user_pinfo, SO_USERS_NUM, sizeof(pinfo), compare);
        for (i=SO_USERS_NUM-1; i > (SO_USERS_NUM-5); i--) {
            printf("-- MASTER [%d]: LO USER [%d] HA (%ld) DI BUDGET.\n", getpid(), user_pinfo[i].num_pid, user_pinfo[i].budget);
        }
        for (i=4; i > 0; i--) {
            printf("-- MASTER [%d]: LO USER [%d] HA (%ld) DI BUDGET.\n", getpid(), user_pinfo[i].num_pid, user_pinfo[i].budget);
        }

        printf("/--------------------------------------------------------/\n");
        /*------------END(2)END-----------------*/

        /*---------------(3)--------------------*/
        for(k=0; k<SO_NODES_NUM; k++) {
            budget = 0;
            for (i=0; i<SO_REGISTRY_SIZE; i++) {
                j = SO_BLOCK_SIZE-1;
                if(my_data->ledger.matrix_tx[i].array_tx[j].receiver == nodes_pid[k]) {
                    budget = budget + my_data->ledger.matrix_tx[i].array_tx[j].quantity;
                }
            }
            node_pinfo[k].num_pid = nodes_pid[k];
            node_pinfo[k].budget = budget;
        }

        qsort(node_pinfo, SO_NODES_NUM, sizeof(pinfo), compare);
        for (i=SO_NODES_NUM-1; i > (SO_NODES_NUM-5); i--) {
            printf("-- MASTER [%d]: IL NODO [%d] HA (%ld) DI BUDGET.\n", getpid(), node_pinfo[i].num_pid, node_pinfo[i].budget);
        }
        for (i=4; i > 0; i--) {
            printf("-- MASTER [%d]: IL NODO [%d] HA (%ld) DI BUDGET.\n", getpid(), node_pinfo[i].num_pid, node_pinfo[i].budget);
        }

        printf("-- MASTER [%d]: GLI UTENTI ATTIVI SONO (%d).\n", getpid(), (SO_USERS_NUM-my_data->premature));

        printf("-- MASTER [%d]: NUMERI DI BLOCCHI NEL LIBRO MASTRO (%d)\n", getpid(), my_data->ledger_index);
        printf("~__________________________________________________________~\n");
        printf("\n");
        /*------------END(3)END-----------------*/

        sleep(1);

    }

    printf("   -----------------------------------------------------------------\n");
    printf(" |                                                                   |\n" );
    printf(" | SIMULAZIONE TERMINATA SIMULAZIONE TERMINATA SIMULAZIONE TERMINATA |\n");
    printf(" |                                                                   |\n");
    printf("   -----------------------------------------------------------------\n");
    printf("\n");

    /* Fuori dal ciclo while, quindi la simulazione deve finire,
       uccido i processi figli */

    for(j=0; j<SO_NODES_NUM; j++) {
        kill(nodes_pid[j], SIGTERM);
    }

    for(i=0; i<SO_USERS_NUM; i++) {
        kill(users_pid[i], SIGTERM);
    }

    /* Faccio la while, e stampo le transazioni in sospeso per ogni nodo */

    while ((child_pid = wait(&status)) != -1) {
		/*dprintf(2,"MASTER %d: CHILD (PID=%d) terminated with status 0x%04X\n", 
		getpid(),
		child_pid,
		status);*/
        for(i=0; i<SO_NODES_NUM; i++) {
            if (nodes_pid[i] == child_pid) {
                tx_nodes[i] = WEXITSTATUS(status);
                printf("-- MASTER [%d]: IL NODO [%d] AVEVA (%d) TRANSAZIONI IN SOSPESO.\n", getpid(), nodes_pid[i], WEXITSTATUS(status));
            }
        }
	}
    printf("~__________________________________________________________~\n");
    printf("\n");

    /*
     * Tutti i figli hanno terminato, stampo lo stato finale della simulazione,
     * dealloco tutte le risorse IPC e faccio la free di tutti gli array allocati
     * dinamicamente, dopodiche' termino.
     * 
     */

    for(i=0; i<SO_USERS_NUM; i++) {
        printf("-- MASTER [%d]: L'UTENTE [%d] HA TERMINATO CON (%ld) DI BUDGET.\n", getpid(), user_pinfo[i].num_pid, user_pinfo[i].budget);
    }

    printf("~__________________________________________________________~\n");
    printf("\n");

    for(i=0; i<SO_NODES_NUM; i++) {
        printf("-- MASTER [%d]: IL NODO [%d] HA TERMINATO CON (%ld) DI BUDGET.\n", getpid(), node_pinfo[i].num_pid, node_pinfo[i].budget);
    }

    printf("~__________________________________________________________~\n");
    printf("\n");
    
    printf("-- MASTER [%d]: GLI UTENTI CHE HANNO FINITO PREMATURAMENTE SONO (%d)\n", getpid(), my_data->premature);

    printf("-- MASTER [%d]: IL NUMERO DI BLOCCHI SUL LEDGER SONO (%d)\n", getpid(), my_data->ledger_index);

    msgctl(q_id, IPC_RMID, NULL);
    semctl(s_id, 0, IPC_RMID);

    free(users_pid);
    free(nodes_pid);
    free(node_pinfo);
    free(user_pinfo);
    free(tx_nodes);

    printf("-- MASTER [%d]: TUTTE LE RISORSE DEALLOCATE, TERMINO LA SIMULAZIONE.\n", getpid());
    
    return(0);
}

/*
 * La funzione handle_signal si occupa di gestire i segnali (SIGALRM E SIGINT)
 * ricevere il segnale SIGALRM significherebbe che il tempo della simulazione e' scaduto
 * percio' 
 *
 */

void handle_signal(int signal) {
    /* SONO TRASCORSI SO_SIM_SEC SECONDI, CHIUDO LA SIMULAZIONE. */
    if (signal == SIGALRM) {
        printf("-- MASTER [%d]: IL TEMPO E' SCADUTO, TERMINO LA SIMULAZIONE...\n", getpid());
        printf("\n");
        flag = 0;
    /* IL MASTER HA RICEVUTO UN SEGNALE DA UN NODO, INDICANDO CHE IL LEDGER E' PIENO. */
    } else if (signal == SIGINT) {
        printf("-- MASTER [%d]: IL LEDGER E' PIENO E NON PUO' CONTENERE PIU' TX, CHIUDO LA SIMULAZIONE...\n", getpid());
        printf("\n");
        flag = 0;
    }
}

/*
 *  QUESTA FUNZIONE COMPARE E' STATA PRESA
 *  SU INTERNET, PER POTER EFFETTUARE LA
 *  FUNZIONE QSORT PRESENTE IN STDLIB.H
 * 
 */

int compare(const void * a, const void * b) {
    const pinfo * left = (const pinfo * )a;
    const pinfo * right = (const pinfo * )b;

    return (right->budget < left->budget) - (left->budget < right->budget);
}