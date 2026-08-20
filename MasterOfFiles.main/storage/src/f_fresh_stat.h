#ifndef F_FRESH_START_H
#define F_FRESH_START_H

#include <storage_gestion_estructuras.h>
#include <operaciones_storage.h>

/*
    Crea ruta dada 
    ejemplo: ruta = "/home/utnso/storage"
    
    Devuelve 
         0 si es exitoso
         1 si existe 
        -1 si hubo un error
*/
int crear_ruta( char* ruta );

/*
    Crea archivo si no existe 
    si existe lo abre y cierra
*/
void crear_archivo(char* ruta);

long obtener_tamano_archivo(char* nombre_archivo);

/*
    Crea archivo truncandolo en los bytes dados
    Devuelve
         0 si es exitoso
        -1 si hubo un error
*/
int crear_archivo_size(char* ruta, int tamanio_bytes);

void escribir_archivo(char* ruta, char* texto);
/*
    Elimina la ruta dada y sus archivos
    Devuelve
         0 si es exitoso
         1 si hubo un error 
*/
int eliminar_carpeta_con_contido(char* ruta);
/*
    Elimina el archivo
    ruta : "/home/utnso/storage/bitmap.bin"
    Devuelve
         0 si es exitoso
         1 si hubo un error 
*/
int eliminar_archivo(char* ruta);

void leer_config_superblock(char* ruta);
//void escribir_config(char* ruta, void * block,char* name_block);

// -----Nuevos para struct fs
/*
    Escribe en un archivo truncado
    Verifica que el contenido del archivo mas 
    la cadena sea menor al tmaño del archivo
    Entonces recien escribe
    path_archivo debe ser una direccion especifica "home/sotage/..."
    Devuelve
         0 si es exitoso
         1 si hubo un error al abrir archivo
        -1 cadena sobrepasa el tamaño del archivo
*/
int escribir_archivo_truncado(char* path_archivo,void* buffer,int size_block);
/*
    Rellena una archivo vacio truncado que esta lleno de ´\0´
    lo llena con un carracter dado
    Funcion usada para inicializar un block fisico
    Devuelve
         0 si es exitoso
         1 si hubo un error 
*/
int llenar_archivo_truncado(char* path_archivo,char caracter_relleno,int size_block);
/*
    Trae el contenido de un archivo
    Devuelve
        char* contenido 
        NULL si huno un error
*/
char* get_contenido(char* path);
/*
    Crea un t_block_fisico segun count_block 
    Crea el archivo .dat segun el tamanio indicado 
    Rellena el archivo .dat con '0' ceros todo el archivo
    Guarda contenido del archivo en t_block_fisico . contenido
    Guarda el t_block_fisico en la lista 'lista_bloques_fisicos'
*/
void iniciar_bloques_fisicos(int count_block,int size_block);
/*
    Crea un t_block_fisico segun count_block
    Abre el archivo . dat y guarda lso datos en t_block_logico
    Agrega el block fisico en la lista 'lista_bloques_fisicos'
*/
void iniciar_bloques_fisicos_existentes(int count_block,int size_block);

/*
    Crea variable t_metadata
    Inicializa la variable
    estado = WORK_IN_PROGRESS
    tamanio_file = 0
    block_fisicos_usados =NULL // []
*/
t_metadata* crear_metadata();
/*
 link (block_fisico_0 , block_logico_0)
 por el momento solo pide ruta de blog logico de un file
 ejemplo : ruta  "/homr/utnso/storage/file/nameFile/nameTag/blog_logicos"
*/
void linkear_block_inicial(char* _ruta_block_logicos);
/*
    Crea un t_tag y lo inicializa
    t_tag .nameTag = nameTag
    t_tag.metadata = crear_metadata() // solo crea la variable
    t_tag .lista_block_logicos = lista_create();
    Crea su metadata.config y escribe los valores iniciales
    Crea ruta para los block logicos
    
    Devuelve el t_tag creado
*/
t_tag* crear_tag(char* nameTag,char* ruta_tag);

//void agregar_Tag_a_Lista(t_tag** head_tag ,char* nameTag,char* ruta_file);
/*
    Crea un t_file
    t_file.nameFile = nameFile
    t_file.tags = list_create(); 
    Agrega el t_file en la lista 'lista_files'
*/
void crear_file(char* nameFile );

/*
    A un file de la lista 'lista_files' segun index
    se le crea un tag nuevo
    Se crea la ruta y una variable t_tag =crear_tag()
    Agrega el tag nuevo al file indicado
*/
void asigar_tag_a_file(char* nameFile , char* nameTag, int index);
/*
    cadena : es el estado en formato char*
    Devuelve COMMITED o  WORK_IN_PROGRESS
*/
t_estado convertir_string_a_estado(char * cadena);
/*
    convierte t_estado en un string
*/
char* convertir_estado_a_string(t_estado estado);
/*
    Devuelve un t_metadata con los valores que habia en el config
*/
t_metadata* cargar_metadata(char* ruta);

/*
    Crea una lista con los nombres de carpetas existente 
    en una direccion dada

    Devuelve NULL si no hubo ninguna carpeta en la ruta
    Devuelve lista si se agrego name_file en la lista
*/
t_list* listar_directorios(const char* ruta);
/*
    Crea una lsita con los nombres de archivos existentes
    en una direccion dada
    Devuelve NULL si no encontro ningun archivo
    Devuelve lista si agrego nameArchivo
*/
t_list* listar_archivos(const char *ruta) ;
// ------fin fs

void montar_desde_cero(); // crea directorios nativos y superbloque.config

// crear bitmap vacío desde cero
int crear_bitmap_vacio(char* pathBitmap, uint32_t cant_block);
int abrir_bitmap (char* pathBitmap, uint32_t fs_size);
void destruir_bitmap();
void crear_primer_file();
/*
    Elimina todas las acrpetas y archivos en el punto de montaje
    Excepto superblock.config
    Crea rutas y archivos nativos faltantes
*/
void fresh_start_true();
void fresh_start_false();

// ----- Destructores -----

void destruir_block_fisico(void* elem);
void destruir_block_logico(void* elem);
void destruir_tag(void* elem);
void destruir_file(void* elem);
void destruir_hilo_worker(void *elem);

// ----- Utilidades -----

// Inserta en un string_array una copia heap-allocated de s
void safe_string_array_push(char*** arr_ptr, const char* s);




#endif