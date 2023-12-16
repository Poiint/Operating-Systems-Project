#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>
#include <time.h>
#include "support.h"

#define NO_SENDER -1

void handle_signal(int signal);

int i=0;

int main(int argc, char* argv[]) {
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

    int s_id, m_id, q_id;
    int my_index, my_pid,j=0, my_budget;
    long rcv_type;
    long rw_blck=0;
    struct sigaction sa;
    struct sembuf sops;
    struct shm_data * my_data;
    struct array_transaction block_tx;
    struct msgbuff my_msg;
    struct transaction * tx_pool;
    struct timespec tp, tm1, tm2;
    sigset_t my_mask;
    FILE * config_file;

    config_file = fopen("config.txt", "r");

    if (config_file == NULL) {
        printf("MASTER %d: COULD NOT OPEN CONFIGURATION FILE\n", getpid());
        exit(-1);
    }

    fscanf(config_file, "%d %d %d %d %d %d %d %d %d %d %d" , &SO_USERS_NUM, &SO_NODES_NUM, &SO_BUDGET_INIT, &SO_REWARD,
    &SO_MIN_TRANS_GEN_NSEC, &SO_MAX_TRANS_GEN_NSEC, &SO_RETRY, &SO_TP_SIZE, &SO_MIN_TRANS_PROC_NSEC, &SO_MIN_TRANS_PROC_NSEC, &SO_SIM_SEC);

    /*
     * Alloco dinamicamente la transaction_pool, grande quanto
     * la variabile ($SO_TP_SIZE) letta a runtime.
     * Dopodiche', come nello user, ottengo i vari ID delle risorse
     * IPC dall'array args, faccio l'attach e la detach della 
     * memoria condivisa
     *
     */

    tx_pool = malloc(sizeof(struct transaction) * SO_TP_SIZE);

    m_id = atoi(argv[1]);
    s_id = atoi(argv[2]);
    q_id = atoi(argv[3]);
    TEST_ERROR;

    my_data = shmat(m_id, NULL, 0);
    shmctl(m_id, IPC_RMID, NULL);
    TEST_ERROR;

    my_pid = getpid();

    /*
     * Setto l'handler e la maschera solo per il segnale SIGTERM, che 
     * indica che la simulazione e' finita.
     * 
     */

    sa.sa_handler = handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&my_mask);
    sa.sa_mask = my_mask;
    sigaction(SIGTERM, &sa, NULL);

    /*
     * Setto il seed, imposto il ($rcv_type) al mio pid, e setto il mio
     * budget a 0.
     * 
     */

    srand(time(NULL) + getpid());
    rcv_type = my_pid;
    my_budget=0;

    /* Semaforo per sincronizzarmi con gli utenti e il master */
    sops.sem_num = 2;
    sops.sem_flg = 0;
    sops.sem_op = -1;
    semop(s_id, &sops, 1);
    TEST_ERROR;

    /*
     * Nella sezione (1) viene effettuata la crazione di un blocco candidato.
     * Questo avviene ciclando all'interno del loop affinche' non ho raggiunto
     * un numero di transazioni pari a ($SO_BLOCK_SIZE -1). Dopodiche' creo una 
     * transazione di reward che somma tutte le reward delle transazioni precedenti.
     * 
     * Nella sezione (2) si simula l'elaborazione di un blocco attraverso una
     * attesa non attiva (nanosleep)
     * 
     * Nella sezione (3) si scrive nel libro mastro un blocco di transazioni all'indice corrente,
     * il nodo in questione si occupa anche di incrementare l'indice del libro mastro.
     *
     */

    while(1) {

        /*---------------(1)--------------------*/

        /*
         *  Creazione di un blocco candidato:
         *       – Estrazione dalla transaction pool di un insieme di SO_BLOCK_SIZE−1 transazioni non ancora presenti nel
         *       libro mastro
         *       – Alle transazioni presenti nel blocco, il nodo aggiunge una transazione di reward, con le seguenti caratte-
         *        ristiche:
         *           * timestamp: il valore attuale di clock_gettime(...)
         *           * sender : -1 (definire una MACRO...)
         *           * receiver : l’dentificatore del nodo corrente
         *           * quantit`a: la somma di tutti i reward delle transazioni incluse nel blocco
         *           * reward : 0
         *  
         */

        msgrcv(q_id, &my_msg, sizeof(my_msg.tx), rcv_type, 0);
        TEST_ERROR;
        tx_pool[i] = my_msg.tx;
        rw_blck = rw_blck + my_msg.tx.reward;

        if(i+1 == SO_BLOCK_SIZE) {
            clock_gettime(CLOCK_REALTIME, &tp);
            tx_pool[i].timestamp = tp.tv_nsec;
            tx_pool[i].sender = NO_SENDER;
            tx_pool[i].receiver = my_pid;
            tx_pool[i].quantity = rw_blck;
            tx_pool[i].reward = 0;
            my_budget = my_budget + rw_blck;

            /* Adesso il blocco e' completo! procedo a scrivere il blocco... */
            for(j=0; j<SO_BLOCK_SIZE;j++) {
                block_tx.array_tx[j] = tx_pool[j];
            }

        /*------------END(1)END-----------------*/

        /*---------------(2)--------------------*/

            /*
             *   Simula l’elaborazione di un blocco attraverso una attesa non attiva di un intervallo temporale casuale espresso
             *   in nanosecondi compreso tra SO_MIN_TRANS_PROC_NSEC e SO_MAX_TRANS_PROC_NSEC.
             */

            tm1.tv_sec = 0;
            tm1.tv_nsec = rand()%(SO_MAX_TRANS_PROC_NSEC-SO_MIN_TRANS_PROC_NSEC+1) + SO_MIN_TRANS_PROC_NSEC;
            nanosleep(&tm1, &tm2);

        /*------------END(2)END-----------------*/

        /*---------------(3)--------------------*/

            /*
             *   Una volta completata l’elaborazione del blocco, scrive il nuovo blocco appena elaborato nel libro mastro, ed
             *   elimina le transazioni eseguite con successo dal transaction pool.
             */
           
            sops.sem_num = 5;
            sops.sem_op = -1;
            sops.sem_flg = 0;
            semop(s_id, &sops, 1);
            TEST_ERROR;

            if(my_data->ledger_index < SO_REGISTRY_SIZE) {
                my_data->ledger.matrix_tx[my_data->ledger_index] = block_tx;
                my_data->ledger_index++;
            } else {
                kill(getppid(), SIGINT);
                exit(i);
            }

            TEST_ERROR;
            sops.sem_num = 5;
            sops.sem_op = 1;
            sops.sem_flg = 0;
            semop(s_id, &sops, 1);
            TEST_ERROR;

            /* Svuoto la transaction_pool dalle transazioni in esso presenti, e anche l'indice ($i) */
            memset(tx_pool, 0, SO_BLOCK_SIZE*sizeof(tx_pool[0]));
            i=0;

            /*------------END(3)END-----------------*/

        } else {
            i++;
        }
    }
    printf("NODE %d: YOU SHOULD NEVER SEE THIS\n", getpid());
    exit(EXIT_FAILURE);
}

/*
 * L'handler dei segnali maschera solamente
 * il segnale SIGTERM, ricevuto dal padre.
 * Nel caso di segnale SIGTERM, esco con il valore
 * delle transizioni che mi rimangono nella transaction 
 * pool, cosi' da comunicarla al padre.
 * 
 */

void handle_signal(int signal) {
    if (signal == SIGTERM) {
        exit(i);
    }
}