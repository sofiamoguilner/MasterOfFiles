#include "master_query.h"
#include "master_estructuras.h"
#include "master_planificacion.h"
#include <commons/collections/list.h>

extern pthread_mutex_t mutexListaQueries;
extern t_list* listaQueries;
uint32_t next_qid = 0;  // empieza en 0
pthread_mutex_t mutex_qid = PTHREAD_MUTEX_INITIALIZER;

uint32_t generar_qid() {
    pthread_mutex_lock(&mutex_qid);
    uint32_t qid = next_qid++;
    pthread_mutex_unlock(&mutex_qid);
    return qid;
}

t_query* crear_query(char* path, int prioridad, int sock) {
    t_query* q = malloc(sizeof(t_query));

    char* nombre_archivo = path_nombre_archivo(path);
    q->path = nombre_archivo;

    q->qid = generar_qid();
    q->prioridad = prioridad;
    q->pc = 0;
    q->estadoActual = READY;
    q->socketQuery = sock;
    q->workerAsignado = NULL;
    q->canceladaPorDesconexionQC = false;

    // Inicializar punteros a NULL
    q->instrucciones = NULL;
    q->motivoFinalizacion = NULL;

    q->mutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(q->mutex, NULL);

    return q;
}


const char* _saltear_prefijos(const char* p){
    while (p && p[0]=='.' && p[1]=='/') p += 2;
    return p;
}

char* path_nombre_archivo(const char* ruta){
    if(!ruta) return NULL;
    ruta = _saltear_prefijos(ruta);
    const char* base = strrchr(ruta, '/');
    base = base ? base + 1 : ruta;
    return strdup(base);
}

char* path_unir_directorio_archivo(const char* directorio, const char* nombre){
    if(!directorio || !nombre) return NULL;
    size_t nd = strlen(directorio);
    size_t nn = strlen(nombre);
    char* out = malloc(nd + (nd>0 && directorio[nd-1] != '/' ? 1 : 0) + nn + 1);
    if(!out) return NULL;

    memcpy(out, directorio, nd);
    size_t pos = nd;
    if(nd>0 && directorio[nd-1] != '/'){ out[pos++] = '/'; }
    memcpy(out + pos, nombre, nn);
    out[pos + nn] = '\0';
    return out;
}

t_query* buscar_query_por_socket(uint32_t socketQuery) {
    bool coincide_socket(void* elem) {
        t_query* q = (t_query*) elem;
        return q->socketQuery == socketQuery;
    }
    
    // Busco en READY
    if (estadoReady && estadoReady->listaQueries) {
        t_query* q = list_find(estadoReady->listaQueries, coincide_socket);
        if(q) return q;
    }

    // Busco en EXEC
    if (estadoExecute && estadoExecute->listaQueries) {
        t_query* q = list_find(estadoExecute->listaQueries, coincide_socket);
        if(q) return q;
    }

    // Busco en EXIT
    if (estadoExit && estadoExit->listaQueries) {
        t_query* q = list_find(estadoExit->listaQueries, coincide_socket);
        if(q) return q;
    }

    return NULL;   // puede ser NULL
}

/*t_query* buscar_query_por_socket(uint32_t socketQuery) {
    bool coincide_socket(void* elem) {
        t_query* q = (t_query*) elem;
        return q->socketQuery == socketQuery;
    }
    
    // Busco en READY
    t_query* q = list_find(estadoReady->listaQueries, coincide_socket);
    if(q) return q;

    // Busco en EXEC
    q = list_find(estadoExecute->listaQueries, coincide_socket);
    if(q) return q;

    // Busco en EXIT
    q = list_find(estadoExit->listaQueries, coincide_socket);
    return q;   // puede ser NULL
}*/

void eliminar_query_por_socket(uint32_t socketQuery) {
    bool coincide_socket(void* elem) {
        t_query* q = (t_query*) elem;
        return q->socketQuery == socketQuery;
    }

    if (estadoReady && estadoReady->listaQueries)
        list_remove_by_condition(estadoReady->listaQueries, coincide_socket);
    if (estadoExecute && estadoExecute->listaQueries)
        list_remove_by_condition(estadoExecute->listaQueries, coincide_socket);
    if (estadoExit && estadoExit->listaQueries)
        list_remove_by_condition(estadoExit->listaQueries, coincide_socket);


    /*list_remove_by_condition(estadoReady->listaQueries, coincide_socket);
    list_remove_by_condition(estadoExecute->listaQueries, coincide_socket);
    list_remove_by_condition(estadoExit->listaQueries, coincide_socket);*/
}

