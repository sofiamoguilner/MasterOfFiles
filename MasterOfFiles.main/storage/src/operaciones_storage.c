#include <operaciones_storage.h>

// Retorna p = "home/utnso/storage/files/nameFILE"
char* ruta_file(char* nameFile){
    return string_from_format("%s/%s",directorio_files,nameFile);
}

//Retorna p = "home/utnso/storage/files/nameFILE/nameTAG"
char* ruta_tag(char* nameFile, char *nameTag){
    return string_from_format("%s/%s/%s",directorio_files,nameFile,nameTag);
}
// nombreBloque = 000000.dat
// Retorna p="home/storage/file/nameFile/nameTag/nameBloque"
char* ruta_blockLogico(char* file, char* tag, char* nombreBloque) {
    return string_from_format("%s/%s/%s/logical_blocks/%s", directorio_files, file, tag, nombreBloque);
}

char* direccion_metadata(char* file, char* tag) {
    char* dir_tag = ruta_tag(file, tag);
    char* res = string_from_format("%s/metadata.config", dir_tag);
    free(dir_tag);
    return res;
}


//==== CREATE ====
int operacion_create_file(char* nameFile, char* nameTag){
    //FILE TAG prexistente
    if (buscar_index_file(nameFile) != -1) {
        if (buscar_index_tag(nameFile, nameTag) != -1) {
            log_error(storage_logger,
                      "CREATE: ya existe File:Tag %s:%s", nameFile, nameTag);
            return -1;
        }
    }

    crear_file(nameFile); // ver si no se duplica
    int index = buscar_index_file(nameFile);
    if (index < 0) {
        log_error(storage_logger, "CREATE: no se pudo encontrar/crear el File %s", nameFile);
        return -1;
    }

    asigar_tag_a_file(nameFile, nameTag, index);
    return 0;
}


int buscar_index_file(char* nameFile){
    pthread_mutex_lock(&mutex_listas_archivos);

    for (int i = 0; i < list_size(lista_files); i++){
        t_file* file = list_get(lista_files,i);
        if (strcmp(file->name_File, nameFile) == 0){
            pthread_mutex_unlock(&mutex_listas_archivos);
            return i;
        }
    }

    pthread_mutex_unlock(&mutex_listas_archivos);
    return -1; // no se encontro
}

int buscar_index_tag(char* nameFile, char* nameTag) {
    int idx_file = buscar_index_file(nameFile);
    if (idx_file < 0) return -1;

    t_file* file = list_get(lista_files, idx_file);

    pthread_mutex_lock(&mutex_listas_archivos);

    for (int i = 0; i < list_size(file->tags); i++) {
        t_tag* tag = list_get(file->tags, i);
        if (strcmp(tag->name_tag, nameTag) == 0) {
            pthread_mutex_unlock(&mutex_listas_archivos);
            return i;
        }
    }

    pthread_mutex_unlock(&mutex_listas_archivos);
    return -1;
}


t_file* buscar_file(char* nameFile){
    int resultado = buscar_index_file(nameFile);
    if (resultado == -1){
        return NULL;
    }
    return list_get(lista_files,resultado);
}

