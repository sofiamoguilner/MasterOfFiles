// Biblioteca para guardar las estructuras y variables globales del master
#ifndef MASTER_ESTRUCTURAS_H_
#define MASTER_ESTRUCTURAS_H_

// Bibliotecas estandar
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <netdb.h>

// Bibliotecas commons
#include <commons/log.h>
#include <commons/config.h>
#include <commons/collections/list.h>
#include <commons/collections/dictionary.h>

//Estructuras
enum nombre_estado
{   
    READY,
    EXEC,
    EXIT,
};
typedef enum nombre_estado t_nombre_estado;

struct estado
{
    t_nombre_estado nombreEstado;
    t_list *listaQueries;
    sem_t *semaforoEstado;
    pthread_mutex_t *mutexEstado;
};
typedef struct estado t_estado;

struct query
{
    uint32_t qid;
    char* instrucciones;
    uint32_t pc;
    char* path;
    int prioridad;
    t_nombre_estado estadoActual;
    uint32_t socketQuery;
    pthread_mutex_t *mutex;

    //prioridad con  desalojo
    struct worker* workerAsignado;  // pointer al worker que lo ejecuta

    bool canceladaPorDesconexionQC;   // Si el QC se desconectó
    bool finalizada;                  // Marca general de EXIT
    char* motivoFinalizacion;         // Texto para enviar al QC
};
typedef struct query t_query;

struct worker {
    int socket;       
    uint32_t id;       
    bool libre;    
    t_query* ejecutando; //qué query está ejecutando cada worker 
};
typedef struct worker t_worker;


struct semaforo_recurso
{
    int32_t instancias;
    t_estado *estadoRecurso;
};
typedef struct semaforo_recurso t_semaforo_recurso;

//Variales globales
extern sem_t dispatchPermitido;
extern pthread_mutex_t mutexSocketFilesystem;
extern sem_t semFRead;
extern sem_t semFWrite;
extern bool fRead;
extern bool fWrite;

// Estados
extern t_estado *estadoReady;
extern t_estado *estadoExecute; 
extern t_estado *estadoExit;

#endif