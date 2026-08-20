
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <instrucciones.h>
#include <utils/protocolo.h>
#include <commons/string.h>
#include <memoria_interna.h>
#include <commons/log.h>

volatile bool query_cancelada = false;
extern bool desalojado;
extern int CONTEXT_QUERY_ID;
extern size_t FRAME_COUNT;
extern Marco* frames;

//t_log* logger = NULL;
extern t_log* logger;

int parse_size_t_safe(const char* txt, size_t* out) {
    if (!txt || !out) return -1;

    char* end = NULL;
    unsigned long val = strtoul(txt, &end, 10);

    if (*end != '\0')  // Sobran caracteres → no es número
        return -1;

    *out = (size_t)val;
    return 0;
}



/*char* generar_path_query(char* path_queries, char* nombre_query) {
    if (path_queries == NULL || nombre_query == NULL)
        return NULL;

    // string_new y string_append son commons
    char* path = string_new();

    // por si el path no temrina con / (que creo que si asi que tomar con pinzas)
    if (path_queries[strlen(path_queries) - 1] == '/')
        string_append(&path, path_queries);
    else {
        string_append(&path, path_queries);
        string_append(&path, "/");
    }

    // agrega el nombre el query al path (ya con al ruta del config)
    string_append(&path, nombre_query);

    return path; // Recordá liberar después con free()
}*/

char* generar_path_query(char* path_queries, char* nombre_query) {
    if (!path_queries || !nombre_query)
        return NULL;

    // Normalizar: agregar '/' si no está
    bool termina_con_slash = path_queries[strlen(path_queries) - 1] == '/';

    char* path = termina_con_slash
        ? string_from_format("%s%s", path_queries, nombre_query)
        : string_from_format("%s/%s", path_queries, nombre_query);

    return path;  // liberar con free()
}


// --------- INSTRUCCIONES --------
// borra espacios y saltos de línea al principio y al final
static char* limpiar_linea(char* texto) {
    if (!texto) return texto;

    size_t largo = strlen(texto);
    while (largo && (texto[largo - 1] == '\n' || texto[largo - 1] == '\r' || texto[largo - 1] == ' ' || texto[largo - 1] == '\t'))
        texto[--largo] = '\0';
    while (*texto == ' ' || *texto == '\t')
        texto++;

    return texto;
}

// Separa una instrucción en sus partes: comando y hasta 3 argumentos
void parsear_instruccion(char* linea, Instruccion* instr) {
    instr->comando[0] = instr->arg1[0] = instr->arg2[0] = instr->arg3[0] = '\0';
    if (!linea) return;
    // 4to arg: todo lo que queda (sirve para WRITE con espacios)
    sscanf(linea, "%31s %127s %127s %255[^\n]", instr->comando, instr->arg1, instr->arg2, instr->arg3);
}

int separar_FileTag(const char* filetag, char** file_out, char** tag_out) {
    if (!filetag) return -1;

    const char* sep = strchr(filetag, ':');
    if (!sep) return -1; // no hay ':'

    size_t len_file = (size_t)(sep - filetag);
    size_t len_tag  = strlen(sep + 1);

    *file_out = malloc(len_file + 1);
    *tag_out  = malloc(len_tag + 1);

    if (!*file_out || !*tag_out) {
        free(*file_out);
        free(*tag_out);
        return -1;
    }

    memcpy(*file_out, filetag, len_file);
    (*file_out)[len_file] = '\0';

    memcpy(*tag_out, sep + 1, len_tag + 1); // copia también el '\0'

    return 0;
}