t_tag* buscar_tag(char* nameFile, char* nameTag){
    int resultado = buscar_index_tag(nameFile,nameTag);
    if (resultado == -1){
        return NULL;
    }
    t_file* _file = buscar_file(nameFile);
    t_tag* _tag =list_get((*_file).tags, resultado);
    return _tag;
}
// TRUNCATE FUNCION
int operacion_truncate_file(int qid, char* nameFile, char* nameTag, int tamanio_bytes) {
    
    if (tamanio_bytes % superbloque.BLOCK_SIZE != 0) {
        log_error(storage_logger, "## TRUNCATE: tamaño %d no es múltiplo de BLOCK_SIZE=%d",
                  tamanio_bytes, superbloque.BLOCK_SIZE);
        return -1;
    }

    t_tag* _tag = buscar_tag(nameFile, nameTag);
    if (_tag == NULL) {
        log_error(storage_logger, "## TRUNCATE: File:Tag %s:%s inexistente", nameFile, nameTag);
        return 1;
    }

    if (_tag->metadata->estado == COMMITED) {
        log_error(storage_logger, "## TRUNCATE: %s:%s está COMMITED, no se puede truncar",
                  nameFile, nameTag);
        return -1;
    }

    int tamanio_actual = _tag->metadata->tamanio_file;
    int bloques_actuales = tamanio_actual / superbloque.BLOCK_SIZE;
    int bloques_nuevos = tamanio_bytes / superbloque.BLOCK_SIZE;

    log_info(storage_logger, "## TRUNCATE %s:%s de %d bytes (%d bloques) a %d bytes (%d bloques)",
             nameFile, nameTag, tamanio_actual, bloques_actuales, tamanio_bytes, bloques_nuevos);

    char* rutaTag = ruta_tag(nameFile, nameTag);
    char* ruta_block_logicos = string_from_format(DIR_LOGICAL_BLOCK, rutaTag);

    // agrandar
    if (bloques_nuevos > bloques_actuales) {
        
        int bloques_a_agregar = bloques_nuevos - bloques_actuales;
        
        for (int i = 0; i < bloques_a_agregar; i++) {
            int nro_bloque = bloques_actuales + i;
            
            char* nombre_bl = crear_nombre_bloque_logico(nro_bloque);
            char* ruta_bl = string_from_format("%s/%s", ruta_block_logicos, nombre_bl);
            int bf_libre = bitmap_encontrar_bloque_libre();
            if (bf_libre < 0) {
                log_error(storage_logger, "No hay bloques físicos libres");
                return -1;
            }

            bitmap_marcar_bloque_ocupado(bf_libre);

            char* nombre_bf = string_from_format("block%04d", bf_libre);
            char* ruta_bf = string_from_format("%s/%s.dat", directorio_blocks_fisicos, nombre_bf);

            if (link(ruta_bf, ruta_bl) != 0) {
                log_error(storage_logger, "Error creando hard link: %s", strerror(errno));
                bitmap_marcar_bloque_libre(bf_libre);

                free(nombre_bl); 
                free(ruta_bl); 
                free(ruta_bf);
                continue;
            }
            
            // LOG OBLIGATORIO Hard Link Agregado
            log_info(storage_logger, 
                     "## Query %d - %s:%s Se agregó el hard link del bloque lógico %s al bloque físico block0000",
                     qid, nameFile, nameTag, nombre_bl);
     
            // Agregar a la lista de bloques lógicos
            t_block_logico* bl_nuevo = malloc(sizeof(t_block_logico));
            bl_nuevo->nombre_block = nombre_bl;
            bl_nuevo->bloque_fisico_asociado = strdup("block0000");
            
            pthread_mutex_lock(&mutex_listas_archivos);
            list_add(_tag->lista_block_logicos, bl_nuevo);
            pthread_mutex_unlock(&mutex_listas_archivos);
            
            t_block_fisico* bf0 = list_get(lista_bloques_fisicos, 0);
            bf0->blogico_asociados++;
            
            // Actualizar metadata
            if (!esta_en_array(_tag->metadata->blocks_fisicos_usados, "0")) {
                char* s = string_itoa(0); // devuelve malloc
                string_array_push(&(_tag->metadata->blocks_fisicos_usados), s);
            }
            
            free(ruta_bl);
            free(ruta_bf);
        }
    }
    
    // achicar
    else if (bloques_nuevos < bloques_actuales) {

        int bloques_a_liberar = bloques_actuales - bloques_nuevos;

        for (int i = 0; i < bloques_a_liberar; i++) {

            pthread_mutex_lock(&mutex_listas_archivos);
            int ultimo_index = list_size(_tag->lista_block_logicos) - 1;
            t_block_logico* bl_a_borrar = list_remove(_tag->lista_block_logicos, ultimo_index);
            pthread_mutex_unlock(&mutex_listas_archivos);

            if (bl_a_borrar == NULL) continue;

            int index_bf = buscar_index_blockFisico(bl_a_borrar->bloque_fisico_asociado);
            if (index_bf == -1) {
                free(bl_a_borrar->nombre_block);
                free(bl_a_borrar->bloque_fisico_asociado);
                free(bl_a_borrar);
                continue;
            }

            t_block_fisico* bf = list_get(lista_bloques_fisicos, index_bf);

            // Eliminar hard link
            char* ruta_bl = string_from_format("%s/%s", ruta_block_logicos, bl_a_borrar->nombre_block);

            // LOG OBLIGATORIO: Hard Link Eliminado
            log_info(storage_logger,
                     "## Query %d - %s:%s Se eliminó el hard link del bloque lógico %s al bloque físico %s",
                     qid, nameFile, nameTag, bl_a_borrar->nombre_block, bl_a_borrar->bloque_fisico_asociado);

            if (unlink(ruta_bl) != 0) {
                log_error(storage_logger, "## Error eliminando hard link: %s", strerror(errno));
            }

            bf->blogico_asociados--;

            // Si nadie más referencia ese bloque físico, liberarlo
            if (bf->blogico_asociados == 0 && index_bf != 0) {
                pthread_mutex_lock(&mutex_bitmap);
                bitmap_marcar_bloque_libre(index_bf);
                pthread_mutex_unlock(&mutex_bitmap);

                // LOG OBLIGATORIO: Bloque Físico Liberado
                log_info(storage_logger,
                         "## Query %d - Bloque Físico Liberado - Número de Bloque: %d",
                         qid, index_bf);

                char* valor = string_itoa(index_bf);
                quitar_del_array_blocks(&(_tag->metadata->blocks_fisicos_usados), valor);
                free(valor);
            }

            free(ruta_bl);
            free(bl_a_borrar->nombre_block);
            free(bl_a_borrar->bloque_fisico_asociado);
            free(bl_a_borrar);
        }
    }

    // Actualizar metadata
    _tag->metadata->tamanio_file = tamanio_bytes;

    if (tamanio_bytes == 0) {
        string_array_destroy(_tag->metadata->blocks_fisicos_usados);
        _tag->metadata->blocks_fisicos_usados = string_array_new();
    }

    actualizar_metadata(rutaTag, _tag->metadata);

    free(rutaTag);
    free(ruta_block_logicos);

    return 0;
}


