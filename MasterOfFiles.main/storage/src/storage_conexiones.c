#include <storage_conexiones.h>

/*
// Mutex globales para bitmap y bloques fisicos
pthread_mutex_t mutex_bitmap;
pthread_mutex_t mutex_bloques;

// Miutex para listas
pthread_mutex_t mutex_listas_archivos;
pthread_mutex_t mutex_listas_bloques_fisicos;
pthread_mutex_t mutex_workers;
*/

int iniciar_servidor_con_worker(){
    int socket_servidor;

	struct addrinfo hints, *servinfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	getaddrinfo(NULL, configuracion.puerto_escucha, &hints, &servinfo);

	// Creamos el socket de escucha del servidor con fd_escucha o socket_servidor
	socket_servidor = socket(servinfo->ai_family,
							servinfo->ai_socktype,
							servinfo->ai_protocol);

	int err = 0;
    err = setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));
    // Asociamos el socket a un puerto
    err = bind(socket_servidor, servinfo->ai_addr, servinfo->ai_addrlen);
    // Escuchamos las conexiones entrantes
    err = listen(socket_servidor, SOMAXCONN);

    if(err != 0)
		log_error(logger, "Error con el puerto %s: %d", configuracion.puerto_escucha, err);

	freeaddrinfo(servinfo);
	log_trace(storage_log_debug, "Listo para escuchar a mi cliente");

	return socket_servidor;
}

int esperando_worker(int socket_servidor){
    int socket_cliente = accept(socket_servidor, NULL, NULL);
    return socket_cliente;
}

int index_elemento_idHilo(unsigned long id_hilo){
    int index;
    for ( index = 0; index < list_size(workers); index++) {
        t_hilo_worker* x = list_get(workers,index);;
        if ((unsigned long)x->HILO == id_hilo)
        
        
        return index;
        
    }
    
    return -1; // no se halla el elemento en la lista
}

t_hilo_worker* list_buscar_x_id_hilo(unsigned long id_hilo ){
        
    t_hilo_worker* _worker = malloc(sizeof(t_hilo_worker));

    _worker = list_get( workers , index_elemento_idHilo(id_hilo) );

    return _worker;
}

int get_idWorker(){
    unsigned long id_hilo = (unsigned long)pthread_self(); // id_hilo que se esta ejecutando

    t_hilo_worker* _worker = malloc(sizeof(t_hilo_worker));

    _worker = list_buscar_x_id_hilo(id_hilo);
    int _id_worker= -1;
    _id_worker =  (*_worker).ID_WORKER;
    free(_worker);
    return _id_worker; // -1 ---> si hubo Error / id_worker --> exitoso
}




void* atender_worker(void* arg) {
    int fd = (int)(intptr_t)arg;
    int worker_id = -1 ; // lo sacamos del handshake

    log_info(storage_logger, "Hilo de Storage listo para atender a un Worker");

   while (1) {
        int op = recibir_operacion(fd);

        if (op <= 0) {
           
            if (worker_id != -1) {
                 // LOG OBLIGATORIO
                pthread_mutex_lock(&mutex_workers);
                int cantidad_actual = list_size(workers);
                log_info(storage_logger, 
                         "## Se desconecta el Worker %d - Cantidad de Workers: %d",
                         worker_id, cantidad_actual - 1);
                
                
                // Remover de la lista de workers
                pthread_mutex_lock(&mutex_workers);
                for (int i = 0; i < list_size(workers); i++) {
                    t_hilo_worker* w = list_get(workers, i);
                    if (w->ID_WORKER == worker_id) {
                        list_remove(workers, i);
                        free(w);
                        break;
                    }
                }
                pthread_mutex_unlock(&mutex_workers);
            }
            shutdown(fd, SHUT_RDWR);
            close(fd);
            return NULL;
        }


        switch (op) {

            case OP_PEDIR_TAM_BLOQUE: {
               // storage_handshake(fd);
                //  ID del worker
                recv(fd, &worker_id, sizeof(worker_id), MSG_WAITALL);
                
                // crea y agrega estructura a lista
                t_hilo_worker* nuevo_worker = malloc(sizeof(t_hilo_worker));
                nuevo_worker->ID_WORKER = worker_id;
                nuevo_worker->FD_HILO = fd;
                nuevo_worker->HILO = pthread_self();
                
                pthread_mutex_lock(&mutex_workers);
                list_add(workers, nuevo_worker);
                int cantidad_actual = list_size(workers);
                pthread_mutex_unlock(&mutex_workers);

                // ✅ LOG Conexión
                log_info(storage_logger, 
                         "## Se conecta el Worker %d - Cantidad de Workers: %d",
                         worker_id, cantidad_actual);
                
                // rta con BLOCK_SIZE
                int op_resp = OP_RESP_TAM_BLOQUE;
                send(fd, &op_resp, sizeof(op_resp), 0);
                send(fd, &superbloque.BLOCK_SIZE, sizeof(int), 0);
                break;
            }

            case OP_CREATE:
                storage_create(fd);
                break;

            case OP_TRUNCATE:
                storage_truncate(fd);
                break;

            case OP_TAG:
                storage_tag(fd);
                break;

            case OP_DELETE:
                storage_delete(fd);
                break;

            case OP_COMMIT:
                storage_commit(fd);
                break;

            case OP_FLUSH:
                storage_flush(fd);
                break;

            case OP_ST_READ_BLOCK:
                storage_read_block(fd);
                break;

            case OP_ST_WRITE_BLOCK:
                storage_write_block(fd);
                break;

            default:
                log_warning(storage_logger,
                    "Operacion desconocida recibida del Worker: %d", op);
                break;
        }
    }

    return NULL;
}
