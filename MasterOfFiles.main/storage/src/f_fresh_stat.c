#include <f_fresh_stat.h>
//#include "sincronizacion.h"

//  FRESH STRAT : TRUE OR FALSE 

int crear_ruta( char* ruta ) {
    
    int estado = mkdir(ruta,S_IRWXU); // permite Read/write/exec por el usuario
    if(estado ==0){
        //log_debug(storage_log_debug,"Directorio '%s' creado exitosamente.", ruta);
        return 0;
    }
    else{

        if (errno == EEXIST) {
            log_debug(storage_log_debug,"El directorio '%s' ya existe.", ruta);
            return 1; // existe
        } else {
            log_error(storage_log_debug,"Error al crear el directorio '%s': %s", ruta, strerror(errno));
            return -1; // hubo un error
        }
    }
    
    // se creo exitosamente la ruta
}

void crear_archivo(char* ruta){
    FILE * _archivo = txt_open_for_append(ruta);
    txt_close_file(_archivo);
}

long obtener_tamano_archivo(char *nombre_archivo) {
   
    struct stat st;
    // La función stat devuelve 0 si tiene éxito, -1 si falla
    if (stat(nombre_archivo, &st) == 0) {
        return (long)st.st_size; // st_size contiene el tamaño del archivo
    } else {
        perror("Error al obtener el estado del archivo");
        return -1;
    }

    //return tamano;
}
//Crear si no exite, si existe no pasa nada
int crear_archivo_size(char* ruta, int tamanio_bytes){

    FILE * _archivo = txt_open_for_append(ruta); // existente sino lo crea
    int fildes = fileno(_archivo); 

    if(ftruncate(fildes,tamanio_bytes)==-1){
        log_error(storage_log_debug,"Error truncate file : %s",ruta);
        txt_close_file(_archivo);
        return -1; // error
    }
    //log_debug(storage_log_debug,"Se creo < %s > - size : %d bytes",ruta,tamanio_bytes);
    txt_close_file(_archivo);
    return 0; // ftruncate exitoso
}

void escribir_archivo(char* ruta, char* texto){

    FILE * _archivo = txt_open_for_append(ruta);
    txt_write_in_file(_archivo,texto); // funcion de commons txt.h
    txt_close_file(_archivo);
}

int eliminar_carpeta_con_contido(char* ruta) {
    // Ruta del directorio (incluso si no está vacío)
    const char *nombreDirectorio = ruta;
    // Construir el comando completo
    char comando[256]; // Buffer para el comando
    snprintf(comando, sizeof(comando), "rm -r %s", nombreDirectorio);

    // Ejecutar el comando
    int resultado = system(comando);

    // Verificar el resultado
    if (resultado != 0) {
        log_error(storage_log_debug, "Error al ejecutar el comando: %s", comando);
        return 1; // error
    } else {
        log_trace(storage_log_debug,"Directorio '%s' y su contenido eliminados con éxito.", nombreDirectorio);
        return 0;
    }
    
}


int eliminar_archivo(char* ruta) {

    // 1. ¿Existe?
    if (access(ruta, F_OK) != 0) {
        // No existe → no es error, pero avisamos
        log_debug(storage_log_debug, "Archivo '%s' no existe, nada para eliminar.", ruta);
        return 0;
    }

    // 2. Intentamos borrar
    if (remove(ruta) != 0) {
        // 3. Error → mostramos causa real
        log_error(storage_log_debug, "No se pudo eliminar archivo '%s': %s", 
                  ruta, strerror(errno));
        return -1;
    }

    // 4. Éxito
    log_debug(storage_log_debug, "Archivo '%s' eliminado correctamente.", ruta);
    return 0;
}


void leer_config_superblock(char* ruta){
    t_config* superblock_config = config_create(ruta);

    superbloque.FS_SIZE=config_get_int_value(superblock_config,"FS_SIZE");
    superbloque.BLOCK_SIZE=config_get_int_value(superblock_config,"BLOCK_SIZE");
    
    config_destroy(superblock_config);

}


