#ifndef MAIN_H
#define MAIN_H

#include "updateHandling.h"
#include "updateHandling_Local.h"
#include "updateHandling_Remote.h"

extern UpdateHandling updateHandling;
extern UpdateHandlingLocal updateHandlingLocal;
extern UpdateHandlingRemote updateHandlingRemote;

void main_initWebserverEndpoints();

#endif