#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    FILE *fp;

    if (argc != 2) { // Validar argumento
        printf("Uso: %s <nombre_archivo>\n", argv[0]);
        return 1;
    }

    printf("Intentando abrir el archivo: %s\n", argv[1]);
    fp = fopen(argv[1], "r");

    if (fp == NULL) {
        perror("No se pudo abrir el archivo"); 
        exit(1);
    }

    fclose(fp);
    return 0;
}
