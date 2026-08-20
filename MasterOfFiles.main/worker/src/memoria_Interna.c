#include <memoria_interna.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <commons/log.h>
#include <commons/temporal.h>
#include <utils/protocolo.h>

extern t_log *logger;

char *direccion_base = NULL;
size_t TAM_MEMORIA;
size_t PAGE_SIZE;
size_t FRAMES;
Marco *frames = NULL;
uint32_t RETARDO_MEMORIA;
t_reemplazo ALGORITMO;

t_temporal *RELOJ_LRU = NULL;
int CONTEXT_QUERY_ID = -1;
size_t PUNTERO_RELOJ = 0;

// Protos de helpers
int elegir_algoritmo(void);



int flush_frame(size_t marco, int storage_fd);
int buscar_marco_libre(void);

/*                    Ciclo de vida de memoria                  */

int inicializar_memoria(Ram *info)
{
    TAM_MEMORIA = info->tam_memoria;
    PAGE_SIZE = info->page_size; // = BLOCK_SIZE (handshake)
    FRAMES = info->tam_memoria / info->page_size;
    RETARDO_MEMORIA = info->retardo;
    ALGORITMO = info->algORITMO;

    direccion_base = malloc(TAM_MEMORIA);
    if (direccion_base == NULL)
    {
        log_error(logger, "No se pudo reservar la Memoria Interna (malloc).");
        return -1;
    }
    memset(direccion_base, 0, TAM_MEMORIA);

    frames = calloc(FRAMES, sizeof(Marco));
    if (frames == NULL)
    {
        log_error(logger, "No se pudo reservar la tabla de frames.");
        free(direccion_base);
        direccion_base = NULL;
        return -1;
    }

    for (size_t i = 0; i < FRAMES; i++)
    {
        frames[i].libre = true;
        frames[i].base = direccion_base + (i * PAGE_SIZE);
        frames[i].file = NULL;
        frames[i].tag = NULL;
        frames[i].pagina = 0;
        frames[i].dirty = false;
        frames[i].U = frames[i].M = 0;
        frames[i].last_used = 0;
    }

    RELOJ_LRU = temporal_create();
    CONTEXT_QUERY_ID = -1;
    PUNTERO_RELOJ = 0;

    log_info(logger, "Memoria Interna creada: %zu bytes (%zu frames de %zu) - Algoritmo=%s",
             TAM_MEMORIA, FRAMES, PAGE_SIZE,
             (ALGORITMO == REEMPLAZO_LRU ? "LRU" : "CLOCK-M"));
    return 0;
}

void destrozar_memoria(void)
{
    if (frames)
    {
        for (size_t i = 0; i < FRAMES; i++)
        {
            free(frames[i].file);
            free(frames[i].tag);
        }
        free(frames);
        frames = NULL;
    }
    if (direccion_base)
    {
        free(direccion_base);
        direccion_base = NULL;
    }
    if (RELOJ_LRU)
    {
        temporal_destroy(RELOJ_LRU);
        RELOJ_LRU = NULL;
    }
    log_info(logger, "Memoria Interna liberada.");
}

/*                    Utilidades de tiempo/retardo              */

uint64_t milisegundos_actual(void)
{
    return (uint64_t)temporal_gettime(RELOJ_LRU);
}

void aplicar_retardo_memoria(void){
    if (RETARDO_MEMORIA)
        usleep(RETARDO_MEMORIA * 1000);
}

void memoria_set_context(int query_id)
{
    CONTEXT_QUERY_ID = query_id;
}

/*                 Worker <-> Storage (protocolo real)          */