void enviar_fin_query(t_query* q, char* motivo) {
    if (!q) return;

    // Marcar estado a EXIT (protección básica)
    q->estadoActual = EXIT;

    if (q->socketQuery > 0) {
        t_paquete* p = crear_paquete();
        if (p) {
            p->codigo_operacion = OP_FIN_QUERY;
            const char* msg = motivo ? motivo : "Finalizada por Master";
            agregar_a_paquete(p, (void*)msg, strlen(msg) + 1);
            if (enviar_paquete(p, q->socketQuery) < 0) {
                log_error(logger, "No se pudo enviar OP_FIN_QUERY a socket %d (qid=%d)", q->socketQuery, q->qid);
            } else {
                log_info(logger, "Se envió OP_FIN_QUERY a Query %d (socket %d) motivo: %s", q->qid, q->socketQuery, msg);
            }
            eliminar_paquete(p);
        } else {
            log_error(logger, "No se pudo crear paquete OP_FIN_QUERY para qid=%d", q->qid);
        }

        // Cerrar socket con el QC: ya notificamos
        shutdown(q->socketQuery, SHUT_RDWR);
        close(q->socketQuery);
        q->socketQuery = -1;
    } else {
        log_info(logger, "Socket del Query %d no válido, no se envió OP_FIN_QUERY", q->qid);
    }

    // Remover de las listas (si estaba)
    eliminar_query_por_socket(q->socketQuery); // q->socketQuery ahora -1, pero eliminar por condición también se debería usar por otro campo si es necesario
    // Alternativa: remover por qid (si existe helper). Para asegurar remoción:
    bool coincide_qid(void* elem) {
        t_query* qq = (t_query*) elem;
        return qq->qid == q->qid;
    }
    if (estadoReady && estadoReady->listaQueries)
        list_remove_by_condition(estadoReady->listaQueries, coincide_qid);
    if (estadoExecute && estadoExecute->listaQueries)
        list_remove_by_condition(estadoExecute->listaQueries, coincide_qid);
    if (estadoExit && estadoExit->listaQueries)
        list_remove_by_condition(estadoExit->listaQueries, coincide_qid);

    log_info(logger, "Query %d movida a EXIT y removida de listas", q->qid);

    // liberar recursos asociados a la query si corresponde
    if (q->mutex) {
        pthread_mutex_destroy(q->mutex);
        free(q->mutex);
    }
    if (q->path) free(q->path);
    free(q);
}

void manejar_desconexion_qc(int socketQuery)
{
    log_info(logger,
             "## Se detecta desconexión del Query Control (socket=%d)",
             socketQuery);

    t_query* q = buscar_query_por_socket(socketQuery);

    if (!q) {
        log_warning(logger,
                    "## No hay Query asociada al socket %d",
                    socketQuery);
        return;
    }

    log_info(logger,
             "## La Query %d inicia cancelación por desconexión del QC",
             q->qid);

       //Si la Query está en READY
    if (q->estadoActual == READY) {

        log_info(logger,
                 "## La Query %d estaba en READY → pasa a EXIT",
                 q->qid);

        q->estadoActual = EXIT;
        /*eliminar_query_por_socket(socketQuery);
        return;*/
        bool coincide_qid(void* elem) {
            t_query* qq = (t_query*) elem;
            return qq->qid == q->qid;
        }
        if (estadoReady && estadoReady->listaQueries)
            list_remove_by_condition(estadoReady->listaQueries, coincide_qid);
        if (estadoExecute && estadoExecute->listaQueries)
            list_remove_by_condition(estadoExecute->listaQueries, coincide_qid);
        if (estadoExit && estadoExit->listaQueries)
            list_remove_by_condition(estadoExit->listaQueries, coincide_qid);

        // liberar recursos
        if (q->mutex) { pthread_mutex_destroy(q->mutex); free(q->mutex); }
        if (q->path) free(q->path);
        free(q);
        return;
    }


       //Si la Query está en EXEC

    if (q->estadoActual == EXEC) {
        t_worker* w = q->workerAsignado;

        if (!w) {
            log_error(logger,
                      "## Query %d en EXEC pero sin worker??",
                      q->qid);
            q->estadoActual = EXIT;
            /*eliminar_query_por_socket(socketQuery);
            return;*/
            bool coincide_qid(void* elem) {
                t_query* qq = (t_query*) elem;
                return qq->qid == q->qid;
            }
            if (estadoReady && estadoReady->listaQueries)
                list_remove_by_condition(estadoReady->listaQueries, coincide_qid);
            if (estadoExecute && estadoExecute->listaQueries)
                list_remove_by_condition(estadoExecute->listaQueries, coincide_qid);
            if (estadoExit && estadoExit->listaQueries)
                list_remove_by_condition(estadoExit->listaQueries, coincide_qid);
            if (q->mutex) { pthread_mutex_destroy(q->mutex); free(q->mutex); }
            if (q->path) free(q->path);
            free(q);
            return;
        }

        log_info(logger,
                 "## Query %d está en EXEC → solicitando desalojo al Worker %d",
                 q->qid, w->id);

        // Señalamos que su final será EXIT (no READY)
        //q->canceladaPorDesconexionQC = true;

        solicitar_desalojo_al_worker(w);
        return; 
    }

       //Si estaba en EXIT o BLOCK
    log_warning(logger,
                "## Query %d en estado %d → se fuerza EXIT por desconexión",
                q->qid, q->estadoActual);

    q->estadoActual = EXIT;
    //eliminar_query_por_socket(socketQuery);
    bool coincide_qid(void* elem) {
        t_query* qq = (t_query*) elem;
        return qq->qid == q->qid;
    }
    if (estadoReady && estadoReady->listaQueries)
        list_remove_by_condition(estadoReady->listaQueries, coincide_qid);
    if (estadoExecute && estadoExecute->listaQueries)
        list_remove_by_condition(estadoExecute->listaQueries, coincide_qid);
    if (estadoExit && estadoExit->listaQueries)
        list_remove_by_condition(estadoExit->listaQueries, coincide_qid);

    if (q->mutex) { pthread_mutex_destroy(q->mutex); free(q->mutex); }
    if (q->path) free(q->path);
    free(q);
}

    
