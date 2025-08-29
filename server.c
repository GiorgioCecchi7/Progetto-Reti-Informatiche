#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>

#define PORT 1234
#define BUFFER_SIZE 2048
#define NUM_TOPIC 4
#define NUM_QNA 5

// Global

// Struttura dati del giocatore, contiene il nome, due vettori con punteggi e topic
// conclusi, un boleano che tiene traccia di se il giocatore e sempre in gioco
// e un puntatore per realizzare una lista di giocatori
struct player {
    char client_nickname[20];
    int client_score[NUM_TOPIC];
    bool client_finish[NUM_TOPIC];
    bool in_game;
    struct player *next;
};

// Testa della lista dei giocatori
struct player *players_list = NULL;
// Semaforo mutex per l'accesso in mutua esclusione alla lista ove necessario
pthread_mutex_t players_list_mutex;
// Conteggio giocatori attivi
int player_count = 0;

// Funzioni di utilità, commentate in fondo
void *connection_handler(void *);
void print_players();
void print_scores();
void print_topics_available();
void print_topics_completed();
void print_dashboard();
void register_(int , char *, struct player *);
void send_topics(int, char *);
void send_score(int , char *);
void disconnect(int, struct player *);
void play_trivia(int , char *, struct player *);
void send_mess(int , char *);
void recv_mess(int , char *, struct player *);

int main() {
    int server_socket, client_socket;
    struct sockaddr_in my_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t handler_thread;
    pthread_mutex_init(&players_list_mutex, NULL);

    // Creazione del socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Assegnamento indirizzo e porta
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &my_addr.sin_addr);
    
    // Bind del socket
    if (bind(server_socket, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Inizio ascolto per connessioni
    if(listen(server_socket, 32) < 0){
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Stampa della dashboard
    print_dashboard();

    // Accettazione delle connessioni
    while (1) {

        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len)) < 0) {
            perror("Accept failed");
            close(server_socket);
            exit(EXIT_FAILURE);
        }

        // Creazione di un thread ad ogni connessione
        if (pthread_create(&handler_thread, NULL, connection_handler,(void*) &client_socket) < 0) {
            perror("Error in creating thread.\n");
            close(client_socket);
        }
    }

}

// Funzione eseguita dal thread per la gestione del client
void *connection_handler(void *socket_desc) {
    int client_socket = *(int*)socket_desc;
    // Buffer per scambio di messaggi
    char buffer[BUFFER_SIZE] = {0};
    // Creazione della struttura dati del giocatore
    struct player * player_pointer = NULL;
    player_pointer = (struct player *) malloc(sizeof(struct player));
    // Tipo certificato per il numero di topic
    uint32_t NUM_TOPIC_net;

    // Registrazione del client
    register_(client_socket, buffer, player_pointer);

    // Stampa della dashboard
    print_dashboard();
    
    // Invio al client del numero di topic
    // Permette di risparmiare scambi di messaggi nella validazione della scelta 
    // del topic
    NUM_TOPIC_net = htonl(NUM_TOPIC);
    send(client_socket, &NUM_TOPIC_net, sizeof(uint32_t), 0);

    while (1) {
        // Invio della lista dei topic
        send_topics(client_socket, buffer);

        // Ricezione e gestion della scelta del client (scelta di un
        // topic, show score oppure endquiz)
        recv_mess(client_socket, buffer, player_pointer);
        if (strcmp(buffer, "show score") == 0){
            send_score(client_socket, buffer);
        }else if (strcmp(buffer, "endquiz") == 0){
            disconnect(client_socket, player_pointer);
        }else {
            // Il topic scelto è sicuramente esistente, validazione lato client
            play_trivia(client_socket, buffer, player_pointer);
        }
    }
}

// Funzione che stampa la dashboard nel terminale del server
void print_dashboard(){
    system("clear");
    printf("Trivia Quiz\n");
    printf("+++++++++++++++++++++++++++++\n");
    printf("Temi:\n");
    print_topics_available();
    printf("+++++++++++++++++++++++++++++\n\n");
    print_players();
    if(players_list){
        print_scores();
        print_topics_completed();
    }
    printf("------\n");
}

// Funzione che stampa i topic disponibili nella dashboard
void print_topics_available(){
    FILE *fd;
    // Buffer di appoggio per la lettura da file
    // Ipotizzo che la stringa contente titolo e domande di un topic sia 
    // al massimo di 500 caratteri
    char buf[500];
    fd = fopen("questions.txt", "r");
    if (fd == NULL){
        perror("Errore nella lettura dei file.\n");
        exit(EXIT_FAILURE);
    }
    // Leggendo dal file delle domande riga per riga e splittando la riga sulla 
    // base di un carattere speciale (;) si stampano i titoli dei topic 
    // disponibili
    // Il titolo del topic è il primo elemento della riga
    char *row = fgets(buf, sizeof(buf), fd);
    int count = 1;
    while (row != NULL) {
            char * token = strtok(row, ";");
            printf("%d - %s\n", count, token);
            count++;
            row = fgets(buf, sizeof(buf), fd);
    }
    fclose(fd);
}

