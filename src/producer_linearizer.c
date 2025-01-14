// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#define _GNU_SOURCE // for qsort_r in stdlib
#include "producer_linearizer.h"

#include <stdlib.h>
#include <string.h>

bool ProducerLinearizer_init(ProducerLinearizer *pl, uint64_t sz) {
  if (!pl || !sz)
    return false;
  pl->A = pl->I = NULL; // to prevent free() issues on error
  pl->F = NULL;

  // allocate storage
  if (!(pl->I = malloc(sz * sizeof(*pl->I))))
    goto PLINIT_ERR_CLEANUP;
  if (!(pl->A = malloc(sz * sizeof(*pl->A))))
    goto PLINIT_ERR_CLEANUP;
  if (!(pl->F = malloc(sz * sizeof(*pl->F))))
    goto PLINIT_ERR_CLEANUP;

  // Initialize storage
  memset(pl->F, 1, sizeof(*pl->F) * sz);
  for (uint64_t i = 0; i < sz; i++) {
    pl->I[i] = i;
    pl->A[i] = UINT64_MAX;
  }

  // Initialize other fields
  pl->sz = sz;
  pl->freecount = sz;
  return true;

PLINIT_ERR_CLEANUP:
  ProducerLinearizer_free(pl);
  return false;
}

void ProducerLinearizer_free(ProducerLinearizer *pl) {
  free(pl->I);
  free(pl->F);
  free(pl->A);
  memset(pl, 0, sizeof(*pl));
}

bool ProducerLinearizer_push(ProducerLinearizer *pl, uint64_t i, uint64_t v) {
  if (i >= pl->sz)
    return false;

  // Only allowed to push to free slots
  if (!pl->F[i])
    return false;

  pl->A[i] = v;     // Update value
  pl->F[i] = false; // Update free list
  --pl->freecount;  // Update free count
  pl->cursor = 0;   // reposition cursor to head (see pop)
  return true;
}

int PL_cmp(const void *L, const void *R, void *state) {
  ProducerLinearizer *pl = (ProducerLinearizer *)state;
  uint64_t i_L = *(uint64_t *)L;
  uint64_t i_R = *(uint64_t *)R;
  uint64_t v_L = pl->A[i_L];
  uint64_t v_R = pl->A[i_R];

  // Free elements are larger than any value.  We allow two frees to compare
  // equally to let the sorting algorithm optimize accordingly.
  if (pl->F[i_L] && pl->F[i_R]) // For the sake of optimization, inf == inf
    return 0;
  if (pl->F[i_L]) // L is infinity, so L > R -> return 1
    return 1;
  if (pl->F[i_R]) // R is infinity, so R > L -> return -1
    return -1;

  // Otherwise, standard comparison applies.
  return v_L == v_R ? 0 : v_L < v_R ? -1 : 1;
}

// This is a modestly tuned implementation.  It could be made much better by
// swapping out qsort_r() for an online sorting method (such as one based on
// insertionsort or timsort), which would not sort free slots.
#include <stdio.h>
bool ProducerLinearizer_pop(ProducerLinearizer *pl, uint64_t *ret) {
  // If all items are free, then we have nothing to offer
  if (pl->sz == pl->freecount)
    return false;

  // If the cursor is set to zero, that means we haven't popped since the
  // last sort operation.  Sort the indices by value.
  if (!pl->cursor)
    qsort_r(pl->I, pl->sz, sizeof(*pl->I), PL_cmp, pl);

  // We're done, give the user the corresponding index and set it to free
  *ret = pl->I[pl->cursor];
  pl->F[*ret] = true;
  ++pl->freecount;
  ++pl->cursor;
  return true;
}