int lectura_pagina_storage(int fd, char *file, char *tag, uint32_t nro_pag, void *out)
{
    // Crear file_tag = "file:tag"
    char *file_tag = malloc(strlen(file) + strlen(tag) + 2);
    sprintf(file_tag, "%s:%s", file, tag);

    t_paquete *p = crear_paquete();
    p->codigo_operacion = OP_ST_READ_BLOCK;

    // ENVIAR QUERY_ID PRIMERO
    agregar_a_paquete(p, &CONTEXT_QUERY_ID, sizeof(int));
    
    // Enviar file:tag
    agregar_a_paquete(p, file_tag, (int)strlen(file_tag) + 1);

    // nro de página
    agregar_a_paquete(p, &nro_pag, sizeof(uint32_t));

    enviar_paquete(p, fd);

    free(file_tag);
    eliminar_paquete(p);

    int op_resp = 0;
    if (recv(fd, &op_resp, sizeof(op_resp), MSG_WAITALL) <= 0) {
        log_error(logger, "Error recibiendo respuesta de Storage en READ_BLOCK");
        return -1;
    }

    if (op_resp != OP_RESP_OK) {
        log_error(logger, "Storage READ_BLOCK ERROR para %s:%s pag=%u",
                  file, tag, nro_pag);
        return -1;
    }

    // pag completa
    size_t recvd = 0;
    while (recvd < PAGE_SIZE) {
    ssize_t n = recv(fd,
                     (char*)out + recvd,
                     PAGE_SIZE - recvd,
                     MSG_WAITALL);
    if (n <= 0)
        return -1;
    recvd += n;
}

    return 0;
}
int escribir_pagina_storage(int fd, char *file, char *tag, uint32_t nro_pag, void *in, size_t len)
{
    t_paquete *p = crear_paquete();
    p->codigo_operacion = OP_ST_WRITE_BLOCK;

    agregar_a_paquete(p, &CONTEXT_QUERY_ID, sizeof(int));
    agregar_a_paquete(p, file, strlen(file) + 1);
    agregar_a_paquete(p, tag, strlen(tag) + 1);
    agregar_a_paquete(p, &nro_pag, sizeof(uint32_t));
    agregar_a_paquete(p, &len, sizeof(size_t));   // ⭐ NUEVO
    agregar_a_paquete(p, in, len);

    enviar_paquete(p, fd);
    eliminar_paquete(p);

    int op_resp = 0;
    if (recv(fd, &op_resp, sizeof(op_resp), MSG_WAITALL) <= 0) {
        log_error(logger, "Error recibiendo respuesta de Storage en WRITE_BLOCK");
        return -1;
    }

    if (op_resp == OP_RESP_ERROR) {
        log_error(logger, "Storage devolvió OP_RESP_ERROR al escribir %s:%s pag=%u",
                file, tag, nro_pag);
        return -1;
    }

    if (op_resp != OP_RESP_OK) {
        log_error(logger, "Opcode inesperado de Storage en WRITE_BLOCK: %d", op_resp);
        return -1;
    }

    return 0;
}

// Operaciones de RAM  //

int buscar_marco_libre(void) {
    for (size_t i = 0; i < FRAMES; i++)
        if (frames[i].libre)
            return (int)i;
    return -1;
}

bool frame_matchea(Marco* f,  char* file,  char* tag, uint32_t nro_pag) {
    if (f->libre) return false;
    if (!f->file || !f->tag) return false;

    if (f->pagina != nro_pag) return false;
    if (strcmp(f->file, file) != 0) return false;
    if (strcmp(f->tag,  tag ) != 0) return false;

    return true;
}