// Funzione che stampa i giocatori attivi nella dashboard
void print_players(){
    printf("Partecipanti(%i)\n", player_count);
    struct player * ptr_player = players_list;
    while (ptr_player != NULL){
        if(ptr_player->in_game)
            printf("- %s\n", ptr_player->client_nickname);
        ptr_player = ptr_player->next;
    }
    printf("\n");
}

// Funzione che stampa i punteggi dei giocatori nella dashboard
void print_scores(){
    for(int i = 0; i < NUM_TOPIC; i++) {
        printf("Punteggio tema %d\n", i+1);
        for(int j = NUM_QNA; j>= 0; j--){
            struct player * ptr_player = players_list;
            while (ptr_player != NULL){
            if (ptr_player->client_score[i] == j && ptr_player->in_game){
                printf("- %s %d\n", ptr_player->client_nickname, j);
            }
            ptr_player = ptr_player->next;
            }
        }
        printf("\n");
    }
}

// Funzione che stampa i giocatori che hanno completato topic nella dashboard
void print_topics_completed(){
    for(int i = 0; i < NUM_TOPIC; i++) {
        printf("Quiz tema %d completato\n", i+1);
        struct player * ptr_player = players_list;
        while (ptr_player != NULL){
            if (ptr_player->client_finish[i]  && ptr_player->in_game){
                printf("- %s\n", ptr_player->client_nickname);
            }
        ptr_player = ptr_player->next;
        }
        if(i != NUM_TOPIC - 1){
            printf("\n");
        }
    }
}

// Funzione per la registrazione di un giocatore
void register_(int client_socket, char *buffer, struct player *new_player){
    bool registered_ = false;
    // Ciclo finchè il giocatore non è correttamente registrato
    while (!registered_){
        // Ricezione dal client del nickame con cui il giocatore 
        // vuole registrarsi
        recv_mess(client_socket, buffer, NULL);
        bool nickname_exists = false;
        // Accesso alla lista in mutua esclusione poichè verrà manipolata
        pthread_mutex_lock(&players_list_mutex);
        struct player * ptr_player = players_list;
        // Controllo nella lista se esiste già un giocatore con quel nickname
        while (ptr_player != NULL){
            if (strcmp(buffer,ptr_player->client_nickname) == 0){
                nickname_exists = true;
                break;
            }
            ptr_player = ptr_player->next;
        }
        if (nickname_exists){
            // Se il nickname è gia esistente viene inviata la stringa Error al
            // client 
            strcpy(buffer, "Error");
            buffer[strlen(buffer)] = '\0';
            send_mess(client_socket, buffer);
        } else {
            // Inizializzazione della struttura dati del giocatore
            player_count++;
            strcpy(new_player->client_nickname,buffer);
            new_player->in_game = true;
            for(int i = 0; i < NUM_TOPIC; i++){
                new_player->client_score[i] = -1;
                new_player->client_finish[i] = false;
            }
            // Inserimento nella lista
            new_player->next = players_list; 
            players_list = new_player;
            // Se il nickname non è gia esistente viene inviata la stringa Ok al
            // client
            strcpy(buffer, "Ok");
            buffer[strlen(buffer)] = '\0';
            send_mess(client_socket, buffer);

            registered_ = true;
        }
        // Rilascio della mutua esclusione
        pthread_mutex_unlock(&players_list_mutex);
    }
}

// Funzione per la costruzione e l'invio al client di una stringa già formattata 
// contenente la lista dei topic
void send_topics(int client_socket, char *buffer){
    FILE *fd;
    // Buffer di appoggio per la lettura da file
    // Ipotizzo che la stringa contente titolo e domande di un topic sia 
    // al massimo di 500 caratteri
    char buf[500];
    char count_text[15];
    fd = fopen("questions.txt", "r");
    if (fd == NULL){
        perror("Errore nella lettura dei file.\n");
        exit(EXIT_FAILURE);
    }
    strcpy(buffer, "Quiz disponibili\n");
    strcat(buffer,"+++++++++++++++++++++++++++++\n");

    // Leggendo dal file delle domande riga per riga e splittando la riga sulla 
    // base di un carattere speciale (;) si ottengo i titoli di topic 
    // disponibili
    // Il titolo del topic è il primo elemento della riga
    char *row = fgets(buf, sizeof(buf), fd);
    int count = 1;
    while (row != NULL) {
            char * token = strtok(row, ";");
            sprintf(count_text, "%d", count);
            strcat(buffer,count_text);
            strcat(buffer, " - ");
            strcat(buffer, token);
            strcat(buffer, "\n");
            count++;
            row = fgets(buf, sizeof(buf), fd);
    }
    strcat(buffer,"+++++++++++++++++++++++++++++\nOppure usa i comandi show score o endquiz\nLa tua scelta: ");

    buffer[strlen(buffer)] = '\0';
    // Invio al client della lista dei topic
    send_mess(client_socket, buffer);
}