void copiar_archivo(char *ruta_origen, char *ruta_destino) {
    FILE *origen, *destino;
    
    origen = fopen(ruta_origen, "rb");
    if (origen == NULL) {
        perror("Error al abrir archivo de origen");
        return;
    }

    destino = fopen(ruta_destino, "wb");
    if (destino == NULL) {
        perror("Error al crear archivo de destino");
        fclose(origen);
        return;
    }
    
    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), origen)) > 0) {
        fwrite(buffer, 1, bytes, destino);
    }

    fclose(origen);
    fclose(destino);
}


//==== TAG ====

int operacion_tag_file(int qid, char* nameFileOrigen, char* nameTagOrigen, char* nameFileDestino, char* nameTagDestino) {
    
    //verificar orden
    t_tag* _tag_origen = buscar_tag(nameFileOrigen, nameTagOrigen);
    if (_tag_origen == NULL) {
        log_error(storage_logger, "## TAG: %s:%s no existe", nameFileOrigen, nameTagOrigen);
        return -1;
    }
    
   
    t_file* file_destino = buscar_file(nameFileDestino);
     // file destino si no existe
    if (buscar_file(nameFileDestino) == NULL) {
        crear_file(nameFileDestino);
        file_destino = buscar_file(nameFileDestino); //pero si lo tiene que crear q pasa?
    }

    char* ruta_tag_destino = ruta_tag(nameFileDestino, nameTagDestino);
    crear_ruta(ruta_tag_destino);
    
    
    t_tag* tag_destino = crear_tag(nameTagDestino, ruta_tag_destino);

    tag_destino->metadata->estado = WORK_IN_PROGRESS;
    tag_destino->metadata->tamanio_file = _tag_origen->metadata->tamanio_file;
    
    // copia array de bloques físicos usados
    int size = string_array_size(_tag_origen->metadata->blocks_fisicos_usados);
    for (int i = 0; i < size; i++) {
        string_array_push(&(tag_destino->metadata->blocks_fisicos_usados),
                         _tag_origen->metadata->blocks_fisicos_usados[i]);
    }
    
    // copia bloques logicos
    for (int i = 0; i < list_size(_tag_origen->lista_block_logicos); i++) {
        t_block_logico* bl_origen = list_get(_tag_origen->lista_block_logicos, i);
        
        t_block_logico* bl_destino = malloc(sizeof(t_block_logico));
        bl_destino->nombre_block = strdup(bl_origen->nombre_block);
        bl_destino->bloque_fisico_asociado = strdup(bl_origen->bloque_fisico_asociado);
        
        // crea hard link
        if (linkear_bl_a_bf(nameFileDestino, nameTagDestino,
                            bl_destino->nombre_block,
                            bl_destino->bloque_fisico_asociado) != 0) {
            log_error(storage_logger,
                      "## TAG: error linkeando BL %s -> BF %s",
                      bl_destino->nombre_block,
                      bl_destino->bloque_fisico_asociado);
            
            free(bl_destino->nombre_block);
            free(bl_destino->bloque_fisico_asociado);
            free(bl_destino);
            continue;
        }

        int index_bf = buscar_index_blockFisico(bl_destino->bloque_fisico_asociado);
        if (index_bf < 0) {
            log_error(storage_logger,
                      "## TAG: no se encontró BF %s en lista interna",
                      bl_destino->bloque_fisico_asociado);
            
        } else {
            t_block_fisico* bf = list_get(lista_bloques_fisicos, index_bf);
            bf->blogico_asociados++;
        }
        list_add(tag_destino->lista_block_logicos, bl_destino);

    }

    // Persistir metadata destino a metadata.config
    actualizar_metadata(ruta_tag_destino, tag_destino->metadata);

    //Agregar el tag a la lista de tags del file destino
    pthread_mutex_lock(&mutex_listas_archivos);
    list_add(file_destino->tags, tag_destino);
    pthread_mutex_unlock(&mutex_listas_archivos);


    free(ruta_tag_destino);
    return 0;
}


