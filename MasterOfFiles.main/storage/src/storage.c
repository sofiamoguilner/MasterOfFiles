// Bibliotecas creadas
#include <storage.h>

int correr = 1;
void sigint(int signum) {
    log_info(storage_logger, "Cerrando STORAGE por SIGINT...");

    correr = 0;

    // --- Cerrar socket del servidor ---

    if (fd_storage > 0) {
        shutdown(fd_storage, SHUT_RDWR);
        close(fd_storage);
        fd_storage = -1;
    }

    // --- Liberar bitmap ---

    if (bitmap) {
        if (bitmap->direccion && bitmap->tamanio > 0) {
            msync(bitmap->direccion, bitmap->tamanio, MS_SYNC);
            munmap(bitmap->direccion, bitmap->tamanio);
            bitmap->direccion = NULL;
        }
        if (bitmap->bitarray) {
            bitarray_destroy(bitmap->bitarray);
            bitmap->bitarray = NULL;
        }
        free(bitmap);
        bitmap = NULL;
    }

    // --- Destruir listas globales ---
    if (lista_files) {
        list_destroy_and_destroy_elements(lista_files, destruir_file);
        lista_files = NULL;
    }

    if (lista_bloques_fisicos) {
        list_destroy_and_destroy_elements(lista_bloques_fisicos, destruir_block_fisico);
        lista_bloques_fisicos = NULL;
    }

    if (workers) {
        list_destroy_and_destroy_elements(workers, destruir_hilo_worker);
        workers = NULL;
    }

    log_info(storage_logger, "STORAGE cerrado correctamente.");

    // --- Destruir logs ---
    if (storage_log_debug)
        log_destroy(storage_log_debug);
    if (storage_logger)
        log_destroy(storage_logger);


    exit(EXIT_SUCCESS);
}

void iterator(char *value){
    log_info(storage_logger, "%s", value);
}

