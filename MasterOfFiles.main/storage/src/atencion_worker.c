#include "atencion_worker.h"

#include <storage_gestion_estructuras.h>
//#include "sincronizacion.h"
#include "operaciones_storage.h"

/*
#include <utils/protocolo.h>
#include <utils/hello.h>
#include <commons/log.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
*/
/*
t_log* storage_logger;
t_superbloque superbloque;
storage_config configuracion;
*/

// HANDSHAKE //

void storage_handshake(int fd) {
    int id_worker = 0;
    recv(fd, &id_worker, sizeof(id_worker), MSG_WAITALL);

    log_info(storage_logger, "## Handshake con Worker %d", id_worker);

    usleep(configuracion.retardo_operacion * 1000);

    int op_resp = OP_RESP_TAM_BLOQUE;
    send(fd, &op_resp, sizeof(op_resp), 0);
    send(fd, &superbloque.BLOCK_SIZE, sizeof(int), 0);
}


// CREATE //

void storage_create(int fd) {
    usleep(configuracion.retardo_operacion * 1000);

    t_list* campos = recibir_paquete(fd);

    int qid = *(int*) list_get(campos, 0);
    char* file_tag = strdup(list_get(campos, 1));   // <--- COPIA SEGURA
    
    char *file = NULL, *tag = NULL;

    if (split_file_tag(file_tag, &file, &tag) != 0) {
        log_error(storage_logger, "CREATE mal formado");
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);

        free(file_tag);
        list_destroy_and_destroy_elements(campos, free);
        return;
    }

    int resultado = operacion_create_file(file, tag);

    if (resultado == 0) {
        log_info(storage_logger,
                 "## Query:%d - File Creado %s:%s", qid, file, tag);
        int ok = OP_RESP_OK;
        send(fd, &ok, sizeof(ok), 0);
    } else {
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
    }

    free(file_tag);  // <--- liberar la copia
    free(file);
    free(tag);
    list_destroy_and_destroy_elements(campos, free);
}

//TRUNCATE
void storage_truncate(int fd) {
    usleep(configuracion.retardo_operacion * 1000);

    t_list* campos = recibir_paquete(fd);
    if (!campos || list_size(campos) < 3) {
        log_error(storage_logger,
                  "TRUNCATE: paquete incompleto (se esperaban file:tag, tamaño, qid)");
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
        if (campos) list_destroy_and_destroy_elements(campos, free);
        return;
    }
    
    int qid = *(int*) list_get(campos, 0);
    char* file_tag = strdup(list_get(campos, 1));
    int* tam_ptr = (int*) list_get(campos, 2);
    int tamanio = *tam_ptr;
    
    char *file = NULL, *tag = NULL;

    if (split_file_tag(file_tag, &file, &tag) != 0) {
        log_error(storage_logger, "TRUNCATE mal formado: '%s'", file_tag);
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
        free(file_tag);
        list_destroy_and_destroy_elements(campos, free);
        return;
    }

    // PASAR QID A LA FUNCIÓN
    int resultado = operacion_truncate_file(qid, file, tag, tamanio);

    if (resultado == 0) {
        
        log_info(storage_logger,
                 "## Query %d - File Truncado %s:%s - Tamaño: %d",
                 qid, file, tag, tamanio);

        int ok = OP_RESP_OK;
        send(fd, &ok, sizeof(ok), 0);
    } else {
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
    }

    free(file_tag);
    free(file);
    free(tag);
    list_destroy_and_destroy_elements(campos, free);
}



// TAG //

void storage_tag(int fd) {
    usleep(configuracion.retardo_operacion * 1000);

    t_list* campos = recibir_paquete(fd);
    
    int qid = *(int*) list_get(campos, 0);
    char* origen = list_get(campos, 1);
    char* destino = list_get(campos, 2);

    char *file_o=NULL, *tag_o=NULL;
    char *file_d=NULL, *tag_d=NULL;

    if (split_file_tag(origen, &file_o, &tag_o) != 0 || 
        split_file_tag(destino, &file_d, &tag_d) != 0) {
        log_error(storage_logger, "TAG mal formado");
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
        list_destroy_and_destroy_elements(campos, free);
        free(file_o); free(tag_o);
        free(file_d); free(tag_d);
        return;
    }


    int resultado = operacion_tag_file(qid, file_o, tag_o, file_d, tag_d);
    
    if (resultado == 0) {
        log_info(storage_logger,
                 "## Query %d - Tag creado %s:%s", 
                 qid, file_d, tag_d);
        
        int resp = OP_RESP_OK;
        send(fd, &resp, sizeof(resp), 0);
    } else {
        log_error(storage_logger,
                  "## Query %d - Error al crear Tag %s:%s",
                  qid, file_d, tag_d);
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
    }

    list_destroy_and_destroy_elements(campos, free);
    free(file_o); free(tag_o);
    free(file_d); free(tag_d);
}