// siempre al principio , para tener algo que eliminar en fresh_start: true
void montar_desde_cero(){
    //crear las rutas de directorio , si ya existen devuelve 1
    crear_ruta(directorio_raiz);
    crear_ruta(directorio_blocks_fisicos);
    crear_ruta(directorio_files);

    // existe archivo en la carpeta del programa
    if(access("./superblock.config",F_OK)==0){
        crear_archivo(ruta_superbloque);
        char* contenido = get_contenido("./superblock.config");
        log_info(storage_logger,"superblock.config = %s",contenido);
        escribir_archivo(ruta_superbloque,contenido);
    }
    else if (access(ruta_superbloque,F_OK)!=0 )
    {   // creo archivo .config
        crear_archivo(ruta_superbloque);
        log_info(storage_logger,"superblock.config = %s",SUPERBLOQUE_CFG);
        escribir_archivo(ruta_superbloque,SUPERBLOQUE_CFG);
        // guardo config en var storage_config
        
    }
}

int crear_bitmap_vacio(char* pathBitmap, uint32_t cant_block) {
    bitmap = malloc(sizeof(t_bitmap));

    // cantidad de bytes necesarios: ceil(cant_block / 8)
    bitmap->tamanio = (cant_block + 7) / 8;

    // abrir archivo truncado a 0 y volverlo del tamaño correcto
    int fd = open(pathBitmap, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        log_error(storage_log_debug, "Error al crear el archivo de Bitmap vacío");
        return -1;
    }

    if (ftruncate(fd, bitmap->tamanio) == -1) {
        log_error(storage_log_debug, "Error al truncar el archivo de Bitmap vacío");
        close(fd);
        return -1;
    }

    bitmap->direccion = mmap(NULL, bitmap->tamanio, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bitmap->direccion == MAP_FAILED) {
        log_error(storage_log_debug, "Error al mapear el Bitmap vacío");
        close(fd);
        return -1;
    }

    // crear estructura bitarray
    bitmap->bitarray = bitarray_create_with_mode(bitmap->direccion, bitmap->tamanio, LSB_FIRST);

    // poner todos los bits en 0 (bloques libres)
    memset(bitmap->direccion, 0, bitmap->tamanio);

    // sincronizar los cambios
    msync(bitmap->direccion, bitmap->tamanio, MS_SYNC);

    close(fd);

    log_debug(storage_log_debug, "Bitmap vacío creado correctamente (%u bloques).", cant_block);
    return 0;
}

int abrir_bitmap(char* pathBitmap, uint32_t cant_block) {
    bitmap = malloc(sizeof(t_bitmap));

    // cantidad de bytes = ceil(cant_block / 8)
    bitmap->tamanio = (cant_block + 7) / 8;

    uint32_t fileDescriptor = open(pathBitmap, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fileDescriptor == -1) {
        log_error(storage_log_debug, "Error al abrir el archivo Bitmap");
        return -1;
    }

    if (ftruncate(fileDescriptor, bitmap->tamanio) == -1) {
        log_error(storage_log_debug, "Error al truncar el archivo Bitmap");
        close(fileDescriptor);
        return -1;
    }

    bitmap->direccion = mmap(NULL, bitmap->tamanio, PROT_READ | PROT_WRITE, MAP_SHARED, fileDescriptor, 0);
    if (bitmap->direccion == MAP_FAILED) {
        log_error(storage_logger, "Error al mapear el Bitmap");
        close(fileDescriptor);
        return -1;
    }

    bitmap->bitarray = bitarray_create_with_mode(bitmap->direccion, bitmap->tamanio, LSB_FIRST);

    close(fileDescriptor);
    return 0;
}

void destruir_bitmap()
{
    munmap(bitmap->direccion,bitmap->tamanio);
    bitarray_destroy(bitmap->bitarray);
}

