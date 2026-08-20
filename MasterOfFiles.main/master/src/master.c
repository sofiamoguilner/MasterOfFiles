#include <signal.h>
#include <commons/config.h>
#include <commons/log.h>
#include <pthread.h>
#include <utils/hello.h>  // iniciar_servidor, esperar_cliente, recibir_mensaje, crear_conexion, enviar_mensaje, liberar_conexion
#include <utils/protocolo.h>

#include "master_query.h"
#include "master_estructuras.h"
#include "master_planificacion.h"

// logger global usado por utils
extern t_log *logger;

t_list *queries_activas; // nuevas listas de queries activadas
t_list *workers;

int fd_servidor;

void destruir_query(void *elem)
{
    if (!elem) return;

    t_query *q = (t_query *)elem;
    // Strings
    free(q->instrucciones);
    free(q->path);
    free(q->motivoFinalizacion);

    // Mutex
    if (q->mutex) {
        pthread_mutex_destroy(q->mutex);
        free(q->mutex);
    }

    free(q);
}

void destruir_worker(void *elem) {
    if (!elem) return;

    t_worker *w = (t_worker *)elem;

    // Cerrar socket si está abierto
    if (w->socket > 0) {
        shutdown(w->socket, SHUT_RDWR);
        close(w->socket);
        w->socket = -1;
    }

    // NO destruir w->ejecutando
    // la query se destruye en el módulo central (scheduler)

    free(w);
}

static int correr = 1;
void sigint(int signum) {
    log_info(logger, "Cerrando MASTER por SIGINT...");

    correr = 0;

    // --- Cerrar socket del servidor ---

    if (fd_servidor > 0) {
        shutdown(fd_servidor, SHUT_RDWR);
        close(fd_servidor);
        fd_servidor = -1;
    }

    if (workers) {
        list_destroy_and_destroy_elements(workers, destruir_worker);
        workers = NULL;
    }

    if (queries_activas) {
        list_destroy_and_destroy_elements(queries_activas, destruir_query);
        queries_activas = NULL;
    }

    log_info(logger, "MASTER cerrado correctamente.");

    // --- Destruir logs ---
    if (logger)
        log_destroy(logger);

    exit(EXIT_SUCCESS);
}

t_query *buscar_query_por_id(uint32_t qid)
{
    for (int i = 0; i < list_size(queries_activas); i++)
    {
        t_query *q = list_get(queries_activas, i);
        if (q->qid == qid)
            return q;
    }
    return NULL;
}


int socketQueryABuscar;
bool mismo_socket(void* elem) {
    t_query* q = (t_query*) elem;
    return q->socketQuery == socketQueryABuscar;
}

//elimina query de lista activas
/*void eliminar_query_por_socket(int socketQuery) {
    socketQueryABuscar = socketQuery;

    t_query* eliminada = list_remove_by_condition(queries_activas, mismo_socket);
    if (eliminada != NULL) {
        log_info(logger, "## Se eliminó la Query %d (%s) por desconexión del cliente",
                 eliminada->qid, eliminada->path);
        pthread_mutex_destroy(eliminada->mutex);
        free(eliminada->mutex);
        free(eliminada->path);
        free(eliminada);
    }
}*/
/*
t_query *buscar_query_por_socket(int socket)
{
    for (int i = 0; i < list_size(queries_activas); i++)
    {
        t_query *q = list_get(queries_activas, i);
        if (q->socketQuery == socket)
            return q;
    }
    return NULL;
}

void eliminar_query_por_socket(int socket)
{
    t_query *q = buscar_query_por_socket(socket);
    if (q == NULL)
        return;

    list_remove(queries_activas, q);

    // liberar memoria
    pthread_mutex_destroy(q->mutex);
    free(q->mutex);
    free(q->path);
    free(q);
}
*/


