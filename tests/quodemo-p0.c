/**
 * Copyright (c) 2013      Los Alamos National Security, LLC.
 *                         All rights reserved.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "quodemo-multilib-common.h"
#include "quodemo-p1.h"
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

#include "mpi.h"

/**
 * libquo demo code that illustrates how two libraries interact.
 */

/**
 * demo finalize.
 */
static int
fini(context_t *c)
{
    if (!c) return 1;
    if (QUO_SUCCESS != quo_finalize(c->quo)) return 1;
    if (QUO_SUCCESS != quo_destruct(c->quo)) return 1;
    /* finalize mpi AFTER quo_destruct - we may mpi in our destruct */
    if (c->mpi_inited) MPI_Finalize();
    if (c->cbindstr) free(c->cbindstr);
    free(c);
    return 0;
}

/**
 * i'm being really sloppy here. ideally, one should probably save the rc and
 * then display or do some other cool thing with it. don't be like this code. if
 * quo_construct or quo_init fail, then someone using this could just continue
 * without the awesomeness that is libquo. they cleanup after themselves, so
 * things *should* be in an okay state after an early failure. the failures may
 * be part of a larger problem, however. if you are reading this, you are
 * probably the first -- thanks! */
static int
init(context_t **c)
{
    context_t *newc = NULL;
    /* alloc our context */
    if (NULL == (newc = calloc(1, sizeof(*newc)))) return 1;
    /* libquo requires that MPI be initialized before its init is called */
    if (MPI_SUCCESS != MPI_Init(NULL, NULL)) return 1;
    /* gather some basic job info from our mpi lib */
    if (MPI_SUCCESS != MPI_Comm_size(MPI_COMM_WORLD, &(newc->nranks))) goto err;
    /* ...and more */
    if (MPI_SUCCESS != MPI_Comm_rank(MPI_COMM_WORLD, &(newc->rank))) goto err;

    /* ////////////////////////////////////////////////////////////////////// */
    /* now libquo can be initialized. libquo uses mpi, so that needs to be
     * initialized first. */
    /* ////////////////////////////////////////////////////////////////////// */

    /* can be called at any point -- even before init and construct. */
    if (QUO_SUCCESS != quo_version(&(newc->qv), &(newc->qsv))) goto err;
    /* cheap call -- must be called before init. */
    if (QUO_SUCCESS != quo_construct(&(newc->quo))) goto err;
    /* relatively expensive call. you only really want to do this once at the
     * beginning of time and pass the context all over the place within your
     * code. that being said, this routine can be called, from within the same
     * quo context, multiple times. the first time is expensive, the others just
     * return QUO_SUCCESS_ALREADY_DONE if someone else already initialized the
     * quo context. */
    if (QUO_SUCCESS != quo_init(newc->quo)) goto err;
    newc->mpi_inited = true;
    *c = newc;
    return 0;
err:
    (void)fini(newc);
    return 1;
}

/**
 * gather system and job info from libquo.
 */
static int
sys_grok(context_t *c)
{
    char *bad_func = NULL;

    /* this interface is more powerful, but the other n* calls can be more
     * convenient. at any rate, this is an example of the
     * quo_get_nobjs_in_type_by_type interface to get the number of sockets on
     * the machine. note: you can also use the quo_nsockets or
     * quo_get_nobjs_by_type to get the same info. */
    if (QUO_SUCCESS != quo_get_nobjs_in_type_by_type(c->quo,
                                                     QUO_MACHINE,
                                                     0,
                                                     QUO_SOCKET,
                                                     &c->nsockets)) {
        bad_func = "quo_get_nobjs_in_type_by_type";
        goto out;
    }
    if (QUO_SUCCESS != quo_ncores(c->quo, &c->ncores)) {
        bad_func = "quo_ncores";
        goto out;
    }
    if (QUO_SUCCESS != quo_npus(c->quo, &c->npus)) {
        bad_func = "quo_npus";
        goto out;
    }
    if (QUO_SUCCESS != quo_bound(c->quo, &c->bound)) {
        bad_func = "quo_bound";
        goto out;
    }
    if (QUO_SUCCESS != quo_stringify_cbind(c->quo, &c->cbindstr)) {
        bad_func = "quo_stringify_cbind";
        goto out;
    }
    if (QUO_SUCCESS != quo_nnodes(c->quo, &c->nnodes)) {
        bad_func = "quo_nnodes";
        goto out;
    }
    if (QUO_SUCCESS != quo_nnoderanks(c->quo, &c->nnoderanks)) {
        bad_func = "quo_nnoderanks";
        goto out;
    }
    if (QUO_SUCCESS != quo_noderank(c->quo, &c->noderank)) {
        bad_func = "quo_noderank";
        goto out;
    }
out:
    if (bad_func) {
        fprintf(stderr, "%s: %s failure :-(\n", __func__, bad_func);
        return 1;
    }
    return 0;
}

