#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <ctype.h>

extern int total_guesses;
extern int total_wins;
extern int total_losses;
extern char ** words;
char **dict = NULL;
int ansIndex = 0;
int numWords = 0;

pthread_mutex_t lock;
int on = 1;

void sigusr1(int sig);
void* handle_client(void* arg);
char* guessWord(const char* guess, const char* hidden_word, int* guesses_left);
int wordle_server( int argc, char ** argv );

void sigusr1(int sig) {
    on = 0; 
    fprintf(stdout, "MAIN: SIGUSR1 rcvd; Wordle server shutting down...\n");
    for (int i = 0; *(dict+i); i++) {
        free(*(dict+i));
    }
    free(dict);
    return;
}

void* handle_client(void* arg) {
    int cSocket = *(int*)arg;
    free(arg);

    pthread_t thread_id = pthread_self();

    pthread_mutex_lock(&lock);
    char* hidden_word = calloc(6,sizeof(char));
    strcpy(hidden_word, *(dict + (int)(rand()%(numWords))));
    for (int j = 0; j < 5; j++){
        *(hidden_word+j) = toupper(*(hidden_word+j));
    }
    pthread_mutex_unlock(&lock);

    char **tmp = realloc(words, ((ansIndex + 1) * sizeof(char*)));
    if (tmp == NULL) {
        perror("realloc failed");
        exit(EXIT_FAILURE);
    }
    words = tmp;

    words = realloc(words, ((ansIndex + 1) * sizeof(char*)));
    *(words+ansIndex) = calloc(6,sizeof(char));
    strcpy(*(words+ansIndex),hidden_word);
    ansIndex++;

    int guesses = 6;
    while (guesses > 0) {
        char *guess = calloc(6,sizeof(char));
        fprintf(stdout, "THREAD %lu: waiting for guess\n", (unsigned long)thread_id);
        int bytes_received = recv(cSocket, guess, 5, 0);
        if (bytes_received <= 0) {
            fprintf(stdout, "THREAD %lu: client gave up; closing TCP connection...\n", (unsigned long)thread_id);
            fprintf(stdout, "THREAD %lu: game over; word was %s!\n", (unsigned long)thread_id, hidden_word);
            pthread_mutex_lock(&lock);
            total_losses++;
            pthread_mutex_unlock(&lock);
            free(guess);
            free(hidden_word);
            close(cSocket);
            pthread_exit(NULL);
        }

        int valid = 0;

        for (int j = 0; j < 5; j++){
            *(guess+j) = tolower(*(guess+j));
        }

        for (char** word = dict; *word; word++) {
            if (!strcmp(guess, *word)) {
                valid = 1;
                break;
            }
        }
        fprintf(stdout, "THREAD %lu: rcvd guess: %s\n", (unsigned long)thread_id, guess);
        
        //only if valid guess
        if(valid){
            total_guesses++;
            printf("THREAD %lu: sending reply: ",(unsigned long)thread_id);
        }
        else if(!valid){
            fprintf(stdout, "THREAD %lu: invalid guess; sending reply: ????? (%d guesses left)\n", (unsigned long)thread_id, guesses);
        }

        char* wordle = calloc(9, sizeof(char));

    if (!wordle) {
        fprintf(stderr,("ERROR: calloc() failed"));
        return NULL;
    }

    int valid2 = 0;
    for (char** word = dict; *word; word++) {
        if (!strcmp(guess, *word)) {
            valid2 = 1;
            break;
        }
    }

    if (!valid2 || wordle == NULL) {
        *(wordle+0) = 'N';
        *(short*)(wordle + 1) = htons(guesses);
        memcpy(wordle + 3, "?????", 5);
    }else{ 

        *(wordle+0) = 'Y';
        (guesses)--;

        char *result = calloc(6, sizeof(char));   
        strcpy(result, "-----");
        int *ret = calloc(5,sizeof(int)); 

        for (int i = 0; i < 5; i++) {
            if (*(guess+i) == *(hidden_word+i)) {
                *(result+i) = toupper(*(guess+i));
                *(ret+i) = 1;
            } else if (*(guess+i) != '\0' && *(guess+i) == *(hidden_word+i) && isalpha(*(guess+i))) {
                *(result+i) = toupper(*(guess+i));
                *(ret+i) = 1;
            }  else if (!*(ret+i)) {
                for (int j = 0; j < 5; j++) {
                    if (!*(ret+j) && *(guess+i) == *(hidden_word+j)) {
                        *(result+i) = tolower(*(guess+i));
                        *(ret+j) = 1;
                        break;
                    }
                }
            }
        }

        *(short*)(wordle + 1) = htons(guesses);
        memcpy(wordle + 3, result, 5);
        if(guesses == 1){
                fprintf(stdout, "%s (%d guess left)\n", result, guesses);
        }else{
            fprintf(stdout, "%s (%d guesses left)\n", result, guesses);
        }
        free(ret);
        free(result);
    }
        send(cSocket, wordle, 8, 0);
        free(wordle);
        if (strcmp(guess, hidden_word) == 0) {
            fprintf(stdout, "THREAD %lu: game over; word was %s!\n", (unsigned long)thread_id, hidden_word);
            pthread_mutex_lock(&lock);
            total_wins++;
            pthread_mutex_unlock(&lock);
            free(hidden_word);
            free(guess);
            break;
        } else if (guesses == 0) {
            fprintf(stdout, "THREAD %lu: out of guesses; word was %s!\n", (unsigned long)thread_id, hidden_word);
            pthread_mutex_lock(&lock);
            total_losses++;
            pthread_mutex_unlock(&lock);
            free(hidden_word);
            free(guess);
            break;
        }
        
        free(guess);
    }
    close(cSocket);
    pthread_exit(NULL);
}