int escribir_archivo_truncado(char* path_archivo, void* buffer, int size_block)
{
    FILE* archivo = fopen(path_archivo, "wb");
    if (!archivo) {
        perror("Error al abrir bloque físico");
        return -1;
    }

    size_t escritos = fwrite(buffer, 1, size_block, archivo);
    fclose(archivo);

    if (escritos != size_block) {
        log_error(storage_logger,
                  "Error escribiendo bloque %s (escritos=%zu, limite bloque=%d)",
                  path_archivo, escritos, size_block);
        return -1;
    }

    return 0;
}


//llena todo el archivo con 0 o otro caracter desde el inicio
int llenar_archivo_truncado(char* path_archivo,char caracter_relleno,int size_block){

    pthread_mutex_lock(&mutex_bloques);

    FILE *archivo = fopen(path_archivo, "w+");
    if (archivo == NULL) {
        perror("Error al abrir el archivo");
        return 1;
    }
    int fd = fileno(archivo);
    if (ftruncate(fd, size_block) != 0) {
        perror("Error al truncar el archivo");
        fclose(archivo);
        return 1;
    }
    //suponiendo que el cursor esta al inicio
    fseek(archivo,0,SEEK_SET);
    for (int i = 0; i < size_block; i++)
    {
        fputc(caracter_relleno,archivo);
    }

    pthread_mutex_unlock(&mutex_bloques);
    
    fclose(archivo);
    return 0;
}

char* get_contenido(char* path){


    FILE *archivo = fopen(path, "rb");
    if (archivo == NULL) {
        perror("Error al abrir el archivo");
        return NULL;
    }

    // Mover el puntero al final para obtener el tamaño
    fseek(archivo, 0, SEEK_END);
    long tamano = ftell(archivo);
    rewind(archivo); // Volver el puntero al inicio

    // Asignar memoria para el contenido + 1 para el terminador nulo
    char *contenido = (char *)malloc(tamano + 1);
    if (contenido == NULL) {
        perror("Error de asignación de memoria");
        fclose(archivo);
        return NULL;
    }

    // Leer el archivo completo en el buffer
    
    if (fread(contenido, 1, tamano, archivo) != tamano) {
        perror("Error al leer el archivo");
        free(contenido);
        fclose(archivo);
        return NULL;
    }
    
    // Añadir el terminador nulo
    contenido[tamano] = '\0';


    fclose(archivo);
    return contenido;
}

void iniciar_bloques_fisicos(int count_block,int size_block){
    char* nameBloque=string_new(); 
    char* digitos=string_new();
    char* _ruta = string_new();
        
    for (int i = 0; i < count_block; i++)
    {
        if (i<10) // de 0 a 9
        {
            digitos=string_from_format("000%d",i);
        }
        else if(i>=10 && i<100) // de 10 a 99
        {
            digitos=string_from_format("00%d",i);
        }
        else if(i>=100 && i <1000) // de 100 a 999
        {
            digitos=string_from_format("0%d",i);
        }
        else // de 1000 a 9999 o más
        {
            digitos=string_from_format("%d",i);
        }
        
        nameBloque=string_from_format("block%s",digitos);

        _ruta=string_from_format("%s/%s.dat",directorio_blocks_fisicos,nameBloque);
        
        crear_archivo_size(_ruta,size_block);
        if(i==0){
            // se llena el primer block fisico con cero
            llenar_archivo_truncado(_ruta,'0',size_block);
        }

        t_block_fisico * un_block=malloc(sizeof(t_block_fisico));
        (*un_block).path_block=nameBloque;
        (*un_block).contenido=get_contenido(_ruta);
        (*un_block).blogico_asociados=0;
        list_add(lista_bloques_fisicos,un_block); // lista de nombres bloque fisicos
         // preguntar cual bloque se llena de cero
    }
    free(_ruta);
    free(digitos);
    //return 0;
}

