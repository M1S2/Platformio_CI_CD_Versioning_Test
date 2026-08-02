#ifndef MAIN_H
#define MAIN_H

#include "updateHandling.h"
#include "updateHandling_Part1.h"
#include "updateHandling_Part2.h"

extern UpdateHandling updateHandling;
extern UpdateHandlingPart1 updateHandlingPart1;
extern UpdateHandlingPart2 updateHandlingPart2;

void main_initWebserverEndpoints();

#endif