static int
emit_node_basics(const context_t *c)
{
    demo_emit_sync(c);
    /* one proc per node will emit this info */
    if (0 == c->noderank) {
        printf("### [rank %d] quo version: %d.%d ###\n",
                c->rank, c->qv, c->qsv);
        printf("### [rank %d] nnodes: %d\n", c->rank, c->nnodes);
        printf("### [rank %d] nnoderanks: %d\n", c->rank, c->nnoderanks);
        printf("### [rank %d] nsockets: %d\n", c->rank, c->nsockets);
        printf("### [rank %d] ncores: %d\n", c->rank, c->ncores);
        printf("### [rank %d] npus: %d\n", c->rank, c->npus);
        fflush(stdout);
    }
    demo_emit_sync(c);
    return 0;
}

/**
 * this is where we set our policy regarding who will actually call into p1 and
 * do work. the others will sit in a barrier an wait for the workers to finish.
 *
 * this particular example distributes the workers among all the sockets on the
 * system, but you can imagine doing the same for NUMA nodes, for example.
 */
static int
elect_workers(context_t *c,
              bool *working,
              quo_obj_type_t *obj_if_working,
              int *obj_index_if_working)
{
    /* points to an array that stores the number of elements in the
     * rank_ids_bound_to_socket array at a particular socket index */
    int *nranks_bound_to_socket = NULL;
    /* array of pointers that point to the smp ranks that cover a particular
     * socket at a particular socket index. you can think of this as a 2D
     * matrix where [i][j] is the ith socket that smp rank j covers. */
    int **rank_ids_bound_to_socket = NULL;
    int rc = QUO_ERR;
    /* set some defaults -- "bind to what" info only valid if working */
    *obj_if_working = QUO_MACHINE; *obj_index_if_working = 0;
    /* allocate some memory for our arrays */
    nranks_bound_to_socket = calloc(c->nsockets, sizeof(*nranks_bound_to_socket));
    if (!nranks_bound_to_socket) return 1;
    /* allocate pointer array */
    rank_ids_bound_to_socket = calloc(c->nsockets,
                                      sizeof(*rank_ids_bound_to_socket));
    if (!rank_ids_bound_to_socket) {
        free(nranks_bound_to_socket); nranks_bound_to_socket = NULL;
        return 1;
    }
    /* grab the smp ranks (node ranks) that are in each socket */
    for (int socket = 0; socket < c->nsockets; ++socket) {
        rc = quo_smpranks_in_type(c->quo,
                                  QUO_SOCKET,
                                  socket,
                                  &(nranks_bound_to_socket[socket]),
                                  &(rank_ids_bound_to_socket[socket]));
        if (QUO_SUCCESS != rc) goto out;
    }
    /* everyone display the list of smp ranks that cover each socket on the
     * system. */
    for (int socket = 0; socket < c->nsockets; ++socket) {
        for (int rank = 0; rank < nranks_bound_to_socket[socket]; ++rank) {
            printf("### [rank %d] rank %d covers socket %d\n", c->rank,
                   rank_ids_bound_to_socket[socket][rank], socket);
        }
    }
    demo_emit_sync(c);
    /* ////////////////////////////////////////////////////////////////////// */
    /* now elect the workers. NOTE: the setup for this was fairly ugly and not
     * cheap, so caching the following result may be a good thing.
     * o setup.
     * o elect workers for a particular regime.
     * o cache that result for later use to avoid setup and query costs.
     */
    /* ////////////////////////////////////////////////////////////////////// */
    /* maximum number of workers for a given resource (socket in this case). we
     * are statically setting this number, but one could imagine this number
     * being exchanged at the beginning of time in a real application. */
    int max_workers_per_res = 2;
    /* whether or not we are already assigned to a particular resource */
    bool res_assigned = false;
    for (int socket = 0; socket < c->nsockets; ++socket) {
        for (int rank = 0; rank < nranks_bound_to_socket[socket]; ++rank) {
            /* if i'm not already assigned to a particular resource and
             * my current cpuset covers the resource in question and
             * someone else won't be assigned to that resource
             */
            if (!res_assigned&&
                c->noderank == rank_ids_bound_to_socket[socket][rank] &&
                rank < max_workers_per_res) {
                res_assigned = true;
                printf("### [rank %d] smp rank %d assigned to socket %d\n",
                        c->rank, c->noderank, socket);
                *obj_if_working = QUO_SOCKET;
                *obj_index_if_working = socket;
            }
        }
    }
    *working = res_assigned;
    demo_emit_sync(c);
out:
    /* the resources returned by quo_smpranks_in_type must be freed by us */
    for (int i = 0; i < c->nsockets; ++i) {
        if (rank_ids_bound_to_socket[i]) free(rank_ids_bound_to_socket[i]);
    }
    free(rank_ids_bound_to_socket);
    free(nranks_bound_to_socket);
    return 0;
}