int wordle_server(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "ERROR: Invalid argument(s)\n");
        fprintf(stderr, "USAGE: hw3.out <listener-port> <seed> <dictionary-filename> <num-words>\n");
        return EXIT_FAILURE;
    }

    int seed = atoi(*(argv+2));
    numWords = atoi(*(argv+4));

    srand(seed);

    FILE *file = fopen(*(argv+3), "r");
    if (!file) {
        fprintf(stderr,("ERROR: failure opening file"));
        exit(EXIT_FAILURE);
    }

    dict = calloc(numWords + 1, sizeof(char*));
    if (!dict) {
        fprintf(stderr,("ERROR: calloc() failed"));
        exit(EXIT_FAILURE);
    }


    char *word = calloc(6,sizeof(char));
    int i = 0;
    while (fscanf(file, "%5s", word) == 1 && i < numWords) {
        *(dict+i) = calloc(strlen(word) + 1, sizeof(char));
        if (!*(dict+i)) {
            fprintf(stderr,("ERROR: calloc() failed"));
            exit(EXIT_FAILURE);
        }

        for (int j = 0; *(word+j); j++) {
            *(word+j) = tolower(*(word+j));
        }
        strcpy(*(dict+i), word);
        i++;
    }

    free(word);

    *(dict+i) = NULL; 
    fclose(file);
    fprintf(stdout, "MAIN: opened %s (%d words)\n", *(argv+3), i);
    if (pthread_mutex_init(&lock, NULL) != 0) {
        fprintf(stderr,("ERROR: pthread_mutex_init() failed"));
        return EXIT_FAILURE;
    }

    struct sigaction s;
    s.sa_handler = sigusr1;
    sigemptyset(&s.sa_mask);
    s.sa_flags = 0;
    sigaction(SIGUSR1, &s, NULL);

    int ssocket = socket(AF_INET, SOCK_STREAM, 0);
    if (ssocket < 0) {
        fprintf(stderr,("ERROR: socket() failed"));
        return EXIT_FAILURE;
    }

    struct sockaddr_in sAddress;
    memset(&sAddress, 0, sizeof(sAddress));
    sAddress.sin_family = AF_INET;
    sAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    sAddress.sin_port = htons(atoi(*(argv+1)));

    if (bind(ssocket, (struct sockaddr*)&sAddress, sizeof(sAddress)) < 0) {
        fprintf(stderr,("ERROR: bind() failed"));
        close(ssocket);
        return EXIT_FAILURE;
    }

    if (listen(ssocket, 5) < 0) {
        fprintf(stderr,("ERROR: listen() failed"));
        close(ssocket);
        return EXIT_FAILURE;
    }

    fprintf(stdout, "MAIN: seeded pseudo-random number generator with %d\n", atoi(*(argv+2)));
    fprintf(stdout, "MAIN: Wordle server listening on port {%d}\n",atoi(*(argv+1)));

        while (on) {
        struct sockaddr_in cAddress;
        socklen_t client_len = sizeof(cAddress);
        int* csocket = malloc(sizeof(int));
        if (!csocket) {
            fprintf(stderr,("ERROR: malloc() failed"));
            continue;
        }

        *csocket = accept(ssocket, (struct sockaddr*)&cAddress, &client_len);
        if (*csocket < 0) {
            free(csocket);
            continue;
        }

        printf("MAIN: rcvd incoming connection request\n");
        
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, csocket);
        pthread_detach(thread);
    }
    close(ssocket);
    pthread_mutex_destroy(&lock);

    

    return EXIT_SUCCESS;
}