void *atender_cliente(void *arg)
{
    int sock = (intptr_t) arg;
    bool esQuery = false;
    bool esWorker = false;

    int op = recibir_operacion(sock);
    if (op <= 0) {
        log_info(logger, "Cliente se desconecta antes (sock=%d)", sock);
        shutdown(sock, SHUT_RDWR);
        close(sock);
        return NULL;
    }

    // ------------- HANDSHAKE INICIAL -------------
    switch (op) {
        case OP_HELLO_QUERY: {
            esQuery = true;

            int op2 = recibir_operacion(sock);
            if (op2 != OP_ENVIAR_PATH) {
                log_error(logger, "Error: Query no envió OP_ENVIAR_PATH (sock=%d)", sock);
                shutdown(sock, SHUT_RDWR);
                close(sock);
                return NULL;
            }

            t_list *campos = recibir_paquete(sock);

            // [0] = path (char*), [1] = prioridad (int*) según tu protocolo
            char *path_recibido = list_get(campos, 0);
            int prioridad = 0;
            memcpy(&prioridad, list_get(campos, 1), sizeof(int));

            // Me quedo con una copia propia del path
            char *path = strdup(path_recibido);

            // Libero la lista del paquete
            list_destroy_and_destroy_elements(campos, free);

            t_query *q = crear_query(path, prioridad, sock);
            free(path); // crear_query ya se guarda su copia interna

            list_add(queries_activas, q);

            int nivel = list_size(listaWorkersConectados);

            log_info(
                logger,
                "## Se conecta un Query Control para ejecutar la Query <%s> con prioridad <%d> "
                "- Id asignado: <%d> Nivel multiprocesamiento <%d>",
                q->path, q->prioridad, q->qid, nivel
            );

            // Encolo la query en READY
            agregar_query_ready_fifo(q);
            break;
        }

        case OP_HELLO_WORKER: {
            esWorker = true;

            uint32_t op2 = recibir_operacion(sock);
            if (op2 != OP_WORKER_ID) {
                log_error(logger, "Error: Worker no envió OP_WORKER_ID (sock=%d)", sock);
                shutdown(sock, SHUT_RDWR);
                close(sock);
                return NULL;
            }

            uint32_t id_worker;
            recv(sock, &id_worker, sizeof(id_worker), MSG_WAITALL);

            t_worker *worker = malloc(sizeof(t_worker));
            worker->socket = sock;
            worker->id = id_worker;
            worker->libre = true;
            worker->ejecutando = NULL;

            pthread_mutex_lock(&mutexWorkersConectados);
            list_add(listaWorkersConectados, worker);
            int cantidadWorkers = list_size(listaWorkersConectados);
            pthread_mutex_unlock(&mutexWorkersConectados);

            log_info(logger,
                     "## Se conecta el Worker %d - Cantidad total de Workers: %d",
                     id_worker, cantidadWorkers);

            // Aviso que hay un worker disponible
            sem_post(&semWorkersDisponibles);

            break;
        }

        default:
            log_warning(logger, "Operación HELLO desconocida (%d) en sock=%d", op, sock);
            shutdown(sock, SHUT_RDWR);
            close(sock);
            return NULL;
    }

    // ------------- LOOP PRINCIPAL -------------
    while (1) {
        op = recibir_operacion(sock);

        if (op <= 0) {
            log_info(logger, "Cliente desconectado (sock=%d)", sock);

            if (esQuery) {
                manejar_desconexion_qc(sock);
            } else if (esWorker) {
                manejar_desconexion_worker(sock);
            }

            shutdown(sock, SHUT_RDWR);
            close(sock);
            break;
        }

        // ---------------- MENSAJES DESDE WORKER ----------------
        if (esWorker) {
            switch (op) {

                // 1) Desalojo de Query (ya lo tenías)
                case OP_QUERY_DESALOJADA: {
                    int qid, pc;
                    recv(sock, &qid, sizeof(qid), MSG_WAITALL);
                    recv(sock, &pc, sizeof(pc), MSG_WAITALL);

                    t_worker *worker = buscar_worker_por_socket(sock);
                    if (!worker || !worker->ejecutando || worker->ejecutando->qid != qid) {
                        log_error(logger,
                                  "## Error: Worker %d reportó desalojo de Query %d pero no coincide con su ejecutando.",
                                  worker ? worker->id : -1, qid);
                        break;
                    }

                    t_query *q = worker->ejecutando;
                    q->pc = pc;
                    q->estadoActual = READY;
                    q->workerAsignado = NULL;
                    worker->ejecutando = NULL;

                    registrar_worker_libre(worker);
                    agregar_query_ready_fifo(q);

                    log_info(logger,
                             "## Query %d desalojada. Guardado PC=%d. Vuelve a READY con prioridad %d. Worker %d queda libre.",
                             q->qid, q->pc, q->prioridad, worker->id);
                    break;
                }

                // 2) Respuesta de READ: reenviar lectura al Query Control
                case OP_RESP_READ_OK: {
                    // 2.1) Identificar worker
                    t_worker* worker = buscar_worker_por_socket(sock);
                    if (!worker) {
                        log_error(logger, "Llego OP_RESP_READ_OK de un socket desconocido");
                        // Consumimos paquete por las dudas
                        t_list* basura = recibir_paquete(sock);
                        if (basura) list_destroy_and_destroy_elements(basura, free);
                        break;
                    }

                    // 2.2) Ver que tenga una query ejecutando
                    if (!worker->ejecutando) {
                        log_error(logger,
                                  "Worker %d envio OP_RESP_READ_OK pero no tiene query asociada",
                                  worker->id);
                        t_list* basura = recibir_paquete(sock);
                        if (basura) list_destroy_and_destroy_elements(basura, free);
                        break;
                    }

                    t_query* q = worker->ejecutando;

                    // 2.3) Recibir paquete del Worker: [filetag][tamanio][buffer]
                    t_list* campos = recibir_paquete(sock);
                    if (!campos || list_size(campos) < 3) {
                        log_error(logger,
                                  "OP_RESP_READ_OK mal formado desde Worker %d",
                                  worker->id);
                        if (campos) list_destroy_and_destroy_elements(campos, free);
                        break;
                    }

                    char* filetag = list_get(campos, 0);

                    int tamanio = 0;
                    memcpy(&tamanio, list_get(campos, 1), sizeof(int));

                    void* buffer = list_get(campos, 2);

                    // 2.4) Armar mensaje de texto para el Query Control
                    int max_mostrar = tamanio;  // si querés, podés limitarlo
                    int tamanio_mensaje = strlen("Archivo ") +
                                          strlen(filetag) +
                                          strlen(", contenido: ") +
                                          max_mostrar + 1;

                    char* mensaje = malloc(tamanio_mensaje);
                    if (!mensaje) {
                        log_error(logger, "No se pudo alocar memoria para mensaje de READ");
                        list_destroy_and_destroy_elements(campos, free);
                        break;
                    }

                    snprintf(mensaje, tamanio_mensaje,
                             "Archivo %s, contenido: %.*s",
                             filetag, max_mostrar, (char*)buffer);

                    // 2.5) Enviar mensaje al Query Control
                    t_paquete* p = crear_paquete();
                    p->codigo_operacion = OP_MENSAJE;  // lo que espera el Query Control

                    agregar_a_paquete(p, mensaje, strlen(mensaje) + 1);
                    enviar_paquete(p, q->socketQuery);
                    eliminar_paquete(p);

                    // 2.6) Log obligatorio del Master
                    log_info(logger,
                             "## Se envía un mensaje de lectura de la Query %d en el Worker %d al Query Control",
                             q->qid, worker->id);

                    // 2.7) Liberar memoria
                    free(mensaje);
                    list_destroy_and_destroy_elements(campos, free);

                    break;
                }

                // 3) Fin normal de la Query (END en el Worker)
                case OP_FIN_QUERY: {
                    t_worker* worker = buscar_worker_por_socket(sock);
                    if (!worker) {
                        log_error(logger, "Llego OP_FIN_QUERY de un socket desconocido");
                        t_list* basura = recibir_paquete(sock);
                        if (basura) list_destroy_and_destroy_elements(basura, free);
                        break;
                    }

                    if (!worker->ejecutando) {
                        log_error(logger,
                                  "Worker %d envio OP_FIN_QUERY pero no tiene query asociada",
                                  worker->id);
                        t_list* basura = recibir_paquete(sock);
                        if (basura) list_destroy_and_destroy_elements(basura, free);
                        break;
                    }

                    t_query* q = worker->ejecutando;

                    // El Worker te manda [qid] en un paquete
                    t_list* campos = recibir_paquete(sock);
                    if (!campos || list_size(campos) < 1) {
                        log_error(logger,
                                  "OP_FIN_QUERY mal formado desde Worker %d",
                                  worker->id);
                        if (campos) list_destroy_and_destroy_elements(campos, free);
                        break;
                    }

                    int qid = 0;
                    memcpy(&qid, list_get(campos, 0), sizeof(int));
                    list_destroy_and_destroy_elements(campos, free);

                    if (qid != q->qid) {
                        log_error(logger,
                                  "Worker %d envio OP_FIN_QUERY con qid=%d pero esperaba qid=%d",
                                  worker->id, qid, q->qid);
                        break;
                    }

                    // Log que pide el enunciado
                    log_info(logger,
                             "## Se terminó la Query %d en el Worker %d",
                             q->qid, worker->id);

                    // Marcar worker como libre
                    worker->ejecutando = NULL;
                    registrar_worker_libre(worker);

                    // Avisar al Query Control y cerrar todo lo de la Query
                    // enviar_fin_query se encarga de mandar OP_FIN_QUERY al QC,
                    // logear motivo y sacarla de las listas
                    enviar_fin_query(q, "Finalizada OK");

                    break;
                }

                default: {
                    log_warning(logger,
                                "Operacion desconocida %d recibida de Worker en sock=%d",
                                op, sock);
                    // Podrías consumir algo del socket si tu protocolo lo requiere
                    break;
                }
            } // fin switch(op) de Worker
        } // fin if (esWorker)

        // Si en el futuro tenés mensajes desde el Query Control (esQuery),
        // los manejás acá con otro if (esQuery) { switch (op) { ... } }
    }

    return NULL;
}