int traer_pagina(char *file, char *tag, uint32_t nro_pag, int storage_fd) {
    
    for (size_t i = 0; i < FRAMES; i++) {
        
        if (frame_matchea(&frames[i], file, tag, nro_pag)){
            frames[i].U = 1;
            frames[i].last_used = milisegundos_actual();
            return (int)i;
        }
    }

    log_info(logger, "Query %d: - Memoria Miss - File: %s - Tag: %s - Pagina: %u",
             CONTEXT_QUERY_ID, file, tag, nro_pag);

    int marco = buscar_marco_libre();
    if (marco < 0)
    {
        int victima = elegir_algoritmo();
        if (victima < 0)
            return -1;

        log_info(logger, "## Query %d: Se reemplaza la página %s:%s/%u por la %s:%s/%u",
                 CONTEXT_QUERY_ID,
                 frames[victima].file ? frames[victima].file : "<none>",
                 frames[victima].tag ? frames[victima].tag : "<none>",
                 frames[victima].pagina, file, tag, nro_pag);

        if (frames[victima].dirty){
            log_info(logger, "Query %d: Flush de Página %s:%s/%u (víctima)",
                     CONTEXT_QUERY_ID, frames[victima].file, frames[victima].tag, frames[victima].pagina);
        }

        if (flush_frame((size_t)victima, storage_fd) != 0)
            return -1;

        log_info(logger, "Query %d: Se libera el Marco: %d perteneciente al - File: %s - Tag: %s",
                 CONTEXT_QUERY_ID, victima,
                 frames[victima].file ? frames[victima].file : "<none>",
                 frames[victima].tag ? frames[victima].tag : "<none>");

        free(frames[victima].file);
        frames[victima].file = NULL;
        free(frames[victima].tag);
        frames[victima].tag = NULL;
        marco = victima;
    }

    if (lectura_pagina_storage(storage_fd, file, tag, nro_pag,
                               frames[marco].base) != 0)
        return -1;

    frames[marco].libre = false;
    free(frames[marco].file);
    free(frames[marco].tag);
    frames[marco].file = strdup(file);
    frames[marco].tag = strdup(tag);
    frames[marco].pagina = nro_pag;
    frames[marco].dirty = false;
    frames[marco].U = 1;
    frames[marco].M = 0;
    frames[marco].last_used = milisegundos_actual();

    log_info(logger, "Query %d: - Memoria Add - File: %s - Tag: %s - Pagina: %u - Marco: %d",
             CONTEXT_QUERY_ID, file, tag, nro_pag, marco);
    log_info(logger, "Query %d: Se asigna el Marco: %d a la Página: %u perteneciente al - File: %s - Tag: %s",
             CONTEXT_QUERY_ID, marco, nro_pag, file, tag);
    return marco;
}

int acceder_memoria(char *file, char *tag, size_t base, size_t size, bool escribir,
                    void *entrada, void *salida, int storage_fd)
{
    size_t restante = size, desplaz = 0;

    while (restante > 0)
    {
        uint32_t nro_pag = (uint32_t)((base + desplaz) / PAGE_SIZE);
        size_t offset = (base + desplaz) % PAGE_SIZE;
        size_t chunk = PAGE_SIZE - offset;
        if (chunk > restante)
            chunk = restante;

        int marco = traer_pagina(file, tag, nro_pag, storage_fd);
        if (marco < 0)
            return -1;

        aplicar_retardo_memoria();

        void *ptr_fis = frames[marco].base + offset;
        if (escribir)
        {
            memcpy(ptr_fis, (char *)entrada + desplaz, chunk);
            frames[marco].dirty = true;
            frames[marco].M = 1;
            log_info(logger, "Query %d: Acción: ESCRIBIR - Dirección Física: %p - Valor: <BIN:%zu bytes>",
                     CONTEXT_QUERY_ID, ptr_fis, chunk);
        }
        else
        {
            memcpy((char *)salida + desplaz, ptr_fis, chunk);
            log_info(logger, "Query %d: Acción: LEER - Dirección Física: %p - Valor: <BIN:%zu bytes>",
                     CONTEXT_QUERY_ID, ptr_fis, chunk);
        }
        frames[marco].U = 1;
        frames[marco].last_used = milisegundos_actual();

        desplaz += chunk;
        restante -= chunk;
    }
    return 0;
}

int flush_todos_filetags_modificados(int storage_fd) {
    // Recorremos todos los frames y, por cada File:Tag sucio,
    // reutilizamos flush_filetag. Este a su vez limpia todos
    // los frames de ese File:Tag.
    for (size_t i = 0; i < FRAMES; i++) {
        if (!frames[i].libre &&
            frames[i].file &&
            frames[i].tag &&
            frames[i].dirty) {

            // flush_filetag se encarga de todas las páginas de ese File:Tag
            if (flush_filetag(frames[i].file, frames[i].tag, storage_fd) != 0) {
                log_error(logger,
                          "Query %d: Error al hacer FLUSH implicito de %s:%s",
                          CONTEXT_QUERY_ID, frames[i].file, frames[i].tag);
                return -1;
            }
        }
    }

    log_info(logger,
             "Query %d: Flush implicito por desalojo completado",
             CONTEXT_QUERY_ID);
    return 0;
}


