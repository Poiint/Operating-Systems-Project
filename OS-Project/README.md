# My university project for the operating system project

It is intended to simulate a ledger containing data of monetary transactions between different users. To this end they are
the following processes are present:
• (master.c) a master process that manages the simulation, the creation of other processes, etc.
• (users.c) SO_USERS_NUM user processes that can send money to other users through a transaction
• (nodes.c) SO_NODES_NUM node processes that process, for a fee, received transactions.


## Transactions 
A transaction is characterized by the following information:
• transaction timestamp with nanosecond resolution (see clock_gettime(...) function)
• sender (implicit, as it is the user who generated the transaction)
• receiver, user receiving the sum
• amount of money sent
• reward, money paid by the sender to the node that processes the transaction
The transaction is sent by the user process that generates it to one of the node processes, chosen at random.

## Configuration
The following parameters are read at runtime, from file, from environment variables, or from stdin (at discretion
of the students):
• SO_USERS_NUM: Number of user processes
• SO_NODES_NUM: Number of node processes
• SO_BUDGET_INIT: initial budget of each user process
• SO_REWARD: the percentage of reward paid by each user for processing a transaction
• SO_MIN_TRANS_GEN_NSEC, SO_MAX_TRANS_GEN_NSEC: minimum and maximum time value (expressed in nano-
seconds) that elapses between the generation of one transaction and the next by a user
5
• SO_RETRY, maximum number of consecutive failures in generating transactions after which a process
user ends
• SO_TP_SIZE: maximum number of transactions in the node process transaction pool
• SO_MIN_TRANS_PROC_NSEC, SO_MAX_TRANS_PROC_NSEC: minimum and maximum value of the simulated time (expressed
in nanoseconds) of processing of a block by a node
• SO_SIM_SEC: simulation duration (in seconds)
• SO_NUM_FRIENDS (max 30 version only): number of nodes friends of the node processes (only for the full version)
• SO_HOPS (max 30 version only): maximum number of forwarding of a transaction to friendly nodes before the
master I created a new node
