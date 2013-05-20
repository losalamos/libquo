/**
 * Copyright (c) 2013      Los Alamos National Security, LLC.
 *                         All rights reserved.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "quo.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STDBOOL_H
#include <stdbool.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

int
main(void)
{
    int qrc = QUO_SUCCESS, erc = EXIT_SUCCESS;
    int qv = 0, qsv = 0;
    int nsockets = 0, ncores = 0, npus = 0;
    char *bad_func = NULL;
    char *topostr = NULL, *cbindstr = NULL;
    bool bound = false;
    quo_t *quo = NULL;

    if (QUO_SUCCESS != (qrc = quo_version(&qv, &qsv))) {
        bad_func = "quo_version";
        goto out;
    }
    if (QUO_SUCCESS != (qrc = quo_init())) {
        bad_func = "quo_init";
        goto out;
    }
    if (QUO_SUCCESS != (qrc = quo_construct(&quo))) {
        bad_func = "quo_construct";
        goto out;
    }
    if (QUO_SUCCESS != (qrc = quo_node_topo_stringify(quo, &topostr))) {
        bad_func = "quo_node_topo_emit";
        goto out;
    }
    if (QUO_SUCCESS != (qrc = quo_nsockets(quo, &nsockets))) {
        bad_func = "quo_nsockets";
        goto out;
    }
    if (QUO_SUCCESS != (qrc = quo_ncores(quo, &ncores))) {
        bad_func = "quo_ncores";
        goto out;
    }
    if (QUO_SUCCESS != (qrc = quo_npus(quo, &npus))) {
        bad_func = "quo_npus";
        goto out;
    }
    if (QUO_SUCCESS != (qrc = quo_bound(quo, &bound))) {
        bad_func = "quo_bound";
        goto out;
    }
    if (QUO_SUCCESS != (qrc = quo_stringify_cbind(quo, &cbindstr))) {
        bad_func = "quo_stringify_cbind";
        goto out;
    }
    if (QUO_SUCCESS != (qrc = quo_destruct(quo))) {
        bad_func = "quo_destruct";
        goto out;
    }
    printf("### quo version: %d.%d ###\n", qv, qsv);
    printf("### nsockets: %d\n", nsockets);
    printf("### ncores: %d\n", ncores);
    printf("### npus: %d\n", npus);
    printf("### process %d [%s] bound: %s\n",
           (int)getpid(), cbindstr, bound ? "true" : "false");
    printf("### begin system topology\n%s###end system topology\n", topostr);
    /* the string returned by quo_node_topo_stringify MUST be free'd by us */
    free(topostr);
    free(cbindstr);
out:
    if (NULL != bad_func) {
        fprintf(stderr, "xxx %s failure in: %s\n", __FILE__, bad_func);
        erc = EXIT_FAILURE;
    }
    return erc;
}