// ---------- EJECUCIÓN DE UNA INSTRUCCIÓN (por ahora solo logs) ----------
//agregue parametros pq la funcion de truncate las necesita para comunicarse con el storage
void ejecutar_instruccion(Instruccion* inst, int id_query, int socket_master, int socket_storage, int block_size, t_log* logger) {
    char* c = inst->comando;
    if (c[0] == '\0'){
     log_warning(logger,"Query %d: instruccion vacia, salta", id_query);        
     return;
    }

    if (strcmp(c, "CREATE") == 0)
        ejecutar_Create(inst, socket_storage, logger);

    else if (strcmp(c, "TRUNCATE") == 0)
        ejecutar_Truncate(inst, socket_storage, block_size, logger);

    else if (strcmp(c, "WRITE") == 0)
        ejecutar_Write(inst, socket_storage, logger);

    else if (strcmp(c, "READ") == 0)  {     // <-- ACÁ VA TU ELSE
            int rc = ejecutar_Read(inst, socket_master, socket_storage, logger);
            if (rc != 0) {
                log_error(logger, "READ falló (rc=%d)", rc);
                // si querés, avisás al master del error
            }
    }
    else if (strcmp(c, "TAG") == 0)
        ejecutar_Tag(inst, socket_storage, logger);

    else if (strcmp(c, "COMMIT") == 0)
        ejecutar_Commit(inst, socket_storage, logger);

    else if (strcmp(c, "FLUSH") == 0)
        ejecutar_Flush(inst, socket_storage, logger);

    else if (strcmp(c, "DELETE") == 0)
        ejecutar_Delete(inst, socket_storage, logger);

    else if (strcmp(c, "END") == 0)
        ejecutar_End(id_query, socket_master, logger);
        
        else log_warning(logger, "Instruccion desconocida %d", id_query);

        // desp va lo de Memoria Interna y Storage según `inst->comando` y sus args.
}

size_t ciclo_instruccion(char* texto_original, int id_query, size_t pc,int socket_master, int socket_storage, int block_size, t_log* logger){
    
    if (query_cancelada) {  
        log_warning(logger, "## Query %d cancelada por desconexión del Query Control. Se aborta.", id_query);
        return pc;  // detenemos ejecución
    }
    
    char* tmp = strdup(texto_original ? texto_original : "");
    char* instruccion = limpiar_linea(tmp);
    if (instruccion[0] == '\0') { free(tmp); return pc + 1; }

    //Fetch
    memoria_set_context(id_query);
    log_info(logger, "## Query %d: FETCH - Program Counter: %ld - %s", id_query, pc, instruccion);
    
    //partir instruccion
    Instruccion inst;
    parsear_instruccion(instruccion, &inst);

    //Ejecutar
    ejecutar_instruccion(&inst, id_query, socket_master, socket_storage, block_size, logger);

    // Actualizamos Program Counter global
    pc += 1;

    // Si el master pidió pausar → avisamos y frenamos
    if (desalojado) {
        log_warning(logger,
                "## Query %d pausada en PC=%ld. Ejecutando FLUSH implicito antes del desalojo.",
                id_query, pc);

    
        if (flush_todos_filetags_modificados(socket_storage) != 0) {
            log_error(logger,
                    "## Query %d: Error al realizar FLUSH implicito antes del desalojo",
                    id_query);
        }

        // LOG OBLIGATORIO
        log_info(logger, "## Query %d: Desalojada por pedido del Master", id_query);

        t_paquete* p = crear_paquete();
        p->codigo_operacion = OP_QUERY_DESALOJADA;
        agregar_a_paquete(p, &id_query, sizeof(id_query));
        agregar_a_paquete(p, &pc, sizeof(pc));
        enviar_paquete(p, socket_master);
        eliminar_paquete(p);

        free(tmp);
        return pc;
    }
    return pc;
}

int procesar_query(char* ruta, int id_query, size_t pc_inicial, int socket_master, int socket_storage, int block_size, t_log* logger) {
    FILE* f = fopen(ruta, "r");
    if (!f) { log_error(logger, "No se pudo abrir el archivo de Query: %s", ruta); return -1; }

    char* linea = NULL; 
    size_t cap = 0; 
    ssize_t n;
    size_t pc = pc_inicial > 0 ? pc_inicial : 1;

    // ir hasta PC que indica (reanudación)
    size_t actual = 1;
    while (actual < pc && (n = getline(&linea, &cap, f)) != -1) actual++;

    // Iterar
    while ((n = getline(&linea, &cap, f)) != -1) {

        if (query_cancelada) {   //Detiene el ciclo si se desconectó
            log_warning(logger, "## Query %d abortada durante ejecución (desconectado Query Control)", id_query);
            break;
        }
        pc = ciclo_instruccion(linea, id_query, pc, socket_master, socket_storage, block_size, logger);
    }

    free(linea);
    fclose(f);
    return 0;
}


