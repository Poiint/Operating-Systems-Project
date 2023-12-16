#include <errno.h>
#include <sys/types.h>
#include <string.h>
#define TEST_ERROR    if (errno) {dprintf(STDERR_FILENO,		\
					  "%s:%d: PID=%5d: Error %d (%s)\n", \
					  __FILE__,			\
					  __LINE__,			\
					  getpid(),			\
					  errno,			\
					  strerror(errno));}

/*
 * Le costanti ($SO_BLOCK_SIZE) e ($SO_REGISTRY_SIZE) vengono
 * lette in tempo di compilazione. 
 * Il libro mastro e' composto da due sottostrutture:
 * Una e' array_transaction e l'altra transaction.
 * Array_transaction e' un array di transazioni, mentre 
 * matrix_transaction e' un array di array_transaction.
 * Il libro mastro quindi e' di tipo matrix_transaction.
 * Quindi array_transaction puo' essere visto come un blocco,
 * mentre matrix_transaction come un blocco di blocchi.
 * 
 * Una transazione e' formata da 5 parametri:
 * timestamp, receiver, sender, quantity e reward.
 *
 * Infine abbiamo alcune strutture extra utilizzate dai processi
 * master, utente e nodo per inviare e ricevere data dagli altri
 * processi.
 *
 */

#define SO_BLOCK_SIZE 10
#define SO_REGISTRY_SIZE 10000

struct transaction {
	int timestamp;
	pid_t receiver;
	pid_t sender;
	unsigned int quantity;
	int reward;
};

struct array_transaction {
	struct transaction array_tx[SO_BLOCK_SIZE];
};

struct matrix_transaction {
	struct array_transaction matrix_tx[SO_REGISTRY_SIZE];
};

struct msgbuff {
	long mtype;
	struct transaction tx;
};

struct msg_pid {
	long mtype;
	pid_t num_pid;
};

typedef struct proc_info {
	pid_t num_pid;
	long budget;
} pinfo;

struct shm_data {
	int premature;
	unsigned int ledger_index;
	struct matrix_transaction ledger;
};