// DELETE //

void storage_delete(int fd) {
    usleep(configuracion.retardo_operacion * 1000);


    t_list* campos = recibir_paquete(fd);
    if (!campos || list_size(campos) < 3) {
        log_error(storage_logger, "DELETE: paquete incompleto (campos=%d)", campos ? list_size(campos) : 0);
        
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
        if (campos) list_destroy_and_destroy_elements(campos, free);

        return;
    }

    int  qid   = *(int*)  list_get(campos, 0);
    char *file = (char*) list_get(campos, 1);
    char *tag  = (char*) list_get(campos, 2);


    operacion_eliminar_tag(qid, file, tag);
    log_info(storage_logger, "## Query:%d - Tag Eliminado %s:%s", qid, file, tag);

    int ok = OP_RESP_OK;
    send(fd, &ok, sizeof(ok), 0);

    list_destroy_and_destroy_elements(campos, free);
}


// COMMIT //

void storage_commit(int fd) {
    usleep(configuracion.retardo_operacion * 1000);

    t_list* campos = recibir_paquete(fd);
   /* if (!campos || list_size(campos) < 1) {
        log_error(storage_logger, "COMMIT: paquete incompleto");
        return;
    }*/

    int qid = *(int*) list_get(campos, 0);
    char* file_tag = list_get(campos, 1);

    char *file=NULL, *tag=NULL;

    if (split_file_tag(file_tag, &file, &tag) != 0) {
        log_error(storage_logger, "COMMIT mal formado: %s", file_tag);
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
        list_destroy_and_destroy_elements(campos, free);
        return;
    }

    int respuesta = operacion_commit_tag(file, tag);
    int resp;
    if (respuesta == 0) {
        log_info(storage_logger,
                 "## Query %d - Commit de File:Tag %s:%s",
                 qid, file, tag);
        resp = OP_RESP_OK;
    } else if (respuesta == 1) {
        log_info(storage_logger,
                 "## Query %d - File:Tag %s:%s ya estaba COMMITED",
                 qid, file, tag);
        resp = OP_RESP_OK;  // desde el punto de vista del protocolo, no es error
    } else {
        log_error(storage_logger,
                  "## Query %d - Error en Commit de File:Tag %s:%s (respuesta =%d)",
                  qid, file, tag, respuesta );
        resp = OP_RESP_ERROR;
    }

    send(fd, &resp, sizeof(resp), 0);

    list_destroy(campos);
    free(file);
    free(tag);
}


// FLUSH //
void storage_flush(int fd) {
    usleep(configuracion.retardo_operacion * 1000);

    t_list* campos = recibir_paquete(fd);
    if (!campos || list_size(campos) < 3) {
        log_error(storage_logger,
                  "FLUSH: paquete incompleto (campos=%d)",
                  campos ? list_size(campos) : -1);
        int resp = OP_RESP_ERROR;
        send(fd, &resp, sizeof(resp), 0);
        if (campos) list_destroy_and_destroy_elements(campos, free);
        return;
    }

    int qid   = *(int*) list_get(campos, 0);
    char* file = list_get(campos, 1);
    char* tag  = list_get(campos, 2);

    log_info(storage_logger,
             "## Query:%d - FLUSH recibido para %s:%s", qid, file, tag);

    t_tag* un_tag = buscar_tag(file, tag);

    int resp = OP_RESP_OK;

    if (un_tag == NULL) {
        log_error(storage_logger,
                  "FLUSH ERROR: File:Tag %s:%s inexistente", file, tag);
        resp = OP_RESP_ERROR;
    }

    send(fd, &resp, sizeof(resp), 0);

    list_destroy_and_destroy_elements(campos, free);
}



// READ_BLOCK //