//=== COMMIT ===
/*int operacion_commit_tag(char* nameFile , char* nameTag){
    t_tag* _tag =buscar_tag(nameFile,nameTag);
    if (_tag == NULL){
        log_info(storage_logger,"## FILE:TAG inexistente");
        return -1;
    }

    if(_tag->metadata->estado == COMMITED){
        log_info(storage_logger,"## FILE:TAG ya estaba en estado COMMITED");
        return 1;
    }

    tag.metadata->estado = COMMITED;

    for (int i = 0; i < list_size((*_tag).lista_block_logicos); i++)
    {
        t_block_logico * _bl=list_get((*_tag).lista_block_logicos,i);

        char* temp_ruta_bl = ruta_blockLogico(nameFile,nameTag,(*_bl).nombre_block);
        char* contenido_bl = get_contenido(temp_ruta_bl); 
        char* hash_bl= crypto_md5(contenido_bl,strlen(contenido_bl)+1);

        if(!config_has_property(hash_index_config, hash_bl)){
            log_info(storage_logger,"## No se encuentra el hash en el index");
           
            // Agrega el hash 
            char* aux_agregar = string_from_format("%s=%s",hash_bl,(*_bl).bloque_fisico_asociado);
            escribir_archivo(ruta_hash_index,aux_agregar);
            return -2;
        }

        t_list* temp_lista_hash = dictionary_keys(hash_index_config->properties);
        t_list* temp_lista_value = dictionary_elements(hash_index_config->properties);
        int temp_contador_repeticion=0;
        int temp_index=0; // primer bloque encontrado
        for (int i= 0; i < list_size(temp_lista_hash); i++)
        {
            char* temp_hash = list_get(temp_lista_hash,i);
           if (temp_hash == hash_bl)
           {
                temp_contador_repeticion++;
           }
           if (temp_contador_repeticion ==1)
           {
                temp_index = i; // guardo el primer bloque identico
           }
           
           
        }
        if (temp_contador_repeticion > 1 )
        {
            char* temp_nameBF_reasignado= list_get(temp_lista_value,temp_index);
            if (strcmp(temp_nameBF_reasignado , (*_bl).bloque_fisico_asociado) != 0 )
            {
                // es distinto los bloque fisicos
                // reasignamos al primer bloque encontrado
                
                unlink(temp_ruta_bl);
                
                //char* temp_nameBF= string_array_pop(temp_index);
                char* temp_ruta_bf= string_from_format("%s/%s.dat",directorio_blocks_fisicos,temp_nameBF_reasignado);
                link(temp_ruta_bf,temp_ruta_bl);

                int temp_index_viejo = buscar_index_blockFisico((*_bl).bloque_fisico_asociado);
                int temp_index_nuevo=buscar_index_blockFisico(temp_nameBF_reasignado);
                
                t_block_fisico* temp_bf_usado= list_get(lista_bloques_fisicos,temp_index_viejo);
                (*temp_bf_usado).blogico_asociados--;

                t_block_fisico* temp_bf_reasignado = list_get(lista_bloques_fisicos,temp_index_nuevo);
                (*temp_bf_reasignado).blogico_asociados++;

                log_info(storage_logger, "##<QUERY_ID> - <%s>:<%s> Bloque Lógico <%s> se reasigna de <%s> a <%s>", nameFile, nameTag, _bl->nombre_block,
                    _bl->bloque_fisico_asociado,
                    temp_nameBF_reasignado);
                    
                //Eliminar el hash ?
                _bl->bloque_fisico_asociado = strdup(temp_nameBF_reasignado);

            }
            

        }
        

    }
    
    return 0;
}*/
int operacion_commit_tag(char* nameFile, char* nameTag) {

    t_tag* tag = buscar_tag(nameFile, nameTag);
    if (!tag) {
        log_error(storage_logger, "## %s:%s no existe", nameFile, nameTag);
        return -1;
    }

    if (tag->metadata->estado == COMMITED)
        return 1;

    tag->metadata->estado = COMMITED;

    // Procesar cada bloque lógico
    for (int i = 0; i < list_size(tag->lista_block_logicos); i++) {
        t_block_logico* bl = list_get(tag->lista_block_logicos, i);
        procesar_bloque_commit(nameFile, nameTag, bl);
    }

    // Actualizar metadata
    char* ruta = ruta_tag(nameFile, nameTag);
    actualizar_metadata(ruta, tag->metadata);
    free(ruta);

    return 0;

}

char* obtener_hash_bloque(char* ruta_bl) {
    char* contenido = get_contenido(ruta_bl);
    char* hash = crypto_md5(contenido, strlen(contenido) + 1);
    free(contenido);
    return hash;
}

void agregar_hash_nuevo(char* hash, char* bf_name) {
    config_set_value(hash_index_config, hash, bf_name);
    config_save(hash_index_config);
}

void reasignar_bloque_logico(
        char* ruta_bl, 
        t_block_logico* bl, 
        char* bf_nuevo, 
        char* bf_viejo) 
{
    // unlink BL → BF viejo
    unlink(ruta_bl);

    char* ruta_bf_nuevo = string_from_format("%s/%s.dat", directorio_blocks_fisicos, bf_nuevo);
    link(ruta_bf_nuevo, ruta_bl);
    free(ruta_bf_nuevo);

    // actualizar contador del viejo
    int idx_viejo = buscar_index_blockFisico(bf_viejo);
    if (idx_viejo >= 0) {
        t_block_fisico* bf_old = list_get(lista_bloques_fisicos, idx_viejo);
        bf_old->blogico_asociados--;

        if (bf_old->blogico_asociados == 0 && idx_viejo != 0)
            bitmap_marcar_bloque_libre(idx_viejo);
    }

    // actualizar contador del nuevo
    int idx_nuevo = buscar_index_blockFisico(bf_nuevo);
    if (idx_nuevo >= 0) {
        t_block_fisico* bf_new = list_get(lista_bloques_fisicos, idx_nuevo);
        bf_new->blogico_asociados++;
    }

    // actualizar en estructura del bloque lógico
    free(bl->bloque_fisico_asociado);
    bl->bloque_fisico_asociado = strdup(bf_nuevo);
}

void procesar_bloque_commit(char* nameFile, char* nameTag, t_block_logico* bl) {

    char* ruta_bl = ruta_blockLogico(nameFile, nameTag, bl->nombre_block);

    char* hash = obtener_hash_bloque(ruta_bl);

    // Caso 1: hash NO existe → crear entrada en hash_index
    if (!config_has_property(hash_index_config, hash)) {
        agregar_hash_nuevo(hash, bl->bloque_fisico_asociado);
        free(ruta_bl);
        free(hash);
        return;
    }

    // Caso 2: hash ya existe → deduplicación
    char* bf_existente = config_get_string_value(hash_index_config, hash);

    if (strcmp(bf_existente, bl->bloque_fisico_asociado) != 0) {
        reasignar_bloque_logico(
            ruta_bl,
            bl,
            bf_existente,
            bl->bloque_fisico_asociado
        );
    }

    free(ruta_bl);
    free(hash);
}

