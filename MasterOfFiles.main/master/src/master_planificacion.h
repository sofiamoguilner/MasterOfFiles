#ifndef MASTER_PLANIFICACION_H_
#define MASTER_PLANIFICACION_H_

#include "master_estructuras.h"
extern t_list* listaWorkersConectados ;
extern pthread_mutex_t mutexWorkersConectados;
extern sem_t semWorkersDisponibles;

extern t_list* listaQueries;              // lista de t_query*
extern pthread_mutex_t mutexListaQueries;


void manejar_desconexion_qc(int socketQuery);
void manejar_desconexion_worker(int socketWorker);


void inicializar_planificacion(char* ruta_cfg); //creo la cola de readys
void agregar_query_ready_fifo(t_query* query); //Agrega una query a READY siguiendo FIFO
void* planificador_fifo(void* arg);
void* aging_ready(void* arg);
void registrar_worker_libre(t_worker* worker); //Registrar worker libre
void verificar_desalojo(void); // función que compara READY vs EXEC y pide desalojos
void solicitar_desalojo_al_worker(t_worker* w);

extern void enviar_fin_query(t_query* q, char* motivo);
t_worker* buscar_worker_por_socket(int socket);

#endif 