void iniciar_bloques_fisicos_existentes(int count_block,int size_block){
   
    for (int i = 0; i < count_block; i++)
    {
        char* digitos;
        if (i<10) // de 0 a 9
        {
            digitos=string_from_format("000%d",i);
        }
        else if(i>=10 && i<100) // de 10 a 99
        {
            digitos=string_from_format("00%d",i);
        }
        else if(i>=100 && i <1000) // de 100 a 999
        {
            digitos=string_from_format("0%d",i);
        }
        else // de 1000 a 9999 o más
        {
            digitos=string_from_format("%d",i);
        }
        
        char* nameBloque=string_from_format("block%s",digitos);
        char* _ruta=string_from_format("%s/%s.dat",directorio_blocks_fisicos,nameBloque);
        free(digitos);

        //crear_archivo_size(_ruta,size_block);
        t_block_fisico * un_block=malloc(sizeof(t_block_fisico));

        un_block->path_block = string_duplicate(nameBloque);      // copia propia del nombre
        un_block->contenido = get_contenido(_ruta);               // puede ser NULL si falta el archivo
        un_block->blogico_asociados = 0;
         if (!un_block->contenido) {
            // si no se pudo leer, inicializa un buffer vacío de tamaño size_block
            un_block->contenido = calloc(size_block + 1, 1);
        }
        list_add(lista_bloques_fisicos,un_block);
        free(nameBloque);
        free(_ruta);
    }
    
}

t_metadata* crear_metadata(char* ruta){
    t_metadata* md = malloc(sizeof(t_metadata));
    if (!md) { perror("malloc metadata"); exit(EXIT_FAILURE); }

    t_config* cfg = config_create(ruta);
    if (!cfg) {
        free(md);
        return NULL;
    }

    // TAMAÑO (si no está, asumimos 0)
    if (config_has_property(cfg, "TAMAÑO"))
        md->tamanio_file = config_get_int_value(cfg, "TAMAÑO");
    else
        md->tamanio_file = 0;

    // ESTADO (si no está, asumimos WORK_IN_PROGRESS)
    if (config_has_property(cfg, "ESTADO")) {
        char* estado_str = config_get_string_value(cfg, "ESTADO");
        md->estado = convertir_string_a_estado(estado_str); // implementá este helper
    } else {
        md->estado = WORK_IN_PROGRESS;
    }

    // BLOCKS: clonar el array que devuelve config
    md->blocks_fisicos_usados = string_array_new();
    char** array_original = config_get_array_value(cfg, "BLOCKS");
    if (array_original != NULL) {
        for (int i = 0; array_original[i] != NULL; i++) {
            string_array_push(&md->blocks_fisicos_usados, strdup(array_original[i]));
        }
    }

    config_destroy(cfg);
    return md;
}


void linkear_block_inicial(char* _ruta_block_logicos){
    char* _rutaBloqueFisico = string_from_format("%s/%s.dat",directorio_blocks_fisicos,BLOCK_FISICO_INICIAL);
    char* _rutaBloqueLogico = string_from_format("%s/%s",_ruta_block_logicos,BLOCK_LOGICO_INICIAL);
    
    if (link(_rutaBloqueFisico,_rutaBloqueLogico)!=0)
    {
        log_debug(storage_log_debug,"Enlace no fue creado");
    }
    
    log_debug(storage_log_debug,"Enlace creado exitosamente");
}

t_tag* crear_tag(char* nameTag, char* ruta_tag) {
    t_tag* un_tag = malloc(sizeof(t_tag));

    if (un_tag == NULL) {
        perror("Error de asignación de memoria para tag");
        return NULL;
    }

    char* _ruta_meta = string_from_format(DIR_METADATA, ruta_tag);
    crear_archivo(_ruta_meta); 
    escribir_archivo(_ruta_meta,
                     "TAMAÑO=0\nBLOCKS=[]\nESTADO=WORK_IN_PROGRESS\n");
    
    un_tag->name_tag = string_duplicate(nameTag);
    un_tag->metadata = crear_metadata(_ruta_meta);
    un_tag->lista_block_logicos = list_create();

    char* _ruta_block_logicos = string_from_format(DIR_LOGICAL_BLOCK, ruta_tag);
    crear_ruta(_ruta_block_logicos);

    free(_ruta_meta);
    free(_ruta_block_logicos);

    return un_tag;
}