int linkear_bl_a_bf(char* nameFile,char* nameTag, char* nameBL,char* nameBF){
    char* _rutaBloqueLogico = ruta_blockLogico(nameFile,nameTag,nameBL);
    char* _rutaBloqueFisico = string_from_format("%s/%s.dat",directorio_blocks_fisicos,nameBF);
  
    if (link(_rutaBloqueFisico, _rutaBloqueLogico) != 0) {
        if (errno == EEXIST) {
            log_debug(storage_log_debug,
                    "Hard link ya existente (%s -> %s), se reutiliza",
                    nameBL, nameBF);
            return 0; // NO es error lógico
        }

        log_error(storage_logger,
                "Error creando hard link %s -> %s: %s",
                nameBL, nameBF, strerror(errno));
        return -1;
    }

    log_debug(storage_log_debug,"Enlace creado exitosamente");
    
    return 0;
}

int buscar_index_blockFisico(char* nameBloqueFisico) {
    int cant = list_size(lista_bloques_fisicos);   // o cant_block si está bien mantenido

    for (int i = 0; i < cant; i++) {
        t_block_fisico* bf = list_get(lista_bloques_fisicos, i);
        if (bf != NULL && bf->path_block != NULL &&
            strcmp(bf->path_block, nameBloqueFisico) == 0) {
            return i;  // encontrado
        }
    }
    return -1; // no se encontró
}

t_block_fisico* buscar_bf_asociado(char* hash_bock_logico){
    //t_block_fisico* _bFisico = malloc(sizeof(t_block_fisico));

    if(config_has_property(hash_index_config,hash_bock_logico) == false){
        return NULL; // Error 
        //Deberia agregar hash en el config?
    }
    char* nameBFisico = config_get_string_value(hash_index_config,hash_bock_logico);
    

    int resultado =buscar_index_blockFisico(nameBFisico);
    if(resultado == -1){
        free(nameBFisico);
        return NULL;
    }
    return list_get(lista_bloques_fisicos,resultado);
}

char* _nameBloque(int nroBloque){
    char* digitos = string_new();
    for (int i = 0; i < cant_block; i++)
    {
        if (i==nroBloque)
        {
            if (i<10) // de 0 a 9
            {
                digitos=string_from_format("block000%d",i);
            }
            else if(i>=10 && i<100) // de 10 a 99
            {
                digitos=string_from_format("block00%d",i);
            }
            else if(i>=100 && i <1000) // de 100 a 999
            {
                digitos=string_from_format("block0%d",i);
            }
            else // de 1000 a 9999 o más
            {
                    digitos=string_from_format("block%d",i);
            }
        }
        
    }

    return digitos;
} 
// ordenar de menor a mayor

// Función de comparación requerida por qsort()
// Debe tomar dos punteros const void* y devolver un int.
int comparar_enteros(const void *a, const void *b) {
    // Convertir los punteros void* a punteros int* y desreferenciarlos
    int int_a = *((int *)a);
    int int_b = *((int *)b);

    // Para ordenar de menor a mayor (ascendente):
    if (int_a < int_b) return -1;
    if (int_a > int_b) return 1;
    return 0;
    
    // Una forma más concisa de hacer lo mismo:
    // return (int_a - int_b); 
}

void agregar_bloque_usado(char*** array_ptr, int nuevo_bloque)
{
    char** array = *array_ptr;
    int size = string_array_size(array);

    int* bloques = malloc((size + 1) * sizeof(int));

    for (int i = 0; i < size; i++)
        bloques[i] = atoi(array[i]);

    bloques[size] = nuevo_bloque;

    qsort(bloques, size + 1, sizeof(int), comparar_enteros);

    // destruís el array original COMPLETO
    string_array_destroy(array);

    // array nuevo
    char** nuevo = string_array_new();

    for (int i = 0; i < size + 1; i++)
        string_array_push(&nuevo, string_itoa(bloques[i]));

    free(bloques);

    *array_ptr = nuevo;   // <-- se actualiza correctamente
}

//Dada un t_metadata* actualizo el .config , el archivo
int actualizar_metadata(char* ruta_tag, t_metadata* _metadata)
{
    char* ruta_metadata = string_from_format(DIR_METADATA, ruta_tag);
    t_config* meta_config = config_create(ruta_metadata);
    if (!meta_config)
        return -1;

    // Construir "[a,b,c]"
    char* bloques = string_new();
    string_append(&bloques, "[");

    int size = string_array_size(_metadata->blocks_fisicos_usados);

    for (int i = 0; i < size; i++) {
        string_append(&bloques, _metadata->blocks_fisicos_usados[i]);
        if (i < size - 1)
            string_append(&bloques, ",");

    }
    
    string_append(&bloques, "]");

    config_set_value(meta_config, "TAMAÑO",
                     string_itoa(_metadata->tamanio_file));
    config_set_value(meta_config, "BLOCKS", bloques);
    config_set_value(meta_config, "ESTADO",
                     convertir_estado_a_string(_metadata->estado));
    config_save(meta_config);

    free(bloques);

    config_destroy(meta_config);

    free(ruta_metadata);

    return 0;
}

