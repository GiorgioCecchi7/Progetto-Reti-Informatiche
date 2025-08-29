#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <signal.h>

#define BUFFER_SIZE 2048
#define NUM_QNA 5

// Global

bool registered;
bool end;
char buffer[BUFFER_SIZE] = {0};
int sock;

// Funzioni di utilità, commentate in fondo
void print_welcome();
void register_();
void get_score();
void play_trivia();
void send_mess();
void recv_mess();

// Funzione di gestione del segnale SIGPIPE, ricevuto in caso di chiusura della
// connessione dal server
void sigpipe_handler(){
    printf("Errore: connessione con il server fallita\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in serv_addr;
    int NUM_TOPIC, ret;
    // Tipo certificato per il numero di topic
    uint32_t NUM_TOPIC_net;

    signal(SIGPIPE, sigpipe_handler);
    if(argc != 2) {
        perror("A port number must be specified");
        exit(EXIT_FAILURE);
    }
    // Ciclo completo con connessione, scelta nickame e gioco
    // Si torna all'inizio di questo ciclo in caso di endquiz 
    while(1){
        end = false;
        registered = false;
        // Stampa della scelta iniziale
        print_welcome();
        // Ciclo finche il comando inserito non è tra quelli possibili
        while (1) { 
            fgets(buffer, BUFFER_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = '\0';

            if (strcmp(buffer, "1") == 0){ 
                // Comincia sessione di trivia
                break;
            } else if (strcmp(buffer, "2") == 0) {
                // Esci
                return 0;
            } else {
                printf("Comando errato, inserire nuovamente.\n");
            }   
        }
    
        // Creazione del socket
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Socket failed");
            exit(EXIT_FAILURE);
        }
        // Inizializzione strutture per la connessione al server
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(atoi(argv[1]));

        if( inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
            printf("Invalid address/Address not supported.\n");
            return -1;
        }
        // Connessione al server
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            printf("Invalid address/Address not supported.\n");
            return -1;
        }

        // Registrazione del giocatore
        register_();

        // Ricezione dal server del numero di topic  in formato network
        ret = recv(sock, &NUM_TOPIC_net, sizeof(uint32_t), 0); 
        if (ret < sizeof(uint32_t)){
            printf("Errore nella connessione\n");
            exit(EXIT_FAILURE);
        }
        NUM_TOPIC = ntohl(NUM_TOPIC_net);
 
        while (1) {
            // Ricezione dal server della lista dei topic
            recv_mess();
            // Stampa della lista dei topic
            printf("\n%s", buffer);
            // Ciclo che obbliga l'utente a inserire un comando accettato
            while (1) { 
                // Inserimento del comando da parte dell'utente
                fgets(buffer, BUFFER_SIZE, stdin);
                buffer[strcspn(buffer, "\n")] = '\0';
                // Controllo che il comando inserito sia corretto
                if (strcmp(buffer, "show score") == 0){
                    // Invio al server la stringa show score
                    send_mess();
                    get_score();
                    break;
                } else if (strcmp(buffer, "endquiz") == 0) {
                    // Invio al server la stringa endquiz
                    send_mess();
                    close(sock);
                    end = true;
                    break;
                } else if (atoi(buffer)<= NUM_TOPIC && atoi(buffer)>0) {
                    // Invio al server il topic scelto come stringa
                    send_mess();
                    play_trivia();
                    break;
                } else {
                    printf("Comando errato, inserire nuovamente.\n");
                }   
            }
            if(end)
               break; 
        }
    }
}

// Funzione che stampa le opzioni nei comandi iniziali
void print_welcome(){
    system("clear");
    printf("Trivia Quiz\n");
    printf("+++++++++++++++++++++++++++++\n");
    printf("Menù:\n");
    printf("1 - Comincia una sessione di Trivia\n");
    printf("2 - Esci\n");
    printf("+++++++++++++++++++++++++++++\n"); 
    printf("La tua scelta: ");   
}