void crear_file(char* nameFile){
    
    t_file * un_file = malloc(sizeof(t_file));
    if (un_file == NULL) {
        perror("Error de asignación de memoria para file");
        exit(EXIT_FAILURE);
    }
    //(*un_file).name_File = nameFile;
    un_file->name_File = strdup(nameFile);
    
    //printf("\n%s\n",(*un_file).name_File);
    
    char* ruta_file= string_from_format("%s/%s",directorio_files,nameFile);
    crear_ruta(ruta_file); //crea si no existe y no lo hace si ya existe
   (*un_file).tags =list_create();

    pthread_mutex_lock(&mutex_listas_archivos);

    list_add(lista_files,un_file);

    pthread_mutex_unlock(&mutex_listas_archivos);
}

//iniciar_tag nuevo a un file
void asigar_tag_a_file(char* nameFile , char* nameTag,int index){ 
    t_file* un_file = list_get(lista_files,index);

    char* ruta_file = string_from_format("%s/%s", directorio_files, un_file->name_File);
    char* ruta_tag  = string_from_format("%s/%s", ruta_file, nameTag);

    crear_ruta(ruta_tag);
    t_tag* tag = crear_tag(nameTag, ruta_tag);

    pthread_mutex_lock(&mutex_listas_archivos);
    list_add(un_file->tags, tag);
    pthread_mutex_unlock(&mutex_listas_archivos);

    // solo liberar las rutas
    free(ruta_file);
    free(ruta_tag);
}

t_list* listar_directorios(const char *ruta) {
    t_list* lista=list_create();
    DIR *directorio;
    struct dirent *entrada;
    char ruta_completa[1024];
    struct stat info_archivo;

    // Abrir el directorio
    directorio = opendir(ruta);
    if (directorio == NULL) {
        perror("Error al abrir el directorio");
        list_destroy(lista); 
        return NULL;
    }

    // Leer cada entrada del directorio
    while ((entrada = readdir(directorio)) != NULL) {
        // Ignorar "." (directorio actual) y ".." (directorio padre)
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0) {
            continue;
        }

        // Construir la ruta completa
        snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", ruta, entrada->d_name);

        // Obtener información del archivo para saber si es un directorio
        if (stat(ruta_completa, &info_archivo) == 0 && S_ISDIR(info_archivo.st_mode)) {
            // Imprimir el nombre de la carpeta
            char* name_file = string_duplicate(entrada->d_name);   
            printf("Carpeta encontrada: %s\n", name_file);
            list_add(lista, name_file);
        }
    }

    // Cerrar el directorio
    closedir(directorio);

    return lista;
}
t_list* listar_archivos(const char *ruta) {
    t_list* lista =list_create();
    DIR *directorio;
    struct dirent *entrada;
    char ruta_completa[1024];
    struct stat info_archivo;

    // Abrir el directorio
    directorio = opendir(ruta);
    if (directorio == NULL) {
        perror("Error al abrir el directorio");
        list_destroy(lista);
        return NULL;
    }

    // Leer cada entrada del directorio
    while ((entrada = readdir(directorio)) != NULL) {
        // Ignorar "." (directorio actual) y ".." (directorio padre)
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0) {
            continue;
        }

        // Construir la ruta completa
        snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", ruta, entrada->d_name);

        // Obtener información del archivo para saber si es un archivo regular
        if (stat(ruta_completa, &info_archivo) == 0 && S_ISREG(info_archivo.st_mode)) {
            // Imprimir el nombre del archivo
            char* name_archivo = string_duplicate(entrada->d_name);  
            printf("Archivo encontrado: %s\n", name_archivo);
            list_add(lista, name_archivo);
        }
    }

    // Cerrar el directorio
    closedir(directorio);
  
    return lista;
}

