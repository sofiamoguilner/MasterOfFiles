#include "master_planificacion.h"
#include "master_estructuras.h"
#include <commons/collections/list.h>
#include <commons/log.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <unistd.h>
#include <utils/protocolo.h>
#include <utils/hello.h>

t_list* colaReadyFIFO; // Cola FIFO de queries
//t_list* listaWorkersLibres; // Lista de workers libres
t_list* listaWorkersConectados = NULL;  // Lista de workers conectados para buscar los q estan ocupados por alguna query

// Mutex para proteger estructuras
pthread_mutex_t mutexColaReady;
//pthread_mutex_t mutexWorkersLibres;
pthread_mutex_t mutexWorkersConectados;

t_estado *estadoReady = NULL;
t_estado *estadoExecute = NULL;
t_estado *estadoExit = NULL;

// Semáforos para sincronización
sem_t semQueriesPendientes;
sem_t semWorkersDisponibles;

int tiempo_aging_ms = 0;
char* algoritmo_planificacion = NULL; // "FIFO" o "PRIORIDADES" 

bool comparar_prioridad(void* a_v, void* b_v) { //a tiene prioridad mas chica entonces más urgente (true, va antes en la lista)
    t_query* a = (t_query*) a_v;
    t_query* b = (t_query*) b_v;
    if (a->prioridad == b->prioridad)
        return a->qid < b->qid; // Desempate por orden de llegada
    return a->prioridad < b->prioridad;  
}

t_worker* buscar_worker_por_socket(int socket) {
    t_worker* resultadoW = NULL; //almacena el resultado de la busqueda
    pthread_mutex_lock(&mutexWorkersConectados); //bloqueo para que no se agreguen workers mientras buscamos
    for (int i = 0; i < list_size(listaWorkersConectados); ++i) { //recorro todos los workers conectados
        t_worker* w = list_get(listaWorkersConectados, i); 
        if (w->socket == socket) { resultadoW = w; break; } //si el worker tiene el socket que busco sigo
    }
    pthread_mutex_unlock(&mutexWorkersConectados);
    return resultadoW;
}

t_worker* buscar_worker_por_id(int worker_id) {
    t_worker* resultadoW = NULL;
    pthread_mutex_lock(&mutexWorkersConectados);
    for (int i = 0; i < list_size(listaWorkersConectados); ++i) {
        t_worker* w = list_get(listaWorkersConectados, i);
        if (w->id == worker_id) {
            resultadoW = w;
            break;
        }
    }
    pthread_mutex_unlock(&mutexWorkersConectados);
    return resultadoW;
}

// tener un worker libre, sin sacarlo de la lista
t_worker* obtener_worker_libre(void) {
    pthread_mutex_lock(&mutexWorkersConectados);
    t_worker* worker_libre = NULL;
    

    for (int i = 0; i < list_size(listaWorkersConectados); i++) {
        t_worker* w = list_get(listaWorkersConectados, i);
        if (w->libre) {
            worker_libre = w;
            w->libre = false; //ocupado
            break;
        }
    }
    pthread_mutex_unlock(&mutexWorkersConectados);
    
    return worker_libre;
}


int contar_workers_libres(void) {
    int count = 0;
    pthread_mutex_lock(&mutexWorkersConectados);
    for (int i = 0; i < list_size(listaWorkersConectados); i++) {
        t_worker* w = list_get(listaWorkersConectados, i);
        if (w->libre) count++;
    }
    pthread_mutex_unlock(&mutexWorkersConectados);
    return count;
}

