//
// Created by joshu on 15/11/2025.
//

#ifndef FUSELOWLEVELAPI_H
#define FUSELOWLEVELAPI_H

#define FUSE_NEW_ARGUMENTS (arguments_cuantity, arguments ) {arguments_cuantity, argumens, 0}


struct fuse_arguments {
    int arguments_quantity;
    char **arguments;
    int is_allocated;

  };

 struct fuse_commandline_options {
   int singlethread; //Para que corra un solo hilo
   int foreground; // para que no corra en el background
   int debug; //Para hacer los debugs
   int nodefault_subtype; //Aqui vamos a asignar que tipo de subtype es el nodo (QR?)
   char *mountponts; //destino del punto de montaje
   int show_version; //podriamos quitarlo sirve param ostrar la version
   int show_help;
   int clone_fd; //fuse maneja descriptores de archivos en la capa de comnicacion
   unsigned int max_idle_threads; //hilos inactivos
   unsigned int max_threads; //hilos maximos


   };





struct fuse_lowlevel_operations {

  void (*init)(void *userdata, struct fuse_conn_info *conn);

  void (*destroy)(void *userdata);

  void (*lookup)(fuse_req_t req, );


  };






#endif //FUSELOWLEVELAPI_H