//Funciones de instrucciones
int ejecutar_Create(Instruccion* inst, int socket_storage, t_log* logger) {

    // ----- Validar formato -----
    if (inst->arg1[1] == '\0') {
        log_error(logger, "## Query %d: CREATE mal formado (falta <file:tag>)",
                 CONTEXT_QUERY_ID);
        return -1;
    }

    log_info(logger, "## Query %d: Ejecutando CREATE - %s",
             CONTEXT_QUERY_ID, inst->arg1);

    // ----- Enviar a Storage -----
    t_paquete* paquete = crear_paquete();
    paquete->codigo_operacion = OP_CREATE;
   
    agregar_a_paquete(paquete, &CONTEXT_QUERY_ID, sizeof(int));
    // Storage recibe <file:tag> y genera file + tag vacío, tamaño 0
    agregar_a_paquete(paquete, inst->arg1, strlen(inst->arg1) + 1);

    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);

    // ----- Recibir respuesta -----
    int respuesta = 0;
    recv(socket_storage, &respuesta, sizeof(respuesta), MSG_WAITALL);

    if (respuesta == OP_RESP_OK) {
        log_info(logger, "## Query %d: CREATE OK - %s",
                 CONTEXT_QUERY_ID, inst->arg1);
        return 0;
    } else {
        log_error(logger, "## Query %d: CREATE ERR - %s",
                  CONTEXT_QUERY_ID, inst->arg1);
        return -1;
    }
}


int ejecutar_Truncate(Instruccion* inst, int socket_storage, int block_size, t_log* logger) {

    // ---------------- VALIDACIONES BÁSICAS ----------------
    if (inst->arg1[0] == '\0' || inst->arg2[0] == '\0') {
        log_error(logger, "## Query %d: TRUNCATE mal formado", CONTEXT_QUERY_ID);
        return -1;
    }

    int tamanio = atoi(inst->arg2);
    if (tamanio % block_size != 0) {
        log_error(logger, "## Query %d: TRUNCATE tamaño %d no es múltiplo del BLOCK_SIZE=%d",
                 CONTEXT_QUERY_ID, tamanio, block_size);
        return -1;
    }

    log_info(logger,
             "## Query %d: Ejecutando TRUNCATE - %s a %d bytes",
             CONTEXT_QUERY_ID, inst->arg1, tamanio);

    // ---------------- ENVIAR A STORAGE ----------------
    t_paquete* paquete = crear_paquete();
    paquete->codigo_operacion = OP_TRUNCATE;

    agregar_a_paquete(paquete, &CONTEXT_QUERY_ID, sizeof(int));
    agregar_a_paquete(paquete, inst->arg1, strlen(inst->arg1) + 1);
    agregar_a_paquete(paquete, &tamanio, sizeof(int));

    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);

    int respuesta = 0;
    if (recv(socket_storage, &respuesta, sizeof(respuesta), MSG_WAITALL) <= 0) {
        log_error(logger, "## Query %d: Error recibiendo respuesta de Storage en TRUNCATE",
                  CONTEXT_QUERY_ID);
        return -1;
    }

    if (respuesta != OP_RESP_OK) {
        log_error(logger, "## Query %d: TRUNCATE falló en Storage - %s",
                  CONTEXT_QUERY_ID, inst->arg1);
        return -1;
    }

    log_info(logger,
             "## Query %d: TRUNCATE OK (Storage) - validando RAM fuera de rango",
             CONTEXT_QUERY_ID);

    // ---------------- AJUSTAR MEMORIA INTERNA ----------------
    // file:tag → split en dos strings
    char *file = NULL, *tag = NULL;
    if (separar_FileTag(inst->arg1, &file, &tag) != 0) {
        log_error(logger, "## Query %d: Error separar_FileTag en TRUNCATE", CONTEXT_QUERY_ID);
        return -1;
    }

    int paginas_validas = tamanio / block_size;

    for (size_t i = 0; i < FRAME_COUNT; i++) {

        if (!frames[i].libre &&
            frames[i].file &&
            frames[i].tag &&
            strcmp(frames[i].file, file) == 0 &&
            strcmp(frames[i].tag, tag) == 0 &&
            frames[i].pagina >= (uint32_t) paginas_validas) {

            // ---------------- FLUSH SI DIRTY ----------------
            if (frames[i].dirty) {
                if (flush_frame(i, socket_storage)!= 0) {
                    log_error(logger,
                              "## Query %d: Error en flush de marco %zu durante TRUNCATE",
                              CONTEXT_QUERY_ID, i);
                }
            }

            // ---------------- LOG DE LIBERACIÓN ----------------
            
            log_info(logger,
                          "Query %d: Se libera el Marco: %zu perteneciente al - File: %s - Tag: %s",
                           CONTEXT_QUERY_ID, i,
                           frames[i].file, frames[i].tag);

            // ---------------- LIBERAR EL FRAME ----------------
            frames[i].libre = true;
            free(frames[i].file);
            free(frames[i].tag);
            frames[i].file = NULL;
            frames[i].tag  = NULL;
            frames[i].dirty = false;
            frames[i].U = frames[i].M = 0;
        }
    }

    free(file);
    free(tag);

    return 0;
}