void inicializar_planificacion(char* ruta_cfg) {
    colaReadyFIFO = list_create();
    //listaWorkersLibres = list_create();
    listaWorkersConectados = list_create();

    pthread_mutex_init(&mutexColaReady, NULL);
    //pthread_mutex_init(&mutexWorkersLibres, NULL);
    pthread_mutex_init(&mutexWorkersConectados, NULL);

    sem_init(&semQueriesPendientes, 0, 0); //inicializo los semaforos en 0 para establecer las queries pendientes y workers libres
    sem_init(&semWorkersDisponibles, 0, 0); //fifo se bloquea hasta que haya al menos 1 query y 1 worker libres

    // Leer config
    t_config* cfg = config_create((char*)ruta_cfg);
    if(cfg) {
        algoritmo_planificacion = strdup(config_get_string_value(cfg, "ALGORITMO_PLANIFICACION"));
        tiempo_aging_ms = config_get_int_value(cfg, "TIEMPO_AGING");
        config_destroy(cfg);
    } else {
        algoritmo_planificacion = strdup("FIFO");
        tiempo_aging_ms = 0;
        }
   
  
       

    pthread_t hiloPlanificador;
    pthread_create(&hiloPlanificador, NULL, planificador_fifo, NULL);
    pthread_detach(hiloPlanificador);

    if(strcmp(algoritmo_planificacion, "PRIORIDADES") == 0 && tiempo_aging_ms > 0){
    pthread_t hiloAging;
    pthread_create(&hiloAging, NULL, aging_ready, NULL);
    pthread_detach(hiloAging);
    }
    log_info(logger, "Planificador inicializado. ALGORITMO=%s TIEMPO_AGING=%d",
             algoritmo_planificacion, tiempo_aging_ms);
}

void agregar_query_ready_fifo(t_query* query) {
    pthread_mutex_lock(&mutexColaReady); //bloquea la cola ready
    list_add(colaReadyFIFO, query); //agrega la query a la lista
     if (!strcmp(algoritmo_planificacion, "PRIORIDADES")){
        list_sort(colaReadyFIFO, comparar_prioridad); //ordena la lista para que 0 sea la de mayor prioridad
     }
    pthread_mutex_unlock(&mutexColaReady); //desbloquea la lista de ready

    log_info(logger, "## Se agrega la Query <%d> (<%d>) a la cola READY", query->qid, query->prioridad);

    sem_post(&semQueriesPendientes); //Aumenta el contador del semaforo, osea que hay una query pendiente, osea el planificador esta trabajando
    if (strcmp(algoritmo_planificacion, "PRIORIDADES") == 0) {
        verificar_desalojo(); //al agregar una query verifico si necesito desalojar alguna
    }
}


void registrar_worker_libre(t_worker* worker) {
    int cantidadWorkers;
    pthread_mutex_lock(&mutexWorkersConectados);

    worker->libre = true;
    worker->ejecutando = NULL;
    //list_remove_element(listaWorkersConectados, worker); cuando un worker termina su laburo
    cantidadWorkers = list_size(listaWorkersConectados);
    pthread_mutex_unlock(&mutexWorkersConectados);
    
    /*pthread_mutex_lock(&mutexWorkersLibres); 
    list_add(listaWorkersLibres, worker); //agregar el socket del worker a la lista
    pthread_mutex_unlock(&mutexWorkersLibres);*/

    

    //log_info(logger, "## Worker <%d> disponible para ejecución", worker->id); //chequear si es log obligatorio
    log_info(logger, "## Se conecta el Worker <%d> - Cantidad total de Workers: <%d>",
             worker->id, cantidadWorkers);

    sem_post(&semWorkersDisponibles); //le dice al planificador que hay un worker libre para usar
}