// Funzione per la registrazione di un giocatore
void register_(){
    system("clear");
    printf("Trivia Quiz\n");
    printf("+++++++++++++++++++++++++++++\n");
    printf("Scegli un nickname (deve essere univoco): ");
    // Ciclo finche la registrazione non è avvenuta con successo
    while(!registered){
        // Inserimento del nickaname da parte dell'utente
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = '\0';
        // Controllo sul formato del nickname 
        if(strlen(buffer) > 20 || strlen(buffer) < 1){
            printf("Il nickname deve essere tra 1 e 20 caratteri\n");
            printf("Inserire nuovamente: ");
            continue;
        }
        // Invio al server del nickanme
        send_mess();
        // Ricezione esito sulla disponibilita del nickname da parte 
        // del server
        recv_mess();
        if (strcmp(buffer, "Ok") == 0){ 
            printf("Nickname registrato con successo\n");
            registered = true;
        } else {
            printf("Nickname gia esistente, inserire nuovamente: ");
        }
    }
}

// Funzione per la stampa della classifica
void get_score(){
    // Ricezione dal server della classifica gia formattata 
    recv_mess();
    // Stampa della classifica
    printf("%s", buffer);
}
void play_trivia(){
    // Il topic a cui il giocatore ha chiesto di giocare è gia stato mandato 
    // al server nel main prima di entrare in questa funzione.
    // Ricezione dal server dell'esito della richiesta di partecipare a un topic 
    recv_mess();
    // Se la risposta del server è la stringa Error il gicocatore ha già partecipato
    // a quel topic
    if (strcmp(buffer, "Error") == 0){ 
        printf("Hai già partecipato a questo topic, scegline un altro\n");
        // Ritorno al main dove il client potrà inserire un nuovo comando
        return;
    }
    // Il giocatore può partecipare al topic poice non ci ha mai partecipato 
    // Ricezione dal server del titolo del topic
    recv_mess();
    //Stampa del titolo del topic
    printf("%s\n", buffer);
    
    // Loop di scambio domande e risposte tra client e server
    for (int i = 0; i < NUM_QNA; i++) {
        // Ricezione dal server della la domanda 
        recv_mess();
        printf("%s\n",buffer);
        printf("Risposta: ");
        // Inserimento della risposta da parte dell'utente
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = '\0';
        // Invio al server della risposta
        send_mess();
        // Ricezione dal server dell'esito della risposta
        recv_mess();
        // Stampa esito risposta
        printf("%s\n", buffer);
    }
    printf("\n");
    // Ritorno al main dove il client potrà inserire un nuovo comando
}

// Funzione per l'invio di un messaggio
void send_mess(){
    int dim;
    // Tipo certificato per l'invio della dimensione
    uint32_t dimNet;
   
    dim = strlen(buffer);
    // Converto la dimensione del messaggio in formato network
    dimNet = htonl(dim);
    // Invio al server la dimensione del messaggio in formaato network
    send(sock, &dimNet, sizeof(uint32_t), 0);
    // Invio al server la dimensione del messaggio
    send(sock, buffer, dim, 0);
    // Eventuali dissconnessioni da parte del server vengono gestite con la
    // gestione del segnale SIGPIPE che il client riceve
}

// Funzione per la ricezione di un messaggio
void recv_mess(){
    int ret, dim;
    // Tipo certificato per l'invio della dimensione
    uint32_t dimNet;
   
    // Ricevo dal server la lunghezza del messaggio in formato network
    ret = recv(sock, &dimNet, sizeof(uint32_t), 0);
    if (ret <= 0) {
        perror("Errore nella connessione\n");
        close(sock);
        exit(EXIT_FAILURE);
    }
    // Converto la dimensione del messaggio in formato host
    dim = ntohl(dimNet);
    // Ricevo dal server il messaggio
    ret = recv(sock, buffer, dim, 0);
    if (ret <= 0) {
        perror("Errore nella connessione\n");
        close(sock);
        exit(EXIT_FAILURE);
    }
    // Inserisco carattere di fine stringa nel buffer
    buffer[dim] = '\0';
}