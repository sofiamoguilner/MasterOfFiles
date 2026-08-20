# MasterOfFiles — Distributed Operating System Simulator

*A university project simulating the core components of a distributed operating system, built in C.*

[🇬🇧 English](#english) | [🇦🇷 Español](#español)

---

## English

### Overview

**MasterOfFiles** is a simulated distributed operating system built from scratch in C. The project models how a real OS coordinates process scheduling, memory management, and file storage — but splits each responsibility into an independent module that runs as its own process, potentially on a different machine, and communicates with the others over the network using a custom protocol.

The goal was to apply, hands-on, core operating systems concepts covered in coursework: process scheduling algorithms, memory management, file systems, synchronization, and inter-process communication — while working within a real distributed, multi-machine environment rather than a single-process simulation.

### Architecture

The system is organized as a set of independent C programs, each with its own Makefile, configuration files, and structured logging:

| Module | Role |
|---|---|
| **master** | Acts as the coordinating kernel: schedules simulated processes, resolves system requests, and handles incoming queries. |
| **worker** | Executes instructions for each simulated process, managing internal memory and an instruction fetch–decode–execute cycle. |
| **storage** | Persistent storage layer: manages data in fixed-size physical blocks with a bitmap for allocation, similar to a simplified filesystem. |
| **query_control** | Testing/validation tool used to submit and run test cases (e.g. FIFO and Aging scheduling scenarios) against the system. |
| **utils** | Shared library code: the communication protocol and common helpers used across every module. |

Each module runs as a real, independent OS process (compiled separately), can be deployed on a different machine or VM, and is configured through plain-text `.config` files rather than being recompiled between test runs.

### Key concepts applied

- Process scheduling (multiple algorithms tested, e.g. FIFO, Aging)
- Distributed inter-process communication over a custom protocol
- Memory management and simulated instruction execution
- Block-based storage with allocation bitmaps
- Configuration-driven, logged, testable system design
- Iterative-incremental development methodology across modules

### Tech stack

- **Language:** C
- **Build system:** Makefiles
- **Environment:** Linux, developed and tested across multiple virtual machines to validate distributed behavior

### Notes

This project was developed as the practical assignment for the Operating Systems course at university, evaluated through automated tests plus an individual oral defense of the design and implementation.

---

## Español

### Descripción general

**MasterOfFiles** es un sistema operativo distribuido simulado, desarrollado desde cero en C. El proyecto modela cómo un sistema operativo real coordina la planificación de procesos, la gestión de memoria y el almacenamiento de archivos — pero divide cada responsabilidad en un módulo independiente que corre como un proceso propio, potencialmente en una máquina distinta, y se comunica con los demás por red mediante un protocolo propio.

El objetivo fue aplicar de forma práctica los conceptos centrales de sistemas operativos vistos en la materia: algoritmos de planificación de procesos, administración de memoria, sistemas de archivos, sincronización y comunicación entre procesos — trabajando en un entorno realmente distribuido y multi-máquina, en lugar de una simulación de un solo proceso.

### Arquitectura

El sistema está organizado como un conjunto de programas en C independientes, cada uno con su propio Makefile, archivos de configuración y logging estructurado:

| Módulo | Rol |
|---|---|
| **master** | Actúa como el kernel coordinador: planifica los procesos simulados, resuelve peticiones al sistema y gestiona las queries entrantes. |
| **worker** | Ejecuta las instrucciones de cada proceso simulado, administrando la memoria interna y un ciclo de fetch–decode–execute. |
| **storage** | Capa de almacenamiento persistente: gestiona los datos en bloques físicos de tamaño fijo con un bitmap de asignación, similar a un sistema de archivos simplificado. |
| **query_control** | Herramienta de testing/validación usada para enviar y correr casos de prueba (por ejemplo, escenarios de planificación FIFO y Aging) contra el sistema. |
| **utils** | Código compartido: el protocolo de comunicación y funciones auxiliares comunes usadas por todos los módulos. |

Cada módulo corre como un proceso real e independiente del sistema operativo (compilado por separado), puede desplegarse en una máquina o VM distinta, y se configura mediante archivos `.config` de texto plano, sin necesidad de recompilar entre pruebas.

### Conceptos clave aplicados

- Planificación de procesos (varios algoritmos probados, ej. FIFO, Aging)
- Comunicación entre procesos distribuidos mediante protocolo propio
- Administración de memoria y ejecución simulada de instrucciones
- Almacenamiento basado en bloques con bitmap de asignación
- Diseño configurable, con logging y testeable
- Metodología de desarrollo iterativo-incremental por módulos

### Stack técnico

- **Lenguaje:** C
- **Sistema de build:** Makefiles
- **Entorno:** Linux, desarrollado y probado en múltiples máquinas virtuales para validar el comportamiento distribuido

### Notas

Este proyecto fue desarrollado como trabajo práctico de la materia Sistemas Operativos, evaluado mediante pruebas automatizadas y un coloquio oral individual sobre el diseño y la implementación.
