#include <stdio.h>
#include <stdlib.h>  // Para atoi()

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Uso: %s <nombre> <numero>\n", argv[0]);
        return 1;
    }

    char *nombre = argv[1];
    int repeticiones = atoi(argv[2]); 

    for (int i = 0; i < repeticiones; i++) {
        printf("%s\n", nombre);
    }

    return 0;
}