//=== WRITE====
// falta verificar si actualiza las listas de las estructuras
int operacion_write_bloque(
    int qid,
    char* nameFile,
    char* nameTag,
    t_block_logico* _bl,
    char* contenido,
    size_t len
) {
    int resultado = 0;

    char* dir_bloque_logico = NULL;
    char* contenido_original = NULL;
    char* hash_bl = NULL;
    char* dir_bloque_fisico = NULL;

    pthread_mutex_lock(&mutex_bloques);

    // ─────────────────────────────────────────────────────────────
    // Validar TAG
    t_tag* tag = buscar_tag(nameFile, nameTag);
    if (!tag) {
        log_error(storage_logger,
                  "## <%s>:<%s> WRITE_BLOCK: Tag inexistente",
                  nameFile, nameTag);
        resultado = -1;
        goto cleanup;
    }

    /*if (tag->metadata->estado == COMMITED) {
        log_error(storage_logger,
                  "## <%s>:<%s> WRITE_BLOCK: Escritura prohibida (COMMITED)",
                  nameFile, nameTag);
        resultado = -1;
        goto cleanup;
    }*/

    // ─────────────────────────────────────────────────────────────
    // Obtener contenido actual del bloque lógico
    dir_bloque_logico = ruta_blockLogico(nameFile, nameTag, _bl->nombre_block);
    contenido_original = get_contenido(dir_bloque_logico);

    if (!contenido_original) {
        log_error(storage_logger,
                  "WRITE_BLOCK: contenido_original NULL %s:%s bloque %s",
                  nameFile, nameTag, _bl->nombre_block);
        resultado = -1;
        goto cleanup;
    }

    //size_t len_nuevo    = strlen(contenido);

    /* ⛔ VALIDACIÓN REAL DE ESCRITURA */
    if (len > superbloque.BLOCK_SIZE) {
        log_error(storage_logger,
            "WRITE_BLOCK: contenido excede tamaño de bloque (%zu > %d) %s:%s",
            len,
            superbloque.BLOCK_SIZE,
            nameFile,
            nameTag
        );

        free(dir_bloque_logico);
        free(contenido_original);
        free(hash_bl);

        pthread_mutex_unlock(&mutex_bloques);
        return -1;
    }

    // Hash original
    hash_bl = crypto_md5(contenido_original, strlen(contenido_original) + 1);

    // ─────────────────────────────────────────────────────────────
    // Buscar bloque físico asociado
    int index_bf = buscar_index_blockFisico(_bl->bloque_fisico_asociado);
    if (index_bf < 0) {
        log_error(storage_logger,
                  "## <%s>:<%s> Bloque físico inválido: %s",
                  nameFile, nameTag, _bl->bloque_fisico_asociado);
        resultado = -1;
        goto cleanup;
    }

    t_block_fisico* bf = list_get(lista_bloques_fisicos, index_bf);
    dir_bloque_fisico = string_from_format("%s/%s.dat",
                                           directorio_blocks_fisicos,
                                           bf->path_block);

    // ─────────────────────────────────────────────────────────────
    // CASO 1: bloque físico NO compartido
    //if (bf->blogico_asociados == 1) {
        char* buffer = NULL;
        buffer = malloc(superbloque.BLOCK_SIZE);
        if (!buffer) {
            resultado = -1;
            goto cleanup;
        }

        memset(buffer, 0, superbloque.BLOCK_SIZE);

        // Copio contenido previo del bloque
        size_t len_original = strlen(contenido_original);
        if (len_original > superbloque.BLOCK_SIZE)
            len_original = superbloque.BLOCK_SIZE;

        memcpy(buffer, contenido_original, len_original);

        // Piso con el nuevo contenido
        memcpy(buffer, contenido, len);

        if (escribir_archivo_truncado(
                dir_bloque_fisico,
                buffer,
                superbloque.BLOCK_SIZE) != 0) {

            resultado = -1;
            goto cleanup;
        }

        // Actualizar contenido en memoria
        free(bf->contenido);
        bf->contenido = get_contenido(dir_bloque_fisico);

        // Actualizar hash index
        config_remove_key(hash_index_config, hash_bl);

        char* hash_nuevo = crypto_md5(
            bf->contenido,
            strlen(bf->contenido) + 1);

        char* linea = string_from_format(
            "%s=%s",
            hash_nuevo,
            bf->path_block);

        escribir_archivo(ruta_hash_index, linea);
        config_save(hash_index_config);

        free(hash_nuevo);
        free(linea);
    /*}

    // ─────────────────────────────────────────────────────────────
    // CASO 2: bloque físico compartido
    else if (bf->blogico_asociados > 1) {

        resultado = reasignar_bloque_fisico_compartido(
            qid,
            nameFile,
            nameTag,
            tag,
            _bl,
            bf,
            contenido_original,
            contenido
        );

        if (resultado != 0)
            goto cleanup;
    }*/

cleanup:
    free(buffer);
    free(dir_bloque_logico);
    free(contenido_original);
    free(hash_bl);
    free(dir_bloque_fisico);

    pthread_mutex_unlock(&mutex_bloques);
    return resultado;
}



