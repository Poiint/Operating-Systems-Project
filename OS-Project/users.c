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

void handle_signal(int signal);

int mk_tx=0;

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


    int m_id, s_id, q_id;
    int i, j, rcv_user, tx_node, default_retry;
    int *users_pid, *nodes_pid;
    pid_t my_pid, rnd_user, rnd_node;
    int my_index, struct_size;
    struct sembuf sops;
    struct shm_data * my_data;
    struct msgbuff tx_message;
    struct transaction boh;
    struct msgbuff * msg_arr;
    struct msg_pid msg_pid;
    struct sigaction sa;
    struct timespec tp, tm1, tm2;
    sigset_t my_mask;
    int message;
    int rcv_pid;
    long rcv_type;
    int my_budget, init_budget;
    FILE * config_file;

    config_file = fopen("config.txt", "r");

    if (config_file == NULL) {
        printf("MASTER %d: COULD NOT OPEN CONFIGURATION FILE\n", getpid());
        exit(-1);
    }

    fscanf(config_file, "%d %d %d %d %d %d %d %d %d %d %d" , &SO_USERS_NUM, &SO_NODES_NUM, &SO_BUDGET_INIT, &SO_REWARD,
    &SO_MIN_TRANS_GEN_NSEC, &SO_MAX_TRANS_GEN_NSEC, &SO_RETRY, &SO_TP_SIZE, &SO_MIN_TRANS_PROC_NSEC, &SO_MIN_TRANS_PROC_NSEC, &SO_SIM_SEC);

    default_retry = SO_RETRY;

    /*
     *  Alloco dinamicamente alcune strutture, che mi serviranno
     *  per ricevere messaggi dal processo padre (il master) e 
     *  per poter salvare i pid degli altri utenti e dei nodi.
     *
     * 
     */

    msg_arr = malloc(sizeof(*msg_arr) * SO_TP_SIZE);
    users_pid = malloc(sizeof(*users_pid) * SO_USERS_NUM-1);
    nodes_pid = malloc(sizeof(*nodes_pid) * SO_NODES_NUM);

    /*
     *  Converto le stringhe presenti nell'array args
     *  per ottenere l'ID delle risorse IPC, cosi' da 
     *  poterci lavorare sopra.
     * 
     */

    m_id = atoi(argv[1]); /* Shared memory */
    s_id = atoi(argv[2]); /* Semaphore */
    q_id = atoi(argv[3]); /* Message queue */

    my_pid = getpid();

    TEST_ERROR;

    /* 
     * Proprio come nel master, faccio l'attach e il detach 
     * subito dopo, dato che il deatch verra' fatto solamente
     * quando tutti i processi usciranno.
     * 
     */

    my_data = shmat(m_id, NULL, 0);
    shmctl(m_id, IPC_RMID, NULL);
    TEST_ERROR;

    /*
     * Setto l'handler e la maschera, per i segnali SIGTERM e SIGUSR1:
     * SIGTERM lo ricevero' quando sara' l'ora di chiudere la simulazione;
     * SIGUSR1 lo ricevero' quando dovro' effettuare una transazione in piu'.
     * Applico il flag SA_RESTART perche' il segnale SIGUSR1 potrebbe interrompere
     * qualche system call (anche il segnale SIGTERM, ma li dovro' terminare).
     * 
     */

    sa.sa_handler = handle_signal;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&my_mask);
    sa.sa_mask = my_mask;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    
    /*
     * In questa sezione ricevo i pid utente dal master, setto il
     * ($rcv_type) al mio pid cosi' posso ricevere tutti i messaggi
     * inoltrati a me.
     *
     */

    rcv_type = my_pid;
    i=0;
    while(i < SO_USERS_NUM-1) {

        sops.sem_num=4;
        sops.sem_flg=0;
        sops.sem_op=-1; 
        semop(s_id, &sops, 1);
        TEST_ERROR;


        message = msgrcv(q_id, &msg_pid, sizeof(msg_pid.num_pid), rcv_type, 0);
        TEST_ERROR;

        sops.sem_num=0;
        sops.sem_flg=0;
        sops.sem_op= 1; 
        semop(s_id, &sops, 1);
        TEST_ERROR;

        if (msg_pid.num_pid != my_pid) {
            users_pid[i] = msg_pid.num_pid;
            i++;
        }
    }

    /*
     * In questa sezione ricevo i pid nodo dal master, setto il
     * ($rcv_type) al mio pid * 2 cosi' posso ricevere tutti i messaggi
     * inoltrati a me, differenziando pero' da quelli precedenti.
     *
     */
    
    rcv_type = my_pid * 2;
    for(i=0; i<SO_NODES_NUM; i++) {

        sops.sem_num=4;
        sops.sem_flg=0;
        sops.sem_op=-1;
        semop(s_id, &sops, 1);
        TEST_ERROR;


        message = msgrcv(q_id, &msg_pid, sizeof(msg_pid.num_pid), rcv_type, 0);

        sops.sem_num=0;
        sops.sem_flg=0;
        sops.sem_op= 1;
        semop(s_id, &sops, 1);
        TEST_ERROR;

        nodes_pid[i] = msg_pid.num_pid;
    }
    

    /* Pid ricevuti con successo dal master ! 
     * Setto ($i) a 0 e il mio budget a quello letto a runtime,
     * mi sincronizzo con il master e con il nodo ed entro in loop.
     * La funzione srand serve per settare un seed per la creazione
     * di numeri random.
     */

    srand(time(NULL) + getpid());
    i=0;
    my_budget = SO_BUDGET_INIT;
    
    /* Semaforo per sincronizzarmi con il master */
    sops.sem_num = 3;
    sops.sem_flg = 0;
    sops.sem_op = 1;
    semop(s_id, &sops, 1);
    TEST_ERROR;
    
    /* Semaforo per sincronizzarmi con il master e i nodi */
    sops.sem_num = 2;
    sops.sem_flg = 0;
    sops.sem_op = -1;
    semop(s_id, &sops, 1);
    TEST_ERROR;

    /*
     * Nel seguente ciclo while sono commentate le sezioni del ciclo vitale 
     * del processo utente. 
     * Il passo 1.a e' molto semplice, calcolo le entrate 
     * solo dei nuovi blocchi che vengono creati.
     * Nel mio caso non tengo conto le uscite, dato che sono gia' calcolate 
     * dagli importi sottratti dalle transazioni spedite.
     * 
     * Il passo 1.b e' un po' piu' strutturato: l'utente controlla il proprio budget,
     * se supera il controllo crea una tx seguendo le linee guida date. 
     * Dopodiche' invia la tx al nodo scelto, ma la system call potrebbe fallire nel caso
     * l'utente riceva un segnale SIGUSR1, quindi controllo che ($errno) sia diverso da EINTR,
     * se non lo e' allora devo rinviare la transizione. In questa implementazione, non tengo
     * conto il caso in cui la transaction pool del nodo sia piena, dato che questo tipo di
     * implementazione non permette mai alla transaction pool di riempirsi e quindi una transazione
     * inviata al nodo non viene mai rifiutata.
     * 
     * Dopodiche' controllo la variabile ($mk_tx). Se e' ver (> 0) allora ho ricevuto un segnale
     * SIGUSR1 e quindi devo effettuare una transazione aggiuntiva. Faccio lo stesso tipo di controllo al budget,
     * nel caso il controllo fallisse, sia nel primo caso che nel caso di tx aggiuntiva, decremento il
     * valore di ($SO_RETRY). Se ($SO_RETRY) raggiunge il valore falso (== 0) allora ho concluso la mia esecuzione,
     * percio' incremento una variabile in memoria condivisa (in mutua esclusione) che tiene conto degli utenti che hanno terminato
     * prematuramente.
     * 
     * Il passo 2 e' molto semplice, si tratta solamente dell'attesa non attiva (nanosleep) del valore random letto
     * tra il range delle due variabili lette a runtime, dopodiche' il ciclo ricomincia.
     *
     * 
     * 
     */

    while(1) {
        /*---------------(1.a)--------------------*/

        /* 
         * Calcola il bilancio corrente a partire dal budget iniziale e facendo la somma algebrica delle entrate e delle
         * uscite registrate nelle transazioni presenti nel libro mastro, sottraendo gli importi delle transazioni spedite ma
         * non ancora registrate nel libro mastro.
         * 
         */
        for(i; i<my_data->ledger_index; i++) {
            for(j=0; j<SO_BLOCK_SIZE-1; j++) {
                if (my_data->ledger.matrix_tx[i].array_tx[j].receiver == my_pid) {
                    my_budget = my_budget + my_data->ledger.matrix_tx[i].array_tx[j].quantity;
                } else {} /* if (my_data->ledger.matrix_tx[i].array_tx[j].sender == my_pid) {
                    my_budget = my_budget - my_data->ledger.matrix_tx[i].array_tx[j].quantity;
                } else {} */
            }
        }

        /*------------END(1.a)END-------------------*/

        /*---------------(1.b)--------------------*/
        /* 
         * Se il bilancio `e maggiore o uguale a 2, il processo estrae a caso:
         * – Un altro processo utente destinatario a cui inviare il denaro
         * – Un nodo a cui inviare la transazione da processare
         * – Un valore intero compreso tra 2 e il suo bilancio suddiviso in questo modo:
         * * il reward della transazione pari ad una percentuale SO_REWARD del valore estratto, arrotondato,
         * con un minimo di 1,
         * * l’importo della transazione sar`a uguale al valore estratto sottratto del reward
         * Esempio: l’utente ha un bilancio di 100. Estraendo casualmente un numero fra 2 e 100, estrae 50.
         * Se SO_REWARD `e pari al 20 (ad indicare un reward del 20%) allora con l’esecuzione della transazio-
         * ne l’utente trasferir`a 40 all’utente destinatario, e 10 al nodo che avr`a processato con successo la
         * transazione.
         * Se il bilancio `e minore di 2, allora il processo non invia alcuna transazione
         */

        if(my_budget > 2) {
            SO_RETRY = default_retry;
            clock_gettime(CLOCK_REALTIME, &tp);
            rnd_user = rand() % (SO_USERS_NUM-1);
            tx_message.tx.timestamp = tp.tv_nsec;
            tx_message.tx.sender = my_pid;
            tx_message.tx.receiver = users_pid[rnd_user];
            tx_message.tx.quantity = rand()%(my_budget-1)+2;
            tx_message.tx.reward = ((tx_message.tx.quantity * SO_REWARD) / 100);
            tx_message.tx.quantity = tx_message.tx.quantity - tx_message.tx.reward;
            rnd_node = rand() % SO_NODES_NUM;
            tx_message.mtype = nodes_pid[rnd_node];
            my_budget = my_budget - (tx_message.tx.quantity+tx_message.tx.reward);
            msgsnd(q_id, &tx_message, sizeof(tx_message.tx), 0);
            if (errno == EINTR) {
                msgsnd(q_id, &tx_message, sizeof(tx_message.tx), 0);
                errno = 0;
            }
            TEST_ERROR;

            if(mk_tx && my_budget > 2) {
                SO_RETRY = default_retry;
                clock_gettime(CLOCK_REALTIME, &tp);
                rnd_user = rand() % (SO_USERS_NUM-1);
                tx_message.tx.timestamp = tp.tv_nsec;
                tx_message.tx.sender = my_pid;
                tx_message.tx.receiver = users_pid[rnd_user];
                tx_message.tx.quantity = rand()%(my_budget-1)+2;
                tx_message.tx.reward = ((tx_message.tx.quantity * SO_REWARD) / 100);
                tx_message.tx.quantity = tx_message.tx.quantity - tx_message.tx.reward;
                rnd_node = rand() % SO_NODES_NUM;
                tx_message.mtype = nodes_pid[rnd_node];
                my_budget = my_budget - (tx_message.tx.quantity+tx_message.tx.reward);
                msgsnd(q_id, &tx_message, sizeof(tx_message.tx), 0);
                TEST_ERROR;
                mk_tx = 0;
                printf("-- USER [%d]: TX IN PIU' CREATA CON SUCCESSO!\n", getpid());
                printf("\n");
            } else {
                if (mk_tx) {
                    printf("-- USER [%d]: NON HO ABBASTANZA BUDGET PER FARE UN'ALTRA TX!\n", my_pid);
                    printf("\n");
                    SO_RETRY--;
                    mk_tx=0;
                }
            }
        } else {
            SO_RETRY--;
            if (!SO_RETRY) {
                /*printf("USER %d: SO_RETRY AL LIMITE, ESCO...\n",getpid());*/
                sops.sem_num=1;
                sops.sem_flg=0;
                sops.sem_op=-1;
                semop(s_id, &sops, 1);

                my_data->premature++;

                sops.sem_num=1;
                sops.sem_flg=0;
                sops.sem_op =1;
                semop(s_id, &sops, 1);

                exit(EXIT_SUCCESS);
            }
        }

        /*------------END(1.b)END-----------------*/

        /*---------------(2)--------------------*/

        /*
         * Invia al nodo estratto la transazione e attende un intervallo di tempo (in nanosecondi) estratto casualmente
         * tra SO_MIN_TRANS_GEN_NSEC e massimo SO_MAX_TRANS_GEN_NSEC.
         */

        tm1.tv_sec = 0;
        tm1.tv_nsec = rand()%(SO_MAX_TRANS_GEN_NSEC-SO_MIN_TRANS_GEN_NSEC+1) + SO_MIN_TRANS_GEN_NSEC;
        nanosleep(&tm1, &tm2);

        /*------------END(2)END-----------------*/
    }
    printf("USER %d: YOU SHOULD NEVER SEE THIS\n", getpid());
    exit(EXIT_FAILURE);
}

/*
 * La funzione handle_signal e' l'handler dei segnali.
 * In caso di segnale SIGTERM allora significa che la simulazione deve terminare.
 * In caso di segnale SIGUSR1 allora significa che devo effettuare una transazione
 * in piu' nel ciclo while.
 * 
 * Nell'handler ho cercato di dare meno responsabilita' possibile, dato che spesso
 * possono capitare comportamenti indefiniti. Percio' ho optato nell'usare un flag
 * per sbloccare alcune parti del codice,
 * in questo modo ho la certezza di non avere comportamenti inaspettati.
 * 
 * 
 */

void handle_signal(int signal) {
    if (signal == SIGTERM) {
        exit(EXIT_SUCCESS);
    } else if (signal == SIGUSR1) {
        mk_tx = 1;
    }
}