/*void enviar_fin_query(t_query* q, char* motivo) {
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
                log_info(logger, "Se envió OP_FIN_QUERY a Query %d (socket %d) motivo: %s",
                         q->qid, q->socketQuery, msg);
            }
            eliminar_paquete(p);
        } else {
            log_error(logger, "No se pudo crear paquete OP_FIN_QUERY para qid=%d", q->qid);
        }

        // Cerrar socket con el QC: ya notificamos
        close(q->socketQuery);
        q->socketQuery = -1;
    } else {
        log_info(logger, "Socket del Query %d no válido, no se envió OP_FIN_QUERY", q->qid);
    }

    // Remover de las listas
    eliminar_query_por_socket(q->socketQuery);
}*/

int main(int argc, char* argv[]) {
    
    logger = log_create("master.log", "MASTER", true, LOG_LEVEL_INFO);
    if (!logger) {
        fprintf(stderr, "Error: No se pudo crear el logger\n");
        return EXIT_FAILURE;
    }
    char* ruta_cfg = argv[1];
    queries_activas = list_create();
    workers = list_create();

    t_config* cfg = config_create((char*)ruta_cfg);
    if (!cfg) {
        fprintf(stderr, "No pude abrir %s\n", ruta_cfg);
        return 1;
    }

    inicializar_planificacion(ruta_cfg);

    log_info(logger, "Iniciando servidor Master...");
    
    char* puerto = config_get_string_value(cfg, "PUERTO_ESCUCHA"); // O del config
    fd_servidor = iniciar_servidor(puerto);
    if (fd_servidor < 0) {
        log_error(logger, "Error al crear servidor");
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    log_info(logger, "Servidor escuchando en puerto %s", puerto);
    signal(SIGINT, sigint);

    while (correr) {
        int socket_cliente = esperar_cliente(fd_servidor);
        if (socket_cliente < 0) {
            if (!correr) break;
            continue;
        }

        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente, (void*)(intptr_t)socket_cliente);
        pthread_detach(hilo);
    }

    if (fd_servidor > 0) {
        shutdown(fd_servidor, SHUT_RDWR);
        close(fd_servidor);
        fd_servidor = -1;
    }

    if (workers) {
        list_destroy_and_destroy_elements(workers, destruir_worker);
        workers = NULL;
    }

    if (queries_activas) {
        list_destroy_and_destroy_elements(queries_activas, destruir_query);
        queries_activas = NULL;
    }

    log_info(logger, "MASTER cerrado correctamente.");

    // --- Destruir logs ---
    if (logger)
        log_destroy(logger);
    
    return EXIT_SUCCESS;
}