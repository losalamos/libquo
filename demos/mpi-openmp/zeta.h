/*
 * Copyright (c) 2017-2019 Triad National Security, LLC
 *                         All rights reserved.
 *
 * This file is part of the libquo project. See the LICENSE file at the
 * top-level directory of this distribution.
 */

/*
 * Parallel library that calculates z(s) for any s > 1.0, where z is the
 * Euler–Riemann zeta function.
 */

#ifndef ZETA_H_INCLUDED
#define ZETA_H_INCLUDED

#include <inttypes.h>
#include "mpi.h"

enum {
    ZETA_SUCCESS = 0,
    ZETA_FAILURE,
    ZETA_INVALID_ARG,
    ZETA_OOR
};

int
zeta(double s,
     int64_t n,
     double **z,
     MPI_Comm comm,
     double *result);

#endif