static int
enter_p1(context_t *c)
{
    bool working = false;
    quo_obj_type_t target_res = QUO_MACHINE;
    int res_index = 0;
    /* ////////////////////////////////////////////////////////////////////// */
    /* now enter into the other library so it can do its thing... */
    /* ////////////////////////////////////////////////////////////////////// */
    if (elect_workers(c, &working, &target_res, &res_index)) return 1;
    if (working) {
        if (p1_entry_point(c)) return 1;
        /* signal completion */
        if (MPI_SUCCESS != MPI_Barrier(MPI_COMM_WORLD)) return 1;
    }
    else {
        /* everyone else wait for the workers to complete their thing... */
        if (MPI_SUCCESS != MPI_Barrier(MPI_COMM_WORLD)) return 1;
    }
    return 0;
}

int
main(void)
{
    int erc = EXIT_SUCCESS;
    char *bad_func = NULL;
    context_t *context = NULL;

    /* ////////////////////////////////////////////////////////////////////// */
    /* init code -- note that the top-level package must do this */
    /* ////////////////////////////////////////////////////////////////////// */
    if (init(&context)) {
        bad_func = "init";
        goto out;
    }

    /* ////////////////////////////////////////////////////////////////////// */
    /* libquo is now ready for service and mpi is good to go. */
    /* ////////////////////////////////////////////////////////////////////// */

    /* first gather some info so we can base our decisions on our current
     * situation. */
    if (sys_grok(context)) {
        bad_func = "sys_grok";
        goto out;
    }
    /* show some info that we got about our nodes - one per node */
    if (emit_node_basics(context)) {
        bad_func = "emit_node_basics";
        goto out;
    }
    /* display our binding */
    if (emit_bind_state(context)) {
        bad_func = "emit_bind_state";
        goto out;
    }

    /* ////////////////////////////////////////////////////////////////////// */
    /* now elect some subset of the mpi processes to to work in p1 */
    /* ////////////////////////////////////////////////////////////////////// */
    if (enter_p1(context)) {
        bad_func = "p1_entry_point";
        goto out;
    }
out:
    if (NULL != bad_func) {
        fprintf(stderr, "XXX %s failure in: %s\n", __FILE__, bad_func);
        erc = EXIT_FAILURE;
    }
    (void)fini(context);
    return erc;
}