// no eficiente
void crear_primer_file(){
    char* ruta_initial=string_from_format("%s/initial_file",directorio_files);
    crear_ruta(ruta_initial);
    // crear tag BASE
    char* base=string_from_format("%s/BASE",ruta_initial);
    crear_ruta(base);
    // crear_metadata
    crear_archivo(string_from_format(DIR_METADATA,base));
    // crear dir logical_block
    char* ruta_logical= string_from_format(DIR_LOGICAL_BLOCK,base);
    crear_ruta(ruta_logical);
    // creo block_logico
    char* block_log_0000=string_from_format("%s/000000.dat",ruta_logical);
    //crear_archivo(block0000);
    //crear block fisico
    char* block_fis_0000=string_from_format("%s/block0000.dat",directorio_blocks_fisicos);
    crear_archivo_size(block_fis_0000,superbloque.BLOCK_SIZE);

    if(link(block_fis_0000,block_log_0000)==0){
        log_trace(storage_log_debug,"Se creo link de '%s' a '%s' ",block_fis_0000,block_log_0000);
    }
    else{
        log_error(storage_log_debug,"No se pudo hacer hardlink(),ya existe %s",block_log_0000);
    }
    // implementar
}
void fresh_start_true(){

    log_trace(storage_log_debug,"## Fresh strart: TRUE - formatenado");
    // 1) destruir estructuras RAM previas si están
    if (lista_files) {
        list_clean_and_destroy_elements(lista_files, destruir_file);
        list_destroy(lista_files);
    }
    lista_files = list_create();

    if (lista_bloques_fisicos) {
        list_clean_and_destroy_elements(lista_bloques_fisicos, destruir_block_fisico);
        list_destroy(lista_bloques_fisicos);
    }
    lista_bloques_fisicos = list_create();

    // 2) borrar carpetas / archivos en disco (logs detallados)
    if (eliminar_carpeta_con_contido(directorio_blocks_fisicos) != 0)
        log_warning(storage_log_debug, "No se pudo limpiar directorio blocks fisicos: %s", directorio_blocks_fisicos);

    if (eliminar_carpeta_con_contido(directorio_files) != 0)
        log_warning(storage_log_debug, "No se pudo limpiar directorio files: %s", directorio_files);

    if (eliminar_archivo(ruta_bitmap) != 0)
        log_warning(storage_log_debug, "No se pudo eliminar bitmap: %s", ruta_bitmap);

    if (eliminar_archivo(ruta_hash_index) != 0)
        log_warning(storage_log_debug, "No se pudo eliminar hash index: %s", ruta_hash_index);

    // 3) crear rutas base (si falla, lo logueamos y dejamos que main decida)
    log_debug(storage_log_debug,"## Creando directorios y archivos nativos");
    if (crear_ruta(directorio_blocks_fisicos) < 0)
        log_error(storage_log_debug, "Fallo creando %s", directorio_blocks_fisicos);

    if (crear_ruta(directorio_files) < 0)
        log_error(storage_log_debug, "Fallo creando %s", directorio_files);

    if (access(ruta_superbloque,F_OK)==0)
    {
        crear_archivo(ruta_hash_index);
        /*
        uint32_t cant_block=(uint32_t)(superbloque.FS_SIZE/superbloque.BLOCK_SIZE);
        abrir_bitmap(ruta_bitmap,cant_block);
        */
    }
    else{
        log_error(storage_log_debug,"## Error porque no existe superbloque ");
    }
    log_info(storage_log_debug, "Fresh start: limpieza completada (solo disco + estructuras vacías).");
    
}

t_estado convertir_string_a_estado(char * cadena){
    
    if (strcmp(cadena, "COMMITED") == 0) {
        return COMMITED;
    }
    return WORK_IN_PROGRESS;
}
char* convertir_estado_a_string(t_estado estado){
    if (estado == COMMITED)
    {
        return "COMMITED";
    }
    else
    {
        return "WORK_IN_PROGRESS";
    }
    
    return NULL; //Error
}
t_metadata* cargar_metadata(char* ruta){
    t_metadata* metadata = malloc(sizeof(t_metadata));
    t_config* config = config_create(ruta);
    if (config == NULL)
    {
        //crear metadata
        return NULL;
    }
    (*metadata).tamanio_file=config_get_int_value(config,"TAMAÑO");
    char* estado = config_get_string_value(config,"ESTADO");
    (*metadata).estado = convertir_string_a_estado(estado);
    (*metadata).blocks_fisicos_usados = config_get_array_value(config,"BLOCKS");
    return metadata;
}

