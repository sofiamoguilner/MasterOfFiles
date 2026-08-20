#include <storage_archivos.h>

// para manejo de bitmap y creacion de metadata

// ------   BITMAP  --------

void log_acceso_bitmap(uint32_t numeroBloque, uint32_t estadoBloque)
{   
    log_info(storage_logger, "## Acceso a Bitmap - Bloque: <%u> - Estado: <%u>", numeroBloque, estadoBloque);
    //log_info(storage_log_debug, "##Acceso a Bitmap - Bloque: <%u> - Estado: <%u>", numeroBloque, estadoBloque);
    return;   
}

int32_t bitmap_encontrar_bloque_libre()
{   
    pthread_mutex_lock(&mutex_bitmap);
    // false = 0 --> libre
    // true = 1 --> ocupado
    uint32_t posicion_bitmap;
    bool bloqueOcupado;
    for (posicion_bitmap=0; posicion_bitmap < (bitmap->tamanio * 8); posicion_bitmap++)
    {   
        bloqueOcupado  = bitarray_test_bit(bitmap->bitarray, posicion_bitmap);
        log_acceso_bitmap(posicion_bitmap, bloqueOcupado);
        // Si encuentra un bloque que esté en 0 devuelve la posición de ese bloque
        if(!bloqueOcupado)
        {

            pthread_mutex_unlock(&mutex_bitmap);
            return posicion_bitmap;
            //break;
        }
    }
    pthread_mutex_unlock(&mutex_bitmap);
     // Si no encuentra un bloque libre, retorna -1
    return -1;
}

// Para pruebas
void bitmap_mostrar_por_pantalla()
{   
    pthread_mutex_lock(&mutex_bitmap);
    // false = 0 --> libre
    // true = 1 --> ocupado
    uint32_t posicion_bitmap;
    bool bloqueLeido;
    for (posicion_bitmap=0; posicion_bitmap < (bitmap->tamanio * 8); posicion_bitmap++)
    {
        bloqueLeido  = bitarray_test_bit(bitmap->bitarray, posicion_bitmap);
        // Si encuentra un bloque que esté en 0 devuelve la posición de ese bloque
        if(bloqueLeido == 0)
        {
            log_info(storage_logger, "Bloque %u: 0", posicion_bitmap);
        }
        else
        {
            log_info(storage_logger, "Bloque %u: 1", posicion_bitmap);
        }
    }
    pthread_mutex_unlock(&mutex_bitmap);

   // return;
   
}

void bitmap_marcar_bloque_libre(uint32_t numeroBloque) // 0 --> libre
{
    pthread_mutex_lock(&mutex_bitmap);

    bitarray_clean_bit(bitmap->bitarray, numeroBloque);
    // Sincronizar los cambios en el archivo y verificar que se haga de forma correcta
    if (msync(bitmap->direccion, bitmap->tamanio, MS_SYNC) == -1) {
        log_error(storage_logger,"Error al sincronizar los cambios en el Bitmap");
    }
    log_acceso_bitmap(numeroBloque, 0);

    pthread_mutex_unlock(&mutex_bitmap);

    return;
}

void bitmap_marcar_bloque_ocupado(uint32_t numeroBloque) // 1 --> ocupado
{

    //log_info(storage_logger,"bitmap : %i",(int)numeroBloque);
    pthread_mutex_lock(&mutex_bitmap);

    bitarray_set_bit(bitmap->bitarray, numeroBloque);
    // Sincronizar los cambios en el archivo y verificar que se haga de forma correcta
    if (msync(bitmap->direccion, bitmap->tamanio, MS_SYNC) == -1) {
        log_error(storage_logger,"Error al sincronizar los cambios en el Bitmap");
    }
    log_acceso_bitmap(numeroBloque, 1);

    pthread_mutex_unlock(&mutex_bitmap);

    return;
}