int ejecutar_Write(Instruccion* inst, int socket_storage, t_log* logger) {

    // --- Validaciones ---
    char *file = NULL, *tag = NULL;
    size_t base = 0;

    if (separar_FileTag(inst->arg1, &file, &tag) != 0 ||
        parse_size_t_safe(inst->arg2, &base) != 0 ||
        inst->arg3[0] == '\0') {

        log_error(logger,
                  "## Query %d: WRITE mal formado. Uso: WRITE <file:tag> <base> <contenido>",
                  CONTEXT_QUERY_ID);
        free(file); free(tag);
        return -1;
    }

    // --- Preparar contenido ---
    char* contenido = inst->arg3;
    size_t tam = strlen(contenido);

    void* in = (tam ? malloc(tam) : NULL);
    if (tam && !in) {
        free(file); free(tag);
        return -1;
    }
    if (tam) memcpy(in, contenido, tam);

    log_info(logger,
             "## Query %d: Ejecutando WRITE - %s:%s - base=%zu tam=%zu",
             CONTEXT_QUERY_ID, file, tag, base, tam);

    // --- Escribir en Memoria Interna ---
    int rc = acceder_memoria(file, tag, base, tam, true, in, NULL, socket_storage);

    if (rc == 0)
        log_info(logger,
                 "## Query %d: WRITE OK - %s:%s - base=%zu tam=%zu",
                 CONTEXT_QUERY_ID, file, tag, base, tam);


                 
    else
        log_error(logger,
                  "## Query %d: WRITE ERROR - %s:%s - base=%zu tam=%zu",
                  CONTEXT_QUERY_ID, file, tag, base, tam);

    free(in);
    free(file);
    free(tag);

    return rc;
}