//delegue el else if en operacion_write_block
int reasignar_bloque_fisico_compartido(
    int qid,
    char* nameFile,
    char* nameTag,
    t_tag* tag,
    t_block_logico* bl,
    t_block_fisico* bf_viejo,
    char* contenido_original,
    char* contenido_nuevo
) {
    int pos = bitmap_encontrar_bloque_libre();
    if (pos < 0) {
        log_error(storage_logger,
                  "## <%s>:<%s> No hay bloques físicos libres para reasignar",
                  nameFile, nameTag);
        return -1;
    }

    char* nombre_bf_nuevo = _nameBloque(pos);
    char* ruta_bf_nuevo = string_from_format(
        "%s/%s.dat",
        directorio_blocks_fisicos,
        nombre_bf_nuevo
    );

    /* ===============================
    * 1️⃣ Escribir nuevo bloque físico
    * =============================== */

    char buffer[superbloque.BLOCK_SIZE];
    memset(buffer, 0, superbloque.BLOCK_SIZE);

    // Copiar contenido original
    size_t len_original = strlen(contenido_original);
    if (len_original > superbloque.BLOCK_SIZE)
        len_original = superbloque.BLOCK_SIZE;

    memcpy(buffer, contenido_original, len_original);

    // Pisar con el nuevo contenido
    size_t len_nuevo = strlen(contenido_nuevo);
    if (len_nuevo > superbloque.BLOCK_SIZE)
        len_nuevo = superbloque.BLOCK_SIZE;

    memcpy(buffer, contenido_nuevo, len_nuevo);

    // Escribir UNA SOLA VEZ
    if (escribir_archivo_truncado(
            ruta_bf_nuevo,
            buffer,
            superbloque.BLOCK_SIZE) != 0) {

        log_error(storage_logger,
                "Error escribiendo bloque físico reasignado %s",
                nombre_bf_nuevo);

        free(nombre_bf_nuevo);
        free(ruta_bf_nuevo);
        return -1;
    }

    /* ===============================
     * 2️⃣ Crear hard link NUEVO
     * =============================== */
    if (linkear_bl_a_bf(nameFile, nameTag, bl->nombre_block, nombre_bf_nuevo) != 0) {

        log_error(storage_logger,
                  "Error creando hard link %s:%s/%s -> %s",
                  nameFile, nameTag, bl->nombre_block, nombre_bf_nuevo);

        free(nombre_bf_nuevo);
        free(ruta_bf_nuevo);
        return -1;
    }

    /* ===============================
     * 3️⃣ Reservar bloque físico (bitmap)
     * =============================== */
    bitmap_marcar_bloque_ocupado(pos);

    log_info(storage_logger,
             "## Query %d - Bloque Físico Reservado - Número de Bloque: %d",
             qid, pos);

    /* ===============================
     * 4️⃣ Eliminar hard link viejo
     * =============================== */
    char* ruta_bl_viejo = ruta_blockLogico(nameFile, nameTag, bl->nombre_block);

    unlink(ruta_bl_viejo);

    log_info(storage_logger,
             "## Query %d - %s:%s Se eliminó el hard link del bloque lógico %s al bloque físico %s",
             qid, nameFile, nameTag,
             bl->nombre_block, bf_viejo->path_block);

    free(ruta_bl_viejo);

    if (bf_viejo->blogico_asociados > 0)
        bf_viejo->blogico_asociados--;

    /* ===============================
     * 5️⃣ Log obligatorio: hard link agregado
     * =============================== */
    log_info(storage_logger,
             "## Query %d - %s:%s Se agregó el hard link del bloque lógico %s al bloque físico %s",
             qid, nameFile, nameTag,
             bl->nombre_block, nombre_bf_nuevo);

    /* ===============================
     * 6️⃣ Actualizar estructuras internas
     * =============================== */
    t_block_fisico* bf_nuevo = list_get(lista_bloques_fisicos, pos);
    bf_nuevo->path_block = strdup(nombre_bf_nuevo);
    bf_nuevo->contenido = get_contenido(ruta_bf_nuevo);
    bf_nuevo->blogico_asociados = 1;

    free(bl->bloque_fisico_asociado);
    bl->bloque_fisico_asociado = strdup(nombre_bf_nuevo);

    agregar_bloque_usado(&tag->metadata->blocks_fisicos_usados, pos);
    actualizar_metadata(ruta_tag(nameFile, nameTag), tag->metadata);

    /* ===============================
     * 7️⃣ Hash index
     * =============================== */
    char* hash = crypto_md5(bf_nuevo->contenido,
                            strlen(bf_nuevo->contenido) + 1);
    char* linea = string_from_format("%s=%s", hash, nombre_bf_nuevo);
    escribir_archivo(ruta_hash_index, linea);

    free(hash);
    free(linea);
    free(nombre_bf_nuevo);
    free(ruta_bf_nuevo);

    return 0;
}


