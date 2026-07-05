// src/pipeline/rtmp/compat/util/platform.h
//
// third_party/librtmp/rtmp.c, OBS ağacında <util/platform.h> (libobs) include
// eder ama yalnızca stdbool ve UNUSED_PARAMETER kullanır (Faz2/Aşama2.1 keşfi).
// libobs'u çekmemek için asgari stub — Reji'ye ait, libobs kodu İÇERMEZ.
#pragma once
#include <stdbool.h>
#define UNUSED_PARAMETER(x) ((void)(x))