int flush_filetag(char *file, char *tag, int storage_fd) {
    int paginas_escritas = 0;
    
    for (size_t i = 0; i < FRAMES; i++) {
        
        if (!frames[i].libre && 
            frames[i].file && 
            frames[i].tag && 
            strcmp(frames[i].file, file) == 0 &&
            strcmp(frames[i].tag, tag) == 0 &&
            frames[i].dirty) {
            
            // Log obligatorio
            log_info(logger, 
                     "Query %d: Flushing página %s:%s/%u desde marco %zu",
                     CONTEXT_QUERY_ID, file, tag, frames[i].pagina, i);
            
            
            if (escribir_pagina_storage(storage_fd,
                                        frames[i].file, 
                                        frames[i].tag,
                                        frames[i].pagina, 
                                        frames[i].base, 
                                        PAGE_SIZE) != 0) {
                log_error(logger,
                          "Query %d: Error al escribir página %s:%s/%u durante FLUSH",
                          CONTEXT_QUERY_ID, file, tag, frames[i].pagina);
                return -1;
            }
            
            // limpio dps de escribir exitosamente
            frames[i].dirty = false;
            frames[i].M = 0;
            paginas_escritas++;
        }
    }
    
    log_info(logger, 
             "Query %d: Flush de File:Tag %s:%s completado (%d páginas escritas)",
             CONTEXT_QUERY_ID, file, tag, paginas_escritas);
    
    return 0;
}

int flush_frame(size_t i, int storage_fd)
{
    if (!frames[i].libre && frames[i].dirty)
    {
        if (escribir_pagina_storage(storage_fd,
                                    frames[i].file, frames[i].tag,
                                    frames[i].pagina, frames[i].base, PAGE_SIZE) != 0)
            return -1;
        frames[i].dirty = false;
        frames[i].M = 0;
    }
    return 0;
}

int algoritmo_LRU(void) {
    int victima = -1;
    uint64_t peor = UINT64_MAX;
    for (size_t i = 0; i < FRAMES; i++)
    {
        if (!frames[i].libre && frames[i].last_used < peor)
        {
            peor = frames[i].last_used;
            victima = (int)i;
        }
    }
    return victima;
}

int algoritmo_CLOCKM(void) {
    //primera busqueda = (0;0)
    for (size_t k = 0; k < FRAMES; k++) {
        size_t i = (PUNTERO_RELOJ + k) % FRAMES;
        if (!frames[i].libre && frames[i].U == 0 && frames[i].M == 0)
        {
            PUNTERO_RELOJ = (i + 1) % FRAMES;
            return (int)i;
        }
    }

    //si no, busca (0,1)
    for (size_t k = 0; k < FRAMES; k++){

        size_t i = (PUNTERO_RELOJ + k) % FRAMES;
        if (!frames[i].libre)
        {
            if (frames[i].U == 0 && frames[i].M == 1)
            {
                PUNTERO_RELOJ = (i + 1) % FRAMES;
                return (int)i;
            }
            if (frames[i].U == 1)
                frames[i].U = 0;
        }
    }
    //si no pasa lo anterior, entonces:
    for (size_t k = 0; k < FRAMES; k++) {

        size_t i = (PUNTERO_RELOJ + k) % FRAMES;
        if (!frames[i].libre) {
            PUNTERO_RELOJ = (i + 1) % FRAMES;
            return (int)i;
        }
    }  
    return -1;
}

int elegir_algoritmo(void){
    return (ALGORITMO == REEMPLAZO_LRU) ? algoritmo_LRU() : algoritmo_CLOCKM();
}