// Funzione per la costruzione e l'invio al client di una stringa già formattata 
// contenente la classifica
void send_score(int client_socket, char *buffer){
    strcpy(buffer, "Classifica:\n");
    char to_text[15];
    // Sfrutta i dati nella lista dei giocatori per creare la classifica
    // Per ottenere una classifica ordinata si cicla topic per topic 
    // cercando giocatori via via a punteggi decrescenti partendo da NUM_QNA
    // che essendo il numero di domande è anche il massimo punteggio ottenibile
    for(int i = 0; i < NUM_TOPIC; i++) {
        strcat(buffer, "Punteggio tema ");
        sprintf(to_text, "%d", i+1);
        strcat(buffer, to_text);
        strcat(buffer, "\n");

        for(int j = NUM_QNA; j>= 0; j--){
            struct player * ptr_player = players_list;
            while (ptr_player != NULL){
            if (ptr_player->client_score[i] == j && ptr_player->in_game){
                printf("- %s %d\n", ptr_player->client_nickname, j);
                strcat(buffer,"- ");
                strcat(buffer, ptr_player->client_nickname);
                strcat(buffer, " ");
                sprintf(to_text, "%d", j);
                strcat(buffer, to_text);
                strcat(buffer, "\n");
            }
            ptr_player = ptr_player->next;
            }
        }
        strcat(buffer, "\n");
    }
    buffer[strlen(buffer)] = '\0';
    // Invio al client della classifica
    send_mess(client_socket, buffer);
}

// Funzione per la disconnessione di un client
void disconnect(int client_socket,struct player *current_player){
    // Chiusura del socket
    close(client_socket);
    // Aggiornamento variabili di utilità
    if(current_player){
        current_player->in_game = false;
        player_count--;
    }
    // Stampa della dashboard perchè è cambiata
    print_dashboard(); 
    // Terminazione del thread
    pthread_exit(NULL);
    // Il giocatore rimane nella lista perchè il suo nickname deve continuare
    // ad essere inutilizzabile
}