void* planificador_fifo(void* arg) {
    log_info(logger, "PLANIF: hilo FIFO arrancó");
    while (1) {
        sem_wait(&semQueriesPendientes);   //Espera queries
        sem_wait(&semWorkersDisponibles);  //Espera workers libres

        pthread_mutex_lock(&mutexColaReady);
        if (list_is_empty(colaReadyFIFO)) {
            // En caso raro: desbloqueamos y continuamos
            pthread_mutex_unlock(&mutexColaReady);
            sem_post(&semQueriesPendientes);
            sem_post(&semWorkersDisponibles);
            continue;
        }
        t_query* query = list_remove(colaReadyFIFO, 0); //saca la primer query de la cola
        pthread_mutex_unlock(&mutexColaReady);

        // Obtener Worker libre
        t_worker* worker = obtener_worker_libre();

        if (worker == NULL) {
            // No hay workers libres (condición rara), reencolar
            pthread_mutex_lock(&mutexColaReady);
            list_add_in_index(colaReadyFIFO, 0, query);
            pthread_mutex_unlock(&mutexColaReady);
            
            sem_post(&semQueriesPendientes);
            sem_post(&semWorkersDisponibles);
            continue;
        }
        
        query->estadoActual = EXEC;
        query->workerAsignado = worker;
        worker->ejecutando = query;
        //worker->libre = false;
        log_info(logger, "## Se envía la Query <%d> (<%d>) al Worker <%d>",
                 query->qid, query->prioridad, worker->id) ;
       
        t_paquete* p = crear_paquete();
        if (!p) {
            log_error(logger, "No se pudo crear paquete para enviar query %d ", query->qid);
            
            worker->libre = true;
            worker->ejecutando = NULL;

            pthread_mutex_lock(&mutexColaReady);
            list_add_in_index(colaReadyFIFO, 0, query);
            pthread_mutex_unlock(&mutexColaReady);

            sem_post(&semQueriesPendientes);
            sem_post(&semWorkersDisponibles);
            continue;
        }

        if (query->pc == 0) {
            p->codigo_operacion = OP_ENVIAR_PATH;
        } else {
            p->codigo_operacion = OP_REANUDAR_QUERY;
        }

        agregar_a_paquete(p, &query->qid, sizeof(query->qid));
        agregar_a_paquete(p, &query->pc,  sizeof(query->pc));    // 0 si es nueva
        agregar_a_paquete(p, (void*)query->path, strlen(query->path) + 1);

        if (enviar_paquete(p, worker->socket) < 0) {
            log_error(logger, "Error enviando query %d a worker %d", query->qid, worker->id);
            // manejo defensivo: reencolar query y volver a poner worker en libres
            eliminar_paquete(p);

            pthread_mutex_lock(&mutexColaReady);
            list_add_in_index(colaReadyFIFO, 0, query);
            pthread_mutex_unlock(&mutexColaReady);

            sem_post(&semQueriesPendientes);
            sem_post(&semWorkersDisponibles);
            continue;
        }
        eliminar_paquete(p);

        log_info(logger, "EXEC: Query %d enviada a Worker %d (PC=%d)",
                 query->qid, worker->id, query->pc);

    }
    return NULL;
}

void* aging_ready(void* arg) {
    while (1) {
      
        if (tiempo_aging_ms == 0) {
            usleep(100000);
            continue;
        }

        usleep(tiempo_aging_ms * 1000); // convertir a microsegundos

        pthread_mutex_lock(&mutexColaReady);

        for (int i = 0; i < list_size(colaReadyFIFO); i++) {
            t_query* q = list_get(colaReadyFIFO, i);

            // Disminuir prioridad (recuerda: prioridad menor = más importante)
            if (q->prioridad > 0) {
                int prioridad_anterior = q->prioridad;
                q->prioridad--;
                log_info(logger, "## <%d> Cambio de prioridad: <%d> - <%d>", q->qid, prioridad_anterior, q->prioridad);
            }
        }

        // Reordenar la cola por prioridad
        //list_sort(colaReadyFIFO, comparar_prioridad);

        //pthread_mutex_unlock(&mutexColaReady);
        if (strcmp(algoritmo_planificacion, "PRIORIDADES") == 0) {
            list_sort(colaReadyFIFO, comparar_prioridad);
        }

        pthread_mutex_unlock(&mutexColaReady);
        
        verificar_desalojo();
    }
    return NULL;
}