// Destruye un t_block_fisico (si tu struct tiene campos heap, liberarlos)
void destruir_block_fisico(void* elem) {
    if (!elem) return;
    t_block_fisico* bf = (t_block_fisico*) elem;
    free(bf->path_block);            // si fue strdup'ed
    free(bf);
}

// Destruye un t_block_logico
void destruir_block_logico(void* elem) {
    if (!elem) return;
    t_block_logico* bl = (t_block_logico*) elem;
    free(bl->nombre_block);
    free(bl->bloque_fisico_asociado);
    free(bl);
}

// Destruye un t_tag (libera metadata y lista de blocks lógicos)
void destruir_tag(void* elem) {
    if (!elem) return;
    t_tag* tag = (t_tag*) elem;

    free(tag->name_tag);

    if (tag->lista_block_logicos) {
        list_clean_and_destroy_elements(tag->lista_block_logicos, destruir_block_logico);
        list_destroy(tag->lista_block_logicos);
    }

    if (tag->metadata) {
        // liberar el array blocks_fisicos_usados
        if (tag->metadata->blocks_fisicos_usados)
            string_array_destroy(tag->metadata->blocks_fisicos_usados);
        free(tag->metadata);
    }

    free(tag);
}

// Destruye un t_file (libera tags y nombre)
void destruir_file(void* elem) {
    if (!elem) return;
    t_file* f = (t_file*) elem;
    free(f->name_File);
    if (f->tags) {
        list_clean_and_destroy_elements(f->tags, destruir_tag);
        list_destroy(f->tags);
    }
    free(f);
}

void destruir_hilo_worker(void *elem) {
    if (!elem) return;

    t_hilo_worker *hw = (t_hilo_worker *)elem;

    if (hw->FD_HILO > 0) {
        shutdown(hw->FD_HILO, SHUT_RDWR);
        close(hw->FD_HILO);
    }

    // NO join si fue detached
    pthread_cancel(hw->HILO);

    free(hw);
}

// Safe push al string_array: siempre push heap-allocated string
void safe_string_array_push(char*** arr_ptr, const char* s) {
    if (!s) return;
    char* copy = strdup(s);
    string_array_push(arr_ptr, copy);
}

/*void fresh_start_false(){

    log_trace(storage_log_debug,"## Fresh strart: FALSE ");

    // Primero veo cuantos files hay y listo sus nombres
    t_list* lista =listar_directorios(directorio_files);
    int size = list_size(lista);
    for (int i = 0; i <size ; i++)
    {
        t_file* _file =malloc(sizeof(t_file));
        _file->name_File= list_get(lista,i);
        char* _ruta_file=string_from_format("%s/%s",directorio_files,_file->name_File);
        
        _file->tags = list_create();
        t_list* lista_name_tag = listar_directorios(_ruta_file);
        // Segundo veo cuantos tags hay en el file y sus nombres
        for (int j = 0; j < list_size(lista_name_tag); j++)
        {
            t_tag* _tag = malloc(sizeof(t_tag));
            (*_tag).name_tag = list_get(lista_name_tag,j);

            char* _ruta_tag =string_from_format("%s/%s",_ruta_file,(*_tag).name_tag);
            char * _ruta_meta=string_from_format(DIR_METADATA,_ruta_tag);

            (*_tag).metadata=cargar_metadata(_ruta_meta);
            (*_tag).lista_block_logicos =list_create();

            char* _ruta_bl=string_from_format(DIR_LOGICAL_BLOCK,_ruta_tag);

            t_list * lista_name_bl = listar_archivos(_ruta_bl);

            //recorre los archivos de bloque logico del tag especifico
            for (int x = 0; x < list_size(lista_name_bl); x++)
            {
                t_block_logico* _bl=malloc(sizeof(t_block_logico));
                (*_bl).nombre_block=list_get(lista_name_bl,x);
                (*_bl).bloque_fisico_asociado=BLOCK_FISICO_INICIAL;
                list_add((*_tag).lista_block_logicos,_bl);
            }
            list_destroy_and_destroy_elements(lista_name_bl, free);
            list_add(_file->tags, _tag);
        }
        list_destroy_and_destroy_elements(lista_name_tag, free);
        list_add(lista_files, _file);
        
    }
    list_destroy_and_destroy_elements(lista, free);
}*/