int ejecutar_Read(Instruccion* inst, int socket_master, int socket_storage, t_log* logger) {
    // Formato esperado:
    // READ <file:tag> <direccion_base> <tamanio>

    char *nombre_archivo = NULL;
    char *tag = NULL;
    size_t direccion_base = 0;
    size_t tamanio_lectura = 0;

    // ---- Validación de parámetros ----
    if (separar_FileTag(inst->arg1, &nombre_archivo, &tag) != 0 ||
        parse_size_t_safe(inst->arg2, &direccion_base) != 0 ||
        parse_size_t_safe(inst->arg3, &tamanio_lectura) != 0) {

        log_error(logger,
                  "## Query %d: READ mal formado. Uso: READ <file:tag> <base> <tamanio>",
                  CONTEXT_QUERY_ID);
        free(nombre_archivo);
        free(tag);
        return -1;
    }

    log_info(logger,
             "## Query %d: Ejecutando READ - %s:%s - base=%zu tam=%zu",
             CONTEXT_QUERY_ID, nombre_archivo, tag,
             direccion_base, tamanio_lectura);

    // ---- Buffer donde se guardará lo leído de la Memoria Interna ----
    void* buffer_lectura = (tamanio_lectura ? malloc(tamanio_lectura) : NULL);
    if (tamanio_lectura && !buffer_lectura) {
        log_error(logger,
                  "## Query %d: Error reservando buffer para READ",
                  CONTEXT_QUERY_ID);
        free(nombre_archivo);
        free(tag);
        return -1;
    }

    // ---- Leer desde Memoria Interna (y desde Storage si faltan páginas) ----
    int resultado_memoria = acceder_memoria(
        nombre_archivo,
        tag,
        direccion_base,
        tamanio_lectura,
        false,              // false => es lectura
        NULL,               // entrada (para escribir) => NULL en READ
        buffer_lectura,     // salida: acá se guardan los bytes leídos
        socket_storage      // fd a Storage para page faults
    );

    if (resultado_memoria == 0) {
        // ---- Enviar contenido leído al Master ----
        t_paquete* paquete = crear_paquete();
        paquete->codigo_operacion = OP_RESP_READ_OK;  // usá tu opcode real

        // Mandamos primero el <file:tag>, después tamaño y contenido
        agregar_a_paquete(paquete, inst->arg1, (int)strlen(inst->arg1) + 1);

        int tamanio_int = (int)tamanio_lectura;
        agregar_a_paquete(paquete, &tamanio_int, sizeof(int));

        if (tamanio_lectura > 0) {
            agregar_a_paquete(paquete, buffer_lectura, (int)tamanio_lectura);
        }

        enviar_paquete(paquete, socket_master);
        eliminar_paquete(paquete);

        log_info(logger,
                 "## Query %d: READ OK - %s:%s - base=%zu tam=%zu",
                 CONTEXT_QUERY_ID, nombre_archivo, tag,
                 direccion_base, tamanio_lectura);
    } else {
        // ---- Error en Memoria Interna / Storage ----
        t_paquete* paquete = crear_paquete();
        paquete->codigo_operacion = OP_RESP_READ_ERR; // tu opcode de error
        // Podrías agregar también el <file:tag> y la info del error si querés
        enviar_paquete(paquete, socket_master);
        eliminar_paquete(paquete);

        log_error(logger,
                  "## Query %d: READ ERROR - %s:%s - base=%zu tam=%zu",
                  CONTEXT_QUERY_ID, nombre_archivo, tag,
                  direccion_base, tamanio_lectura);
    }

    free(buffer_lectura);
    free(nombre_archivo);
    free(tag);

    return resultado_memoria;
}



int ejecutar_Tag(Instruccion* inst, int socket_storage, t_log* logger) {

    char *file_origen=NULL, *tag_origen=NULL;
    char *file_dest=NULL, *tag_dest=NULL;

    if (separar_FileTag(inst->arg1, &file_origen, &tag_origen) != 0 ||
        separar_FileTag(inst->arg2, &file_dest, &tag_dest) != 0) {

        log_error(logger,
            "## Query %d: TAG mal formado. Uso: TAG <file_src:tag_src> <file_dst:tag_dst>",
            CONTEXT_QUERY_ID);

        free(file_origen); free(tag_origen);
        free(file_dest);  free(tag_dest);
        return -1;
    }

    log_info(logger,
             "## Query %d: Ejecutando TAG - %s:%s -> %s:%s",
             CONTEXT_QUERY_ID, file_origen, tag_origen, file_dest, tag_dest);

    t_paquete* paquete = crear_paquete();
    paquete->codigo_operacion = OP_TAG;

    agregar_a_paquete(paquete, &CONTEXT_QUERY_ID, sizeof(int));
    agregar_a_paquete(paquete, inst->arg1, strlen(inst->arg1)+1);
    agregar_a_paquete(paquete, inst->arg2, strlen(inst->arg2)+1);

    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);

    int respuesta = 0;
    recv(socket_storage, &respuesta, sizeof(respuesta), MSG_WAITALL);

    if (respuesta == OP_RESP_OK)
        log_info(logger, "## Query %d: TAG OK - %s:%s → %s:%s",
                 CONTEXT_QUERY_ID, file_origen, tag_origen, file_dest, tag_dest);
    else
        log_error(logger, "## Query %d: TAG ERR - %s:%s → %s:%s",
                 CONTEXT_QUERY_ID, file_origen, tag_origen, file_dest, tag_dest);

    free(file_origen); free(tag_origen);
    free(file_dest); free(tag_dest);
    return (respuesta == OP_RESP_OK) ? 0 : -1;
}