t_query* buscar_peor_query_en_ejecucion(t_worker** out_worker) {
    t_query* peor = NULL;
    t_worker* w_peor = NULL;

    pthread_mutex_lock(&mutexWorkersConectados);
    for (int i = 0; i < list_size(listaWorkersConectados); i++) {
        t_worker* w = list_get(listaWorkersConectados, i);
        if (!w->libre && w->ejecutando != NULL) {
            if (peor == NULL || w->ejecutando->prioridad > peor->prioridad) {
                peor = w->ejecutando;
                w_peor = w;
            }
        }
    }
    pthread_mutex_unlock(&mutexWorkersConectados);

    if (out_worker) *out_worker = w_peor;
    return peor;
}

// verificar si la mejor de READY debe desalojar a la peor en ejecución
void verificar_desalojo(void) {
    if (strcmp(algoritmo_planificacion, "PRIORIDADES") != 0) {
        return;
    }
    pthread_mutex_lock(&mutexColaReady);
    if (list_is_empty(colaReadyFIFO)) {
        pthread_mutex_unlock(&mutexColaReady);
        return;
    }
    t_query* mejor_ready = list_get(colaReadyFIFO, 0); //query con mejor prioridad
    pthread_mutex_unlock(&mutexColaReady);

    t_worker* worker_peor = NULL;
    t_query* peor_exec = buscar_peor_query_en_ejecucion(&worker_peor);

    if (peor_exec && mejor_ready->prioridad < peor_exec->prioridad && worker_peor != NULL) {
        // pedimos desalojo al worker_peor
        log_info(logger, "## Pedir desalojo: Query <%d> (p=%d) será desalojada por Query <%d> (p=%d)",
                 peor_exec->qid, peor_exec->prioridad, mejor_ready->qid, mejor_ready->prioridad);

        t_paquete* p = crear_paquete();
        if (!p) {
            log_error(logger, "No se pudo crear paquete de desalojo para worker %d", worker_peor->id);
            return;
        }
        p->codigo_operacion = OP_DESALOJAR_QUERY;
        agregar_a_paquete(p, &peor_exec->qid, sizeof(peor_exec->qid));

        if (enviar_paquete(p, worker_peor->socket) < 0) {
            log_error(logger, "Fallo enviando desalojo al worker %d", worker_peor->id);
        } else {
            log_info(logger, "Se pidió desalojo al Worker %d para Query %d", worker_peor->id, peor_exec->qid);
        }
        eliminar_paquete(p);
    }
}

void solicitar_desalojo_al_worker(t_worker* w)
{
    int op = OP_DESALOJAR_QUERY;

    log_info(logger,
             "## Enviando OP_DESALOJAR_QUERY al Worker %d (socket %d)",
             w->id, w->socket);

    send(w->socket, &op, sizeof(op), 0);

    // No envío nada más: el Worker ya sabe qué Query está ejecutando
}

void manejar_desconexion_worker(int socketWorker) {
    t_worker* worker = buscar_worker_por_socket(socketWorker);
    
    if (!worker) {
        log_warning(logger, "Worker con socket %d no encontrado", socketWorker);
        return;
    }

    pthread_mutex_lock(&mutexWorkersConectados);
    
    // Si estaba ejecutando una Query
    if (worker->ejecutando != NULL) {
        t_query* q = worker->ejecutando;
        
        log_info(logger, 
                 "## Se desconecta el Worker %d - Se finaliza la Query %d - Cantidad total de Workers: %d",
                 worker->id, q->qid, list_size(listaWorkersConectados) - 1);
        
        // Mover query a EXIT
        q->estadoActual = EXIT;
        q->workerAsignado = NULL;
        
        // Notificar al Query Control (opcional)
        // send(q->socketQuery, &error_msg, ...);
        enviar_fin_query(q, "Cancelada por desconexion del Worker");
        
    } else {
        log_info(logger, 
                 "## Se desconecta el Worker %d - Cantidad total de Workers: %d",
                 worker->id, list_size(listaWorkersConectados) - 1);
    }
    
    // Remover de la lista
    list_remove_element(listaWorkersConectados, worker);
    
    pthread_mutex_unlock(&mutexWorkersConectados);
    
    // Liberar memoria
    free(worker);
}