// ==== READ ====
char* operacion_read_bloque(char* nameFile,char*nameTag,char* nroBlockLogico){

    pthread_mutex_lock(&mutex_bloques);
    
    char* contenido=string_new();
    char* path=string_from_format("%s/%s/%s/logical_blocks/%s",directorio_files,nameFile,nameTag,nroBlockLogico);
   // seccion critica porque abre un archivo
    contenido= get_contenido(path);
    
    pthread_mutex_unlock(&mutex_bloques);
     
    // fin de seccion critica 
    free(path);
    return contenido;
}
/*
    Busca en la lista segun el nombre
    Devuelve 
         NRO de bloques asociados a este bfisico
         0 error al encontrar bloque fisico
*/
int blocks_fisicas_asociados(char* nameBF){
    for (int i = 0; i < list_size(lista_bloques_fisicos); i++)
    {
        t_block_fisico* _bf=list_get(lista_bloques_fisicos,i);
        if ((*_bf).path_block == nameBF)
        {
            
            return (*_bf).blogico_asociados;
        }
        
    }
    
    return 0;
}


//===== ELIMINAR ======
/*
    Esta operación eliminará el directorio correspondiente al File:Tag 
    indicado. Al realizar esta operación, si el bloque físico al que apunta 
    cada bloque lógico eliminado no es referenciado por ningún otro File:Tag
    ,deberá ser marcado como libre en el bitmap.
*/
void operacion_eliminar_tag(int qid, char* nameFile, char* nameTag)
{
    char* temp_ruta_tag = ruta_tag(nameFile, nameTag);
    t_file* _file = buscar_file(nameFile);
    t_tag* _tag = buscar_tag(nameFile, nameTag);

    if (!_tag) {
        log_error(storage_logger, "DELETE: Tag %s:%s no existe", nameFile, nameTag);
        free(temp_ruta_tag);
        return;
    }

    // ✅ Primero loguear todos los hard links que se van a eliminar
    for (int i = 0; i < list_size(_tag->lista_block_logicos); i++) {
        t_block_logico* bl = list_get(_tag->lista_block_logicos, i);
        
        log_info(storage_logger,
                 "## Query %d - %s:%s Se eliminó el hard link del bloque lógico %s al bloque físico %s",
                 qid, nameFile, nameTag, bl->nombre_block, bl->bloque_fisico_asociado);
    }

    // ✅ WHILE en lugar de FOR - va sacando hasta vaciar
    while (string_array_size(_tag->metadata->blocks_fisicos_usados) > 0)
    {
        char* temp_nro_bf = string_array_pop(_tag->metadata->blocks_fisicos_usados);
        int nro_bf = atoi(temp_nro_bf);
        
        char* nameBF = _nameBloque(nro_bf);
        int index_bf = buscar_index_blockFisico(nameBF);
        
        if (index_bf >= 0) {
            t_block_fisico* _bf = list_get(lista_bloques_fisicos, index_bf);
            
            _bf->blogico_asociados--;
            
            // Si nadie más lo usa Y no es el bloque 0, liberar
            if (_bf->blogico_asociados == 0 && index_bf != 0) {
                
                bitmap_marcar_bloque_libre((uint32_t)nro_bf);
                
                log_info(storage_logger,
                         "## Query %d - Bloque Físico Liberado - Número de Bloque: %d",
                         qid, nro_bf);
                
                // Limpiar el bloque físico
                char* ruta_bf = string_from_format("%s/%s.dat", 
                                                   directorio_blocks_fisicos, nameBF);
                FILE* archivo_bf = fopen(ruta_bf, "w");
                if (archivo_bf) {
                    fclose(archivo_bf);
                    truncate(ruta_bf, superbloque.BLOCK_SIZE);
                }
                free(ruta_bf);
            }
        }
        
        free(temp_nro_bf);
        free(nameBF);
    }

    // Eliminar el directorio físico
    eliminar_carpeta_con_contido(temp_ruta_tag);
    
    // Remover de la lista del file
    int idx_tag = buscar_index_tag(nameFile, nameTag);
    if (idx_tag >= 0) {
        list_remove(_file->tags, idx_tag);
    }
    
    free(temp_ruta_tag);
}

//funciones auxiliares
//crea nombre de bloque logico con formato 00000.dat
char* crear_nombre_bloque_logico(int numero) {
    char* nombre;
    if (numero < 10) {
        nombre = string_from_format("00000%d.dat", numero);
    } else if (numero < 100) {
        nombre = string_from_format("0000%d.dat", numero);
    } else if (numero < 1000) {
        nombre = string_from_format("000%d.dat", numero);
    } else if (numero < 10000) {
        nombre = string_from_format("00%d.dat", numero);
    } else if (numero < 100000) {
        nombre = string_from_format("0%d.dat", numero);
    } else {
        nombre = string_from_format("%d.dat", numero);
    }
    return nombre;
}

bool esta_en_array(char** array, char* valor) {
    if (array == NULL) return false;
    
    for (int i = 0; array[i] != NULL; i++) {
        if (strcmp(array[i], valor) == 0) {
            return true;
        }
    }
    return false;
}

// Quita un valor del array de blocks
void quitar_del_array_blocks(char*** array_ptr, char* valor) {
    char** array = *array_ptr;
    if (array == NULL) return;

    int size = string_array_size(array);
    char** nuevo_array = string_array_new();
    
    for (int i = 0; i < size; i++) {
        if (strcmp(array[i], valor) != 0) {
            // duplico el string para el nuevo array
            string_array_push(&nuevo_array, string_duplicate(array[i]));
        }
    }

    // destruyo el viejo (libera sus strings)
    string_array_destroy(array);
    *array_ptr = nuevo_array;
}