int ejecutar_Commit(Instruccion* inst, int socket_storage, t_log* logger) {

    char *file=NULL, *tag=NULL;

    if (separar_FileTag(inst->arg1, &file, &tag) != 0) {
        log_error(logger,
                  "## Query %d: COMMIT mal formado. Uso: COMMIT <file:tag>",
                  CONTEXT_QUERY_ID);
        free(file); free(tag);
        return -1;
    }

    log_info(logger, "## Query %d: Ejecutando COMMIT - %s:%s",
             CONTEXT_QUERY_ID, file, tag);

    // FLUSH OBLIGATORIO PREVIO
    if (flush_filetag(file, tag, socket_storage) != 0) {
        log_error(logger,
                  "## Query %d: COMMIT ERR (flush previo) - %s:%s",
                  CONTEXT_QUERY_ID, file, tag);
        free(file); free(tag);
        return -1;
    }

    // Avisar a STORAGE que COMMITTEA
    t_paquete* p = crear_paquete();
    p->codigo_operacion = OP_COMMIT;
    
    agregar_a_paquete(p, &CONTEXT_QUERY_ID, sizeof(int));
    agregar_a_paquete(p, inst->arg1, strlen(inst->arg1)+1);
    enviar_paquete(p, socket_storage);
    eliminar_paquete(p);

    int respuesta = 0;
    recv(socket_storage, &respuesta, sizeof(respuesta), MSG_WAITALL);

    if (respuesta == OP_RESP_OK)
        log_info(logger, "## Query %d: COMMIT OK - %s:%s",
                 CONTEXT_QUERY_ID, file, tag);
    else
        log_error(logger, "## Query %d: COMMIT ERR (Storage) - %s:%s",
                  CONTEXT_QUERY_ID, file, tag);

    free(file); free(tag);
    return (respuesta == OP_RESP_OK) ? 0 : -1;
}


int ejecutar_Flush(Instruccion* inst, int socket_storage, t_log* logger) {

    char *file = NULL, *tag = NULL;

    if (separar_FileTag(inst->arg1, &file, &tag) != 0) {
        log_error(logger,
                  "## Query %d: FLUSH mal formado. Uso: FLUSH <file:tag>",
                  CONTEXT_QUERY_ID);
        free(file); free(tag);
        return -1;
    }

    log_info(logger, "## Query %d: Ejecutando FLUSH - %s:%s",
             CONTEXT_QUERY_ID, file, tag);

    // 1) Primero vaciar todos los frames dirty de ese file:tag
    if (flush_filetag(file, tag, socket_storage) != 0) {
        log_error(logger,
                  "## Query %d: FLUSH ERR (error al escribir páginas) - %s:%s",
                  CONTEXT_QUERY_ID, file, tag);
        free(file); free(tag);
        return -1;
    }

    // 2) Avisarle al Storage que el FLUSH terminó (OP_FLUSH)
    t_paquete* paquete = crear_paquete();
    paquete->codigo_operacion = OP_FLUSH;

    agregar_a_paquete(paquete, &CONTEXT_QUERY_ID, sizeof(int));
    agregar_a_paquete(paquete, file, strlen(file) + 1);
    agregar_a_paquete(paquete, tag,  strlen(tag) + 1);

    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);

    int resp = 0;
    if (recv(socket_storage, &resp, sizeof(resp), MSG_WAITALL) <= 0) {
        log_error(logger,
                  "## Query %d: FLUSH ERR (sin respuesta de Storage) - %s:%s",
                  CONTEXT_QUERY_ID, file, tag);
        free(file); free(tag);
        return -1;
    }

    if (resp != OP_RESP_OK) {
        log_error(logger,
                  "## Query %d: FLUSH ERR (Storage devolvió error) - %s:%s",
                  CONTEXT_QUERY_ID, file, tag);
        free(file); free(tag);
        return -1;
    }

    log_info(logger,
             "## Query %d: FLUSH OK - %s:%s",
             CONTEXT_QUERY_ID, file, tag);

    free(file); free(tag);
    return 0;
}


