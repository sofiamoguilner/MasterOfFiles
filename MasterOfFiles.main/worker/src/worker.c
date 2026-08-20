#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <commons/collections/list.h>
#include <commons/config.h>
#include <commons/log.h>
#include <utils/hello.h>
#include <utils/protocolo.h>
#include <worker.h>
#include <instrucciones.h>
#include <memoria_interna.h>
#include <stdbool.h>  


int pc_actual = 0;
bool desalojado = false;
size_t FRAME_COUNT = 0;



//Esto me permite que cualquier worker pueda reaunudar un querie sin que el master deba reenviar el path
typedef struct {
    int qid;
    char* path;
} QueryPath;

t_list* lista_query_paths = NULL;

void add_path_for_qid(int qid, char* path) { //guarda el path en memoria del worker para poder reaunad cuando llegue una querie
    QueryPath* qp = malloc(sizeof(QueryPath)); //reserva memoria
    qp->qid = qid; //guarda el id
    qp->path = strdup(path);
    list_add(lista_query_paths, qp); //añade el puntero a la lista
}

char* get_path_for_qid(int qid) {
    for (int i = 0; i < list_size(lista_query_paths); i++) {
        QueryPath* qp = list_get(lista_query_paths, i);
        if (qp->qid == qid) return qp->path;
    }
    return NULL;
}

void remove_path_for_qid(int qid) { //libera memoria
    for (int i = 0; i < list_size(lista_query_paths); i++) {
        QueryPath* qp = list_get(lista_query_paths, i);
        if (qp->qid == qid) {
            list_remove(lista_query_paths, i);
            free(qp->path);
            free(qp);
            return;
        }
    }
}


int conexion_con_storage(char* ip_storage, char* puerto_storage){
    int socket_storage = crear_conexion(ip_storage, puerto_storage);
    if (socket_storage < 0) {
        log_error(logger, "No se pudo establecer conexión con Storage (%s:%s)",
                  ip_storage, puerto_storage);
        return -1;
    }

    log_info(logger, "Conexión establecida con Storage (socket %d)", socket_storage);
    return socket_storage;
}

int solicitud_block_size(int socket_storage, int id_worker) {
    int op = OP_PEDIR_TAM_BLOQUE;
    send(socket_storage, &op, sizeof(op), 0);
    send(socket_storage, &id_worker, sizeof(id_worker), 0);

    int respuesta = -1;
    recv(socket_storage, &respuesta, sizeof(respuesta), MSG_WAITALL);
    if (respuesta != OP_RESP_TAM_BLOQUE) {
        log_error(logger, "Storage no respondió correctamente a OP_PEDIR_TAM_BLOQUE");
        return -1;
    }

    // tamaño de bloque
    int block_size = -1;
    if (recv(socket_storage, &block_size, sizeof(block_size), MSG_WAITALL) != sizeof(block_size)) {
        log_error(logger, "Error al recibir el tamaño de bloque desde Storage");
        return -1;
    }

    return block_size;
}

