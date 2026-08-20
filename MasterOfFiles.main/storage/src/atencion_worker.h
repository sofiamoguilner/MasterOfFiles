// storage_ops.h
#pragma once
#include <storage_gestion_estructuras.h>

void storage_handshake(int fd);
void storage_create(int fd);
void storage_truncate(int fd);
void storage_tag(int fd);
void storage_delete(int fd);
void storage_commit(int fd);
void storage_flush(int fd);
void storage_read_block(int fd);
void storage_write_block(int fd);