int main(int argc, char *argv[])
{

    pthread_mutex_init(&mutex_bitmap, NULL);
    pthread_mutex_init(&mutex_bloques, NULL);

    pthread_mutex_init(&mutex_listas_archivos, NULL);
    pthread_mutex_init(&mutex_listas_bloques_fisicos, NULL);
    pthread_mutex_init(&mutex_workers, NULL);

    // Inicializar storage logs y config

    inicializar_storage(argc, argv);

    signal(SIGINT, sigint);

    // Crear rutas necesarios si no existen y si existe no solo avisa que existen

    directorio_raiz = configuracion.punto_montaje;
    directorio_blocks_fisicos = string_from_format("%s/physical_blocks", directorio_raiz);
    directorio_files = string_from_format("%s/files", directorio_raiz);

    // Crear archivos necesarios
    ruta_superbloque = string_from_format("%s/superblock.config", directorio_raiz);       // ruta raiz
    ruta_hash_index = string_from_format("%s/blocks_hash_index.config", directorio_raiz); // ruta raiz
    ruta_bitmap = string_from_format("%s/bitmap.bin", directorio_raiz);                   // ruta bitmap

    montar_desde_cero();
    // leer config superbloque
    leer_config_superblock(ruta_superbloque);
    cant_block = (int)(superbloque.FS_SIZE / superbloque.BLOCK_SIZE);
    // int size=superbloque.BLOCK_SIZE;
    lista_bloques_fisicos = list_create();
    lista_files = list_create();

    if (configuracion.fresh_start == true)
    {
        // Solo limpiar disco / estructuras (fresh_start_true no crea initial_file)
        fresh_start_true();

        // 1) Crear bitmap vacío en disco (necesario antes de iniciar bloques fisicos)
        uint32_t cant_block = (uint32_t)(superbloque.FS_SIZE / superbloque.BLOCK_SIZE);
        if (crear_bitmap_vacio(ruta_bitmap, cant_block) != 0) {
            log_error(storage_log_debug, "No se pudo crear bitmap vacío en %s", ruta_bitmap);
            exit(EXIT_FAILURE);
        }

        // 2) Abrir / mapear el bitmap
        if (abrir_bitmap(ruta_bitmap, cant_block) != 0) {
            log_error(storage_log_debug, "No se pudo abrir el bitmap tras fresh start");
            exit(EXIT_FAILURE);
        }

        // 3) Crear los archivos físicos .dat (block0000..)
        iniciar_bloques_fisicos(cant_block, superbloque.BLOCK_SIZE);

        // 4) Crear initial_file y su tag de forma "canónica" usando funciones ya probadas
        crear_file("initial_file");

        char *temp_ruta_tag = string_from_format("%s/initial_file/BASE", directorio_files);
        crear_ruta(temp_ruta_tag);
        t_tag *_tag = crear_tag("BASE", temp_ruta_tag);
        if (!_tag) {
            log_error(storage_log_debug, "Error creando tag BASE para initial_file");
            free(temp_ruta_tag);
            exit(EXIT_FAILURE);
        }

        // 5) Linkear block0 y agregar block lógico en RAM de forma segura
        char* ruta_block_logicos = string_from_format("%s/logical_blocks", temp_ruta_tag);
        linkear_block_inicial(ruta_block_logicos);

        t_block_logico *un_block_logico = malloc(sizeof(t_block_logico));
        un_block_logico->nombre_block = strdup(BLOCK_LOGICO_INICIAL);
        un_block_logico->bloque_fisico_asociado = strdup(BLOCK_FISICO_INICIAL);
        list_add(_tag->lista_block_logicos, un_block_logico);

        // 6) Añadir el tag al file (crear_file ya agregó initial_file en lista_files)
        t_file *_file = list_get(lista_files, 0);
        if (!_file) {
            log_error(storage_log_debug, "lista_files no contiene initial_file después de crear_file");
            // cleanup parcial
            free(ruta_block_logicos);
            free(temp_ruta_tag);
            exit(EXIT_FAILURE);
        }
        list_add(_file->tags, _tag);

        // 7) Marcar block físico 0 en metadata y bitmap de forma segura (NO pasar literales)
        safe_string_array_push(&(_tag->metadata->blocks_fisicos_usados), "0"); // hace strdup internamente
        _tag->metadata->tamanio_file = superbloque.BLOCK_SIZE;
        _tag->metadata->estado = COMMITED;
        actualizar_metadata(temp_ruta_tag, _tag->metadata);

        // 8) incrementar contador del bloque físico 0 en memoria
        if (list_size(lista_bloques_fisicos) > 0) {
            t_block_fisico* bf0 = list_get(lista_bloques_fisicos, 0);
            bf0->blogico_asociados++;
        } else {
            log_warning(storage_log_debug, "Advertencia: lista_bloques_fisicos vacía al marcar block0.");
        }

        // 9) Marcar bit en el bitmap
        bitmap_marcar_bloque_ocupado(0);

        // 10) crear el t_config* hash_index
        hash_index_config = config_create(ruta_hash_index);

        free(temp_ruta_tag);
        free(ruta_block_logicos);

        log_trace(storage_logger,"## FS levantado - fresh start : true");
    }
    else {
        // Creo la lista con datos basicos
        iniciar_bloques_fisicos_existentes(cant_block, superbloque.BLOCK_SIZE);
        // crear hash_index antes de hacer el fresh para que relacionar los bl con los bf
        hash_index_config = config_create(ruta_hash_index);
        // Si no es fresh start: levantar blocks físicos, abrir bitmap, etc.
        fresh_start_false(); 
        // abrir bitmap
        abrir_bitmap(ruta_bitmap, (uint32_t)cant_block); 
    }

    // levantando estructuras
    
    if (configuracion.fresh_start == true) {
        bitmap_marcar_bloque_ocupado(0);
        for (size_t i = 1; i < cant_block; i++) {
            bitmap_marcar_bloque_libre(i);
        }
    }
    else {
       log_debug(storage_log_debug, "Sincronizando bitmap con bloques físicos");
    
        for (int i = 0; i < list_size(lista_bloques_fisicos); i++) {
            t_block_fisico* bf = list_get(lista_bloques_fisicos, i);
            
            if (bf->blogico_asociados > 0) {
                bitmap_marcar_bloque_ocupado(i);
            } else {
                bitmap_marcar_bloque_libre(i);
            }
        }
    
        log_info(storage_logger, "Bitmap sincronizado correctamente");

        log_trace(storage_logger,"## FS levantado - fresh start : false");
    }
   
    
    bitmap_mostrar_por_pantalla();


    // Crear servidor
    fd_storage = 0;
    fd_storage = iniciar_servidor_con_worker();
    ensure(fd_storage >= 0, "ERROR: No pude crear el servidor en PUERTO_ESCUCHA");

    log_info(storage_logger, "Storage listo. Esperando workers en puerto %s", configuracion.puerto_escucha);

    // Aceptar cliente

    // fd_cliente = esperar_cliente(fd_storage);
    // ensure(fd_cliente >= 0, "ERROR: Falló aceptar cliente");

    workers = list_create();
    
    while (correr) {
        int socket_cliente = esperando_worker(fd_storage); //,storage_logger,"Worker");
        ensure(socket_cliente >= 0, "ERROR: Falló aceptar worker");
        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_worker, (void *)(intptr_t)socket_cliente);
        pthread_detach(hilo);
    }

    pthread_mutex_destroy(&mutex_bitmap);
    pthread_mutex_destroy(&mutex_bloques);

    pthread_mutex_destroy(&mutex_listas_archivos);
    pthread_mutex_destroy(&mutex_listas_bloques_fisicos);
    pthread_mutex_destroy(&mutex_workers);

    if (fd_storage > 0) {
        shutdown(fd_storage, SHUT_RDWR);
        close(fd_storage);
        fd_storage = -1;
    }

    // --- Liberar bitmap ---

    //destruir_bitmap();
    
    if (bitmap) {
        if (bitmap->direccion && bitmap->tamanio > 0) {
            msync(bitmap->direccion, bitmap->tamanio, MS_SYNC);
            munmap(bitmap->direccion, bitmap->tamanio);
            bitmap->direccion = NULL;
        }
        if (bitmap->bitarray) {
            bitarray_destroy(bitmap->bitarray);
            bitmap->bitarray = NULL;
        }
        free(bitmap);
        bitmap = NULL;
    }

    // --- Destruir listas globales ---
    if (lista_files) {
        list_destroy_and_destroy_elements(lista_files, destruir_file);
        lista_files = NULL;
    }

    if (lista_bloques_fisicos) {
        list_destroy_and_destroy_elements(lista_bloques_fisicos, destruir_block_fisico);
        lista_bloques_fisicos = NULL;
    }

    if (workers) {
        list_destroy_and_destroy_elements(workers, destruir_hilo_worker);
        workers = NULL;
    }

    log_info(storage_logger, "STORAGE cerrado correctamente.");

    // --- Destruir logs ---
    if (storage_log_debug)
        log_destroy(storage_log_debug);
    if (storage_logger)
        log_destroy(storage_logger);

    return EXIT_SUCCESS;
}