int conexion_con_master(char* ip_master, char* puerto_master) {
    int socket_master = crear_conexion(ip_master, puerto_master);
    if (socket_master < 0) {
        log_error(logger, "No pude conectar a Master (%s:%s)", ip_master, puerto_master);
        return -1;
    }

    int op_worker = OP_HELLO_WORKER;
    send(socket_master, &op_worker, sizeof(op_worker), 0);
    
    log_info(logger, "Conexión con Master(socket %d)", socket_master);
    return socket_master;
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <worker.config> <ID_Worker>\n", argv[0]);
        return 1;
    }

    const char* ruta_cfg = argv[1];
    int id_worker = atoi(argv[2]);

    t_config* cfg = config_create((char*)ruta_cfg);
    if (!cfg) {
        fprintf(stderr, "No pude abrir %s\n", ruta_cfg);
        return 1;
    }

    char* ip_storage     = config_get_string_value(cfg, "IP_STORAGE");
    char* puerto_storage = config_get_string_value(cfg, "PUERTO_STORAGE");
    char* ip_master      = config_get_string_value(cfg, "IP_MASTER");
    char* puerto_master  = config_get_string_value(cfg, "PUERTO_MASTER");
    char* log_level_str  = config_get_string_value(cfg, "LOG_LEVEL");

    logger = log_create("worker.log", "WORKER", true, log_level_from_string(log_level_str));
    log_info(logger, "Iniciando Worker ID=%d", id_worker);

    lista_query_paths = list_create();

    // ====== Conexión con STORAGE ======
    int conexion_storage = conexion_con_storage(ip_storage, puerto_storage);
    if (conexion_storage < 0) return 1;

    int block_size = solicitud_block_size(conexion_storage, id_worker);
    if (block_size < 0) {
        log_error(logger, "Handshake con Storage falló. Abortando Worker.");
        close(conexion_storage);
        return 1;
    }

    log_info(logger, "Conectado a Storage. BLOCK_SIZE=%d (manteniendo conexión abierta)", block_size);

    // ==== MEMORIA INTERNA ====
    int   tam_memoria    = config_get_int_value(cfg, "TAM_MEMORIA");
    int   retardo_mem    = config_get_int_value(cfg, "RETARDO_MEMORIA");
    char* alg_reemplazo  = config_get_string_value(cfg, "ALGORITMO_REEMPLAZO");

    //el tamaño debe ser multiplo del tam de bloque (desp se puede usar en el truncate)
    if(tam_memoria % block_size != 0) { 
        log_error(logger, "TAM_MEMORIA (%d) debe ser múltiplo de BLOCK_SIZE (%d).", tam_memoria, block_size);
        close(conexion_storage);
        config_destroy(cfg);
        log_destroy(logger);
        return 1;
    }

    Ram info_memoria = {
        .tam_memoria = (size_t)tam_memoria,
        .page_size   = (size_t)block_size,
        .frames = (size_t)tam_memoria / (size_t)block_size, // opcional, lo puede calcular mem_init
        .retardo     = (uint32_t)retardo_mem,
        .algORITMO   = (strcmp(alg_reemplazo, "CLOCK-M") == 0) ? REEMPLAZO_CLOCKM : REEMPLAZO_LRU
    };

    if (inicializar_memoria(&info_memoria) != 0) {
        log_error(logger, "No se pudo inicializar la Memoria Interna.");
        close(conexion_storage);
        config_destroy(cfg);
        log_destroy(logger);
        return 1;
    }
    log_info(logger, "Memoria Interna inicializada: TAM=%d, PAGE_SIZE=%d, FRAMES=%zu, ALG=%s",
             tam_memoria, block_size, info_memoria.frames, alg_reemplazo);


    // ====== Conexión con MASTER ======
    int conexion_master = conexion_con_master(ip_master, puerto_master);
    if (conexion_master < 0) {
        close(conexion_storage);
        return 1;
    }

    int op_ID_Worker = OP_WORKER_ID;
    send(conexion_master, &op_ID_Worker, sizeof(op_ID_Worker), 0);
    send(conexion_master, &id_worker, sizeof(id_worker), 0);
    log_info(logger, "Handshake con Master completado. Worker listo para recibir Queries.");

    //PATH QUERY DADO POR EL MASTER 
    char* path_queries = config_get_string_value(cfg, "PATH_SCRIPTS"); 

    // ===== BUCLE DE ESCUCHA DE MASTER =====

  while (1) {
    int codigo_operacion = recibir_operacion(conexion_master);

    switch (codigo_operacion) {

        case OP_ENVIAR_PATH: {
            t_list* campos = recibir_paquete(conexion_master);

            int id_query   = *(int*) list_get(campos, 0);
            int pc_inicial = *(int*) list_get(campos, 1);
            char* nombre   = (char*) list_get(campos, 2);

            char* path_completo = generar_path_query(path_queries, nombre);

            // Guardamos el path para ese qid (para reanudar si corresponde)
            add_path_for_qid(id_query, path_completo);


            log_info(logger,
                     "## Query %d: Se recibe la Query. El path de operaciones es: %s",
                     id_query, path_completo);


            desalojado = false;
            query_cancelada = false;
            pc_actual = pc_inicial;

            int resultado = procesar_query(
                path_completo,
                id_query,
                pc_inicial,
                conexion_master,      //  se lo pasasamos al intérprete
                conexion_storage,     // y este también
                block_size,
                logger
            );

            if (resultado != 0) {
                log_error(logger,
                          "Falló procesar la Query %d", id_query);
            }

            free(path_completo);
            list_destroy_and_destroy_elements(campos, free);

            break;
        }

        case OP_QUERY_DESCONECTADA: {
            int id_query;
            recv(conexion_master, &id_query, sizeof(id_query), MSG_WAITALL);

            query_cancelada = true;

            log_warning(logger,
                        "Se recibió aviso de desconexión del Query Control (ID=%d)",
                        id_query);
            break;
        }

        case OP_DESALOJAR_QUERY: {
            int id_query;
            recv(conexion_master, &id_query, sizeof(id_query), MSG_WAITALL);

            log_warning(logger, "Master ordena desalojo de Query %d (PC actual = %d)", id_query, pc_actual);

            desalojado = true; 
            break;
        }

        case OP_REANUDAR_QUERY: {
            int qid_reanudar;
            int pc_reanudar;

            recv(conexion_master, &qid_reanudar, sizeof(int), MSG_WAITALL);
            recv(conexion_master, &pc_reanudar, sizeof(int), MSG_WAITALL);

            log_info(logger, "Master solicita reanudar Query %d desde PC=%d", qid_reanudar, pc_reanudar);

            // Buscar path guardado para ese qid
            char* path_para_q = get_path_for_qid(qid_reanudar);
            if (path_para_q == NULL) { log_error(logger, "No tengo el path de la Query %d para reanudar. Ignorando reanudación.", qid_reanudar);
                break;
            }
            // Seteamos el Program Counter global
            pc_actual = pc_reanudar;
            desalojado = false;
            query_cancelada = false;

            // Reanudar la ejecución desde el PC indicado
            int resultado_reanudo = procesar_query(
            path_para_q,
            qid_reanudar,
            pc_actual,
            conexion_master,
            conexion_storage,
            block_size,
            logger
            );

            if (resultado_reanudo != 0) {log_error(logger, "procesar_query (reanudación) retornó %d para qid %d", resultado_reanudo, qid_reanudar);
            }

            remove_path_for_qid(qid_reanudar);

            break;
        }

        case -1:
            log_warning(logger,
                        "El Master cerró la conexión");
            close(conexion_master);
            return 0;

        default:
            log_warning(logger,"Opcode inesperado recibido del Master: %d", codigo_operacion);
            break;

     }

  }


    //luego iria op cuando hay interrucciones
    
    
    close(conexion_master);
    destrozar_memoria();
    close(conexion_storage);
    config_destroy(cfg);
    log_destroy(logger);
}
  
 