void fresh_start_false(){

    log_trace(storage_log_debug,"## Fresh strart: FALSE ");

    t_list* lista = listar_directorios(directorio_files);
    if (!lista) {
        log_debug(storage_log_debug,
                  "No hay files en %s, no hay nada para levantar",
                  directorio_files);
        return;
    }

    int size = list_size(lista);
    for (int i = 0; i < size; i++)
    {
        t_file* _file = malloc(sizeof(t_file));
        _file->name_File = string_duplicate(list_get(lista, i));   // 👈 copia
        _file->tags = list_create();

        char* _ruta_file = string_from_format("%s/%s", directorio_files, _file->name_File);

        t_list* lista_name_tag = listar_directorios(_ruta_file);
        if (!lista_name_tag) {
            // file sin tags
            list_add(lista_files, _file);
            free(_ruta_file);
            continue;
        }

        for (int j = 0; j < list_size(lista_name_tag); j++)
        {
            t_tag* _tag = malloc(sizeof(t_tag));
            _tag->name_tag = string_duplicate(list_get(lista_name_tag, j));  // 👈 copia
            _tag->lista_block_logicos = list_create();

            char* _ruta_tag = string_from_format("%s/%s", _ruta_file, _tag->name_tag);
            char* _ruta_meta = string_from_format(DIR_METADATA, _ruta_tag);
            _tag->metadata = crear_metadata(_ruta_meta);   // 👈 usamos la nueva

            char* _ruta_bl = string_from_format(DIR_LOGICAL_BLOCK, _ruta_tag);
            t_list * lista_name_bl = listar_archivos(_ruta_bl);

            if (lista_name_bl) {
                for (int x = 0; x < list_size(lista_name_bl); x++)
                {
                
                    t_block_logico* _bl = malloc(sizeof(t_block_logico));
                    _bl->nombre_block = string_duplicate(list_get(lista_name_bl, x));
                    
                    // ✅ CLAVE: Leer el hard link para saber a qué BF apunta
                    char* ruta_bl_completa = string_from_format("%s/%s", 
                                                                _ruta_bl, 
                                                                _bl->nombre_block);
                    char* contenido_bl=get_contenido(ruta_bl_completa);
                    char* hash_bl=crypto_md5(contenido_bl,strlen(contenido_bl)+1);

                    t_block_fisico* bf_asociado = buscar_bf_asociado(hash_bl);
                    if(bf_asociado == NULL){
                        _bl->bloque_fisico_asociado= strdup(BLOCK_FISICO_INICIAL);
                    }
                    else{
                        _bl->bloque_fisico_asociado = strdup(bf_asociado->path_block);
                    }
                    
                     // Incrementar contador del BF
                    bf_asociado->blogico_asociados++;
                    
                    list_add(_tag->lista_block_logicos, _bl);
                    free(ruta_bl_completa);
                    free(contenido_bl);
                    free(hash_bl);
                }
                list_destroy_and_destroy_elements(lista_name_bl, free);
            }

            free(_ruta_tag);
            free(_ruta_meta);
            free(_ruta_bl);

            list_add(_file->tags, _tag);
        }

        list_destroy_and_destroy_elements(lista_name_tag, free);
        free(_ruta_file);

        list_add(lista_files, _file);
    }

    list_destroy_and_destroy_elements(lista, free);
}