void storage_read_block(int fd) {
    usleep(configuracion.retardo_operacion * 1000);

    t_list* campos = recibir_paquete(fd);

    // RECIBIR QUERY_ID PRIMERO
    int qid = *(int*) list_get(campos, 0);
    char* file_tag = list_get(campos, 1);
    int* nro_pag_ptr = (int*) list_get(campos, 2);
    int nro_pag = *nro_pag_ptr;

    char *file = NULL, *tag = NULL;

    if (split_file_tag(file_tag, &file, &tag) != 0) {
        log_error(storage_logger, "READ_BLOCK mal formado: %s", file_tag);
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
        list_destroy_and_destroy_elements(campos, free);
        return;
    }

    t_tag* _tag = buscar_tag(file, tag);
    if (_tag == NULL) {
        log_error(storage_logger, "READ_BLOCK: File:Tag inexistente %s:%s", file, tag);
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
        list_destroy_and_destroy_elements(campos, free);
        return;
    }

    if (nro_pag < 0 || nro_pag >= list_size(_tag->lista_block_logicos)) {
        log_error(storage_logger, "READ_BLOCK: nro_pag fuera de rango (%d) para %s:%s",
                  nro_pag, file, tag);
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
        list_destroy_and_destroy_elements(campos, free);
        return;
    }

    t_block_logico* bl = list_get(_tag->lista_block_logicos, nro_pag);
    char* contenido = operacion_read_bloque(file, tag, bl->nombre_block);

    if (contenido != NULL) {
        usleep(configuracion.retardo_acceso * 1000);

        //LOG OBLIGATORIO CON QUERY_ID REAL
        log_info(storage_logger,
                 "## Query %d - Bloque Lógico Leído %s:%s - Número de Bloque: %d",
                 qid, file, tag, nro_pag);

        int ok = OP_RESP_OK;
        send(fd, &ok, sizeof(ok), 0);
        send(fd, contenido, superbloque.BLOCK_SIZE, 0);

        free(contenido);
    } else {
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
    }

    list_destroy_and_destroy_elements(campos, free);
}


// WRITE_BLOCK //
void storage_write_block(int fd) {
    usleep(configuracion.retardo_operacion * 1000);

    t_list* campos = recibir_paquete(fd);

    int qid          = *(int*) list_get(campos, 0);
    char* file       = list_get(campos, 1);
    char* tag        = list_get(campos, 2);
    uint32_t nro_pag = *(uint32_t*) list_get(campos, 3);
    size_t len       = *(size_t*) list_get(campos, 4);   // ⭐ NUEVO
    void* contenido  = list_get(campos, 5);

    //char *file = NULL, *tag = NULL;

    if (!campos || list_size(campos) < 5) {
        log_error(storage_logger, "WRITE_BLOCK mal formado (campos=%d)", campos ? list_size(campos) : -1);
        if (campos) list_destroy_and_destroy_elements(campos, free);
        return;
    }

    // Buscar TAG
    t_tag* _tag = buscar_tag(file, tag);
    if (_tag == NULL) {
        log_error(storage_logger, "WRITE_BLOCK: File:Tag inexistente %s:%s", file, tag);
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
        list_destroy_and_destroy_elements(campos, free);
        
        return;
    }

    // Validar índice de bloque lógico
    if (nro_pag >= list_size(_tag->lista_block_logicos)) {
        log_error(storage_logger, "WRITE_BLOCK: nro_pag fuera de rango (%d) para %s:%s",
                  nro_pag, file, tag);
        int error = OP_RESP_ERROR;
        send(fd, &error, sizeof(error), 0);
        list_destroy_and_destroy_elements(campos, free);
        
        return;
    }

    if (len > superbloque.BLOCK_SIZE) {
        log_error(storage_logger,
            "WRITE_BLOCK: tamaño inválido (%zu) para %s:%s pag=%u",
            len, file, tag, nro_pag);
        int err = OP_RESP_ERROR;
        send(fd, &err, sizeof(err), 0);
        return;
    }

    t_block_logico* _bl = list_get(_tag->lista_block_logicos, nro_pag);

    // Escribimos en el bloque lógico
    int resultado = operacion_write_bloque(qid, file, tag, _bl, contenido, len);

    if (resultado == 0) {
        usleep(configuracion.retardo_acceso * 1000);

        // ⭐ ESTE ES EL LOG OBLIGATORIO
        log_info(storage_logger,
            "## Query:%d - Bloque Lógico Escrito %s:%s - Número de Bloque: %u",
            qid, file, tag, nro_pag);

        int ok = OP_RESP_OK;
        send(fd, &ok, sizeof(ok), 0);
    }
    else {
        int err = OP_RESP_ERROR;
        send(fd, &err, sizeof(err), 0);
    }

    list_destroy_and_destroy_elements(campos, free);
}