// Funzione che gestisce il gioco di un client a un topic
void play_trivia(int client_socket, char *buffer, struct player *current_player){
    FILE *fd;
    // Buffer di appoggio per la lettura da file
    // Ipotizzo che la stringa contente titolo e domande di un topic sia 
    // al massimo di 500 caratteri
    char buf[500];
    // Array di stringhe di appoggio per l'inserimento delle domande e risposte 
    // del topic a cui il client sta giocando
    char ** questions = NULL;
    char ** answers = NULL;
    // Conversione a intero del numero di topic inviato dall'utente
    // L'intero è passato come text poiche questo messaggio del client potrebbe
    // essere anche show score o endquiz
    int topic_playing = atoi(buffer);

    // Controllo per far giocare un giocatore ad un topic una volta sola
    // Il punteggio a tale topic sarà -1 solo da inizializzazione
    if(current_player->client_score[topic_playing - 1] != -1){
        // Se il giocatore ha già partecipato al topic viene inviata la 
        // stringa Error al client 
        strcpy(buffer, "Error");
        buffer[strlen(buffer)] = '\0';
        send_mess(client_socket, buffer);
        // Ritorno all' attesa di un nuovo comando
        return;
    }  

    // Se il giocatore non ha già partecipato al topic viene inviata la 
    // stringa Ok al client 
    strcpy(buffer, "Ok");
    buffer[strlen(buffer)] = '\0';
    send_mess(client_socket, buffer);

    // Il giocatore inizia a giocare al topic, quindi il suo punteggio per quel
    // topic viene portato a 0
    current_player->client_score[topic_playing - 1]++;
    // Stampa della dashboard perchè è cambiata, da questo momento il giocatore  
    // appare nella classifica del topic a cui sta giocando
    print_dashboard();

    // Allocazione delle variabili di appoggio
    questions = (char**)malloc(NUM_QNA *sizeof(char*));
    answers = (char**)malloc(NUM_QNA *sizeof(char*));

    // Apertura del file delle domande e letture di righe a vuoto fino a quella 
    // del topic a cui il giocatore sta giocando
    fd = fopen("questions.txt", "r");
    if (fd == NULL){
        perror("Errore nella lettura dei file.\n");
        exit(EXIT_FAILURE);
    }
    for(int i = 0; i < topic_playing; i++)
        fgets(buf, sizeof(buf), fd);
    buf[strlen(buf)] = '\0';

    // Split della riga sulla base del carattere speciale (;) 
    // La prima lettura è a vuoto, contiene il titolo
    char * token_q = strtok(buf, ";");
    strcpy(buffer, "\nQuiz - ");
    strcat(buffer, token_q);
    strcat(buffer,"\n+++++++++++++++++++++++++++++");
    buffer[strlen(buffer)] = '\0';
    // Invio al client del titolo del topic
    send_mess(client_socket, buffer);

    // Le seguenti letture contengono le domande, che vengono salvate 
    for(int i = 0; i < NUM_QNA; i++){
        token_q = strtok(NULL, ";");
        questions[i] = (char*)malloc((strlen(token_q)+1)*sizeof(char));
        strcpy(questions[i], token_q); 
    }
    fclose(fd);

    // Apertura del file delle risposte e letture di righe a vuoto fino a quella 
    // del topic a cui il giocatore sta giocando
    fd = fopen("answers.txt", "r");
    if (fd == NULL){
        perror("Errore nella lettura dei file.\n");
        exit(EXIT_FAILURE);
    }
    for(int i = 0; i < topic_playing; i++)
        fgets(buf, sizeof(buf), fd);
    buf[strlen(buf)] = '\0';
    // La prima lettura è a vuoto, contiene il titolo
    char * token_a = strtok(buf, ";");
    // Le seguenti letture contengono le risposte, che vengono salvate
    for(int i = 0; i < NUM_QNA; i++){
        token_a = strtok(NULL, ";");
        answers[i] = (char*)malloc((strlen(token_a)+1)*sizeof(char));
        strcpy(answers[i], token_a);
    }
    fclose(fd);
    // Loop di scambio domande e risposte tra client e server
    for(int i = 0; i < NUM_QNA; i++){
        // Invio al client della domanda
        strcpy(buffer, questions[i]);
        buffer[strlen(buffer)] = '\0';
        send_mess(client_socket, buffer);
        
        // Ricezione dal client della risposta del giocatore
        recv_mess(client_socket, buffer, current_player);

        if(strcmp(buffer, answers[i]) == 0){
            // Risposta errata 
            // Incremento punteggio nel topic a cui il giocatore sta giocando
            current_player->client_score[topic_playing - 1]++;
            // Stampa della dashboard perchè è cambiata
            print_dashboard();
            strcpy(buffer, "Risposta corretta!");
            buffer[strlen(buffer)] = '\0';
            // Se il giocatore ha risposto correttamente viene inviata la 
            // stringa Risposta corretta! al client 
            send_mess(client_socket, buffer);
        }
        else{ 
            // Risposta errata
            strcpy(buffer, "Risposta errata!");
            buffer[strlen(buffer)] = '\0';
            // Se il giocatore non ha risposto correttamente viene inviata la 
            // stringa Risposta errata! al client 
            send_mess(client_socket, buffer);
        }
        
    }
    // Il giocatore ha risposto a tutte le domande
    // Salvo che ha finito il topic
    current_player->client_finish[topic_playing - 1] = true;
    // Stampa della dashboard perchè è cambiata
    print_dashboard();

    for(int i = 0; i < NUM_QNA; i++){
        free(questions[i]);
        free(answers[i]);
    }
    free(questions);
    free(answers);
}

// Funzione per l'invio di un messaggio
void send_mess(int client_socket, char *buffer){
    int dim;
    // tipo certificato per l'invio della dimensione
    uint32_t dimNet;
   
    dim = strlen(buffer);
    // Converto la dimensione del messaggio in formato network
    dimNet = htonl(dim);
    // invio al client la dimensione del messaggio in formato network
    send(client_socket, &dimNet, sizeof(uint32_t), 0);
    // invio al client il messaggio
    send(client_socket, buffer, dim, 0);
}

// Funzione per la ricezione di un messaggio
void recv_mess(int client_socket, char *buffer, struct player *current_player){
    int ret, dim;
    // Tipo certificato per l'invio della dimensione
    uint32_t dimNet;
   
    // Ricevo dal client la lunghezza del messaggio in formato network
    ret = recv(client_socket, &dimNet, sizeof(uint32_t), 0);
    if(ret < sizeof(uint32_t)){
        disconnect(client_socket, current_player);
    }
    // Converto la dimensione del messaggio in formato host
    dim = ntohl(dimNet);
    // Ricevo dal client il messaggio
    ret = recv(client_socket, buffer, dim, 0);
    if (ret < dim){
        disconnect(client_socket, current_player);
    }
    // Inserisco carattere di fine stringa nel buffer
    buffer[dim] = '\0';
}