#include <stdio.h>

int main ()
{
   FILE *fp;
   printf("Programa que intenta abrir el archivo file.txt\n");
   /* Intentando abrir el archivo */
   fp = fopen("file.txt", "r");
   if( fp == NULL ) {
      return(-1);
   }
   fclose(fp);
      
   return(0);
}