int ejecutar_Delete(Instruccion* inst, int socket_storage, t_log* logger) {

    char *file=NULL, *tag=NULL;

    if (separar_FileTag(inst->arg1, &file, &tag) != 0) {
        log_error(logger,
                  "## Query %d: DELETE mal formado. Uso: DELETE <file:tag>",
                  CONTEXT_QUERY_ID);
        free(file); free(tag);
        return -1;
    }

    log_info(logger, "## Query %d: Ejecutando DELETE - %s:%s", CONTEXT_QUERY_ID, file, tag);

    // 1. FLUSH previo
    if (flush_filetag(file, tag, socket_storage) != 0) {
        log_error(logger,
                  "## Query %d: DELETE ERR (flush previo) - %s:%s",
                  CONTEXT_QUERY_ID, file, tag);
        free(file); free(tag);
        return -1;
    }

    // 2. Liberar marcos en RAM
    for (size_t i = 0; i < FRAME_COUNT; i++) {
        if (!frames[i].libre &&
            frames[i].file && frames[i].tag &&
            strcmp(frames[i].file, file) == 0 &&
            strcmp(frames[i].tag, tag) == 0) {

            log_info(logger,
                      "Query %d: Se libera el Marco: %zu perteneciente al - File: %s - Tag: %s",
                      CONTEXT_QUERY_ID, i,
                      frames[i].file, frames[i].tag);

            frames[i].libre = true;
            free(frames[i].file); frames[i].file = NULL;
            free(frames[i].tag); frames[i].tag = NULL;
            frames[i].dirty = false;
            frames[i].U = frames[i].M = 0;
        }
    }

    // 3. Avisar a Storage
    t_paquete* paquete = crear_paquete();
    paquete->codigo_operacion = OP_DELETE;
    
    agregar_a_paquete(paquete, &CONTEXT_QUERY_ID, sizeof(int));

    agregar_a_paquete(paquete, file, strlen(file) + 1);

    agregar_a_paquete(paquete, tag, strlen(tag) + 1);

    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);

    int respuesta = 0;
    recv(socket_storage, &respuesta, sizeof(respuesta), MSG_WAITALL);

    if (respuesta == OP_RESP_OK)
        log_info(logger, "## Query %d: DELETE OK - %s:%s",
                 CONTEXT_QUERY_ID, file, tag);
    else
        log_error(logger, "## Query %d: DELETE ERR (Storage) - %s:%s",
                  CONTEXT_QUERY_ID, file, tag);

    free(file); free(tag);
    return (respuesta == OP_RESP_OK) ? 0 : -1;
}


int ejecutar_End(int id_query, int socket_master, t_log* logger) {

    log_info(logger,
             "## Query %d: END recibido. Finalizando ejecución.",
             id_query);

    // Avisar al Master que terminó la Query
    t_paquete* paquete = crear_paquete();
    paquete->codigo_operacion = OP_FIN_QUERY; // tu opcode real
    remove_path_for_qid(id_query);
    agregar_a_paquete(paquete, &id_query, sizeof(int));
    enviar_paquete(paquete, socket_master);
    eliminar_paquete(paquete);

    return 0;
}
