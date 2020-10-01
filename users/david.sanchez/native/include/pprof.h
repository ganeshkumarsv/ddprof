#ifndef _H_pprof
#define _H_pprof

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <zlib.h>
#include <proto/profile.pb-c.h>

#include "procutils.h"

// TODO
// * We use the free-null pattern, but could+should move setting to the helper funs

/******************************************************************************\
|*                            String Table (Vocab)                            *|
\******************************************************************************/
#define VOCAB_SZ 4096
char*  d_vocab[VOCAB_SZ] = {0};
size_t n_d_vocab         = VOCAB_SZ;

size_t addToVocab(char* str, char*** _st, size_t* _sz_st) {
  char** st    = *_st;
  size_t sz_st = *_sz_st;

  // If given a null pointer, use the null string
//  if(!str)
//    str = "";

  // Does this string already exist in the table?
  for(size_t i = 0; i < sz_st; i++)
    if(!strcmp(st[i], str))
      return i;

  // We have a new string.  Resize the string table if needed.
  if(!(sz_st%VOCAB_SZ)) {
    printf("Resizing: %ld\n", sz_st);
    char** buf = calloc(sz_st + VOCAB_SZ, sizeof(char*));
    if(!buf) {}  // TODO do something?
    memcpy(buf, st, sz_st*sizeof(char*));
    free(*_st);
    st = *_st = buf;
  }

  st[sz_st] = strdup(str);
  (*_sz_st)++;
  return sz_st;
}


/******************************************************************************\
|*                              pprof interface                               *|
\******************************************************************************/
uint64_t pprof_mapAdd(Perftools__Profiles__Profile*, uint64_t, uint64_t, char*);
uint64_t pprof_funAdd(Perftools__Profiles__Profile*, uint64_t);
uint64_t pprof_locAdd(Perftools__Profiles__Profile*, uint64_t, pid_t);
uint64_t pprof_lineAdd(Perftools__Profiles__Profile*, Perftools__Profiles__Location*, uint64_t, int64_t);
char     pprof_sampleAdd(Perftools__Profiles__Profile*, int64_t, uint64_t*, size_t, pid_t);
void     pprof_sampleMakeStack(Perftools__Profiles__Profile*, Perftools__Profiles__Sample*, uint64_t*, size_t, pid_t);

// (con/de)structors
char     pprof_Init(Perftools__Profiles__Profile*);
char     pprof_Free(Perftools__Profiles__Profile*);

// These should be internal
uint64_t pprof_mapNew(Perftools__Profiles__Profile*, uint64_t, uint64_t, char*);
uint64_t pprof_funNew(Perftools__Profiles__Profile*, uint64_t);
uint64_t pprof_locNew(Perftools__Profiles__Profile*, uint64_t, pid_t);
uint64_t pprof_lineNew(Perftools__Profiles__Profile*, Perftools__Profiles__Location*, uint64_t, int64_t);
char     pprof_sampleNew(Perftools__Profiles__Profile*, int64_t, uint64_t*, size_t, pid_t);

#define CHUNK_PPROF_LIST 1024
#ifdef KNOCKOUT_UNUSED
#  define UNUSED(x) (void)(x)   // TODO :) check these out
#else
#  define UNUSED(x) do {} while(0)
#endif
Perftools__Profiles__Profile g_dd_pprofs[1] = {0};

size_t pprof_strIntern(Perftools__Profiles__Profile* pprof, char* str) {
  return addToVocab(str, &pprof->string_table, &pprof->n_string_table);
}

uint64_t pprof_mapNew(Perftools__Profiles__Profile* pprof, uint64_t addr_start, uint64_t addr_end, char* filename) {
  uint64_t id = pprof->n_mapping;
  if(!id%VOCAB_SZ) {
    printf("Resizing pprof mapping.\n");
    Perftools__Profiles__Mapping** buf = calloc(pprof->n_mapping + VOCAB_SZ, sizeof(Perftools__Profiles__Mapping*));
    if(!buf) {} // TODO wat
    if(pprof->mapping) {
      memcpy(buf, pprof->mapping, pprof->n_mapping*sizeof(Perftools__Profiles__Mapping*));
      free(pprof->mapping);
    }
    pprof->mapping = buf;
  }

  // Initialize this mapping
  pprof->mapping[id] = calloc(1, sizeof(Perftools__Profiles__Mapping));
  if(!pprof->mapping[id]) {} // TODO error
  perftools__profiles__mapping__init(pprof->mapping[id]);


  // Populate specific mapping
  pprof->mapping[id]->id = id+1;
  pprof->mapping[id]->memory_start = addr_start;
  pprof->mapping[id]->memory_limit = addr_end;
  pprof->mapping[id]->file_offset  = 0;
  pprof->mapping[id]->filename = pprof_strIntern(pprof, filename);
  pprof->mapping[id]->build_id = 0; // TODO?

  printf("Added mapping %ld\n", id);
  printf("  filename: %s\n", filename);

  // Optional
  if(filename)
    pprof->mapping[id]->has_filenames = 1;

  // Done!
  pprof->n_mapping++;
  return id;
}

char isEqualMapping(Perftools__Profiles__Profile* pprof, uint64_t addr_start, uint64_t addr_end, char* path, Perftools__Profiles__Mapping* B) {
  // TODO make this more correct
  //  * safer check (especially the string part)
  //  * intern metadata about the file at time of access
  //  * do we want to resolve/disambiguate symlinks?
  if(!path || !B                                    ||
     strcmp(path, pprof->string_table[B->filename]) ||
     addr_start != B->memory_start                  ||
     addr_end   != B->memory_limit)
    return 0;
  return 1;
}

// Returns the ID of the mapping
// NOTE that this is different from the index of the mapping in the pprof
uint64_t pprof_mapAdd(Perftools__Profiles__Profile* pprof, uint64_t addr_start, uint64_t addr_end, char* filename) {
  for(size_t i=0; i < pprof->n_mapping; i++)
    if(isEqualMapping(pprof, addr_start, addr_end, filename, pprof->mapping[i]))
      return i+1;
  return pprof_mapNew(pprof, addr_start, addr_end, filename)+1;
}

uint64_t pprof_mapAddFromAddr(Perftools__Profiles__Profile* pprof, uint64_t addr, pid_t pid) {
  MapLine map = {0};
  if(procfs_mapMatch(pid, &map, addr) || !map.isFile) {
    // Couldn't identify the map, so we have an error
    return 0;
  }
  return pprof_mapAdd(pprof, map.start, map.end, map.path);
}

char isEqualLocation(uint64_t A, Perftools__Profiles__Location* B) {
  return A == B->address; // TODO laughably childish
}

uint64_t pprof_lineNew(Perftools__Profiles__Profile* pprof, Perftools__Profiles__Location* loc, uint64_t id_fun, int64_t line) {
UNUSED(pprof);
  uint64_t id = loc->n_line;
  if(!id%VOCAB_SZ) {
    printf("Resizing loc lines (%ld)\n", id);
    Perftools__Profiles__Line** buf = calloc(id + VOCAB_SZ, sizeof(Perftools__Profiles__Line*));
    if(!buf) {} // TODO uh what
    if(loc->line) {
      memcpy(buf, loc->line, id*sizeof(Perftools__Profiles__Line*));
      free(loc->line);
    }
    loc->line = buf;
  }
  loc->line[id] = calloc(1, sizeof(Perftools__Profiles__Line));
  if(!loc->line[id]) {} // TODO error
  perftools__profiles__line__init(loc->line[id]);

  // Populate this entry
  loc->line[id]->line        = line;
  loc->line[id]->function_id = id_fun;

  // Done!
  loc->n_line++;
  return id;
}

// Returns the ID of the line
// NOTE that this is different from the index of the line in the location
uint64_t pprof_lineAdd(Perftools__Profiles__Profile* pprof, Perftools__Profiles__Location* loc, uint64_t addr, int64_t line) {
  // Figure out the calling function
  uint64_t id_fun = pprof_funAdd(pprof, addr);

  // Right now, assume line -1 of a given function.  Need to lookup the function to check for equality
  for(size_t i = 0; i < loc->n_line; i++)
    if(id_fun == loc->line[i]->function_id && line == loc->line[i]->line)
      return i+1;

  return pprof_lineNew(pprof, loc, id_fun, line)+1;
}

// ASSERT: funNew is never accessed directly, only by funAdd
// TODO:   make this more explicit...
uint64_t pprof_funNew(Perftools__Profiles__Profile* pprof, uint64_t id_name) {
  uint64_t id = pprof->n_function;
  if(!id%VOCAB_SZ) {
    printf("Resizing pprof function.\n");
    Perftools__Profiles__Function** buf = calloc(pprof->n_function + VOCAB_SZ, sizeof(Perftools__Profiles__Function*));
    if(!buf) {} // TODO wat
    memcpy(buf, pprof->function, pprof->n_function*sizeof(Perftools__Profiles__Function*));
    free(pprof->function);
    pprof->function = buf;
  }

  // Initialize this function
  pprof->function[id] = calloc(1, sizeof(Perftools__Profiles__Function));
  if(!pprof->function[id]) {} // TODO error
  perftools__profiles__function__init(pprof->function[id]);

  // Populate a new function
  pprof->function[id]->id = id+1;
  pprof->function[id]->name = id_name;
  pprof->function[id]->system_name = id_name;
  pprof->function[id]->filename = pprof_strIntern(pprof, "[UNKNOWN]");
  pprof->function[id]->start_line = -1;

  // Done!
  pprof->n_function++;
  return id;
}

char isEqualFunction(int64_t A, Perftools__Profiles__Function* B) {
  return A == B->name;
}

uint64_t pprof_funAdd(Perftools__Profiles__Profile* pprof, uint64_t addr) {
  char funname[64] = {0};
  snprintf(funname, 59, "<0x%lx>", addr);
  uint64_t id_name = pprof_strIntern(pprof, funname);
  for(size_t i=0; i < pprof->n_function; i++)
    if(isEqualFunction(id_name, pprof->function[i]))
      return i+1;
  return pprof_funNew(pprof, id_name)+1;
}

uint64_t pprof_locNew(Perftools__Profiles__Profile* pprof, uint64_t addr, pid_t pid) {
  uint64_t id = pprof->n_location;
  if(!id%VOCAB_SZ) {
    printf("Resizing pprof location.\n");
    Perftools__Profiles__Location** buf = calloc(pprof->n_location + VOCAB_SZ, sizeof(Perftools__Profiles__Location*));
    if(!buf) {} // TODO wat
    memcpy(buf, pprof->location, pprof->n_location*sizeof(Perftools__Profiles__Location*));
    free(pprof->location);
    pprof->location = buf;
  }

  // Initialize this location
  pprof->location[id] = calloc(1, sizeof(Perftools__Profiles__Location));
  if(!pprof->location[id]) {} // TODO error
  perftools__profiles__location__init(pprof->location[id]);

  // Try to find the mapping
  uint64_t id_mapping = pprof_mapAddFromAddr(pprof, addr, pid);
  if(!id_mapping) {} // TODO this is an error?

  // Populate a new location, adding map if necessary
  pprof->location[id]->id = 1+id;
  pprof->location[id]->mapping_id = id_mapping;
  pprof->location[id]->address = addr;

  // Add a line
  pprof_lineAdd(pprof, pprof->location[id], addr, -1);

  // Done!
  pprof->n_location++;
  return id;
}

uint64_t pprof_locAdd(Perftools__Profiles__Profile* pprof, uint64_t addr, pid_t pid) {
  // TODO stop ignoring line
  for(size_t i=0; i < pprof->n_location; i++) {
    if(isEqualLocation(addr, pprof->location[i]))
      return i+1;
  }
  return pprof_locNew(pprof, addr, pid)+1;
}


char isEqualSample(uint64_t A, Perftools__Profiles__Sample* B) {
  // TODO :)
UNUSED(A);
UNUSED(B);
  return 1;
}

void pprof_sampleMakeStack(Perftools__Profiles__Profile* pprof, Perftools__Profiles__Sample* sample, uint64_t* addr, size_t sz_addr, pid_t pid) {
  sample->location_id = calloc(sz_addr, sizeof(uint64_t));
  if(!sample->location_id) {} // TODO error
  sample->n_location_id = sz_addr;

  for(size_t i=0; i < sz_addr; i++) {
    sample->location_id[i] = pprof_locAdd(pprof, addr[sz_addr-i-1], pid);
  }
}

char pprof_sampleAdd(Perftools__Profiles__Profile* pprof, int64_t val, uint64_t* addr, size_t sz_addr, pid_t pid) {
  uint64_t id = pprof->n_sample;
  // Initialize the sample, possibly expanding if needed
  if(!pprof->n_sample%VOCAB_SZ) {
    printf("Resizing pprof sample\n");
    Perftools__Profiles__Sample** buf = calloc(pprof->n_sample + VOCAB_SZ, sizeof(Perftools__Profiles__Sample*));
    if(!buf) {} // TODO wat
    if(pprof->sample) {
      printf("..Copying too!\n");
      memcpy(buf, pprof->sample, pprof->n_sample*sizeof(Perftools__Profiles__Sample*));
      free(pprof->sample);
    }
    pprof->sample = buf;
  }

  // Initialize this sample
  pprof->sample[id] = calloc(1, sizeof(Perftools__Profiles__Sample));
  if(!pprof->sample[id]) {} // TODO error
  perftools__profiles__sample__init(pprof->sample[id]);

  // Generate and stash the location IDs
  pprof_sampleMakeStack(pprof, pprof->sample[id], addr, sz_addr, pid);

  // Populate the sample value
  pprof->sample[id]->n_value = pprof->n_sample_type;
  pprof->sample[id]->value   = calloc(pprof->n_sample_type,sizeof(int64_t));
  if(!pprof->sample[id]->value) {} // TODO error
  pprof->sample[id]->value[0] = 1;
  pprof->sample[id]->value[1] = val;

  // We're done!
  pprof->n_sample++;
  return 0;
}

char pprof_sampleFree(Perftools__Profiles__Sample** sample, size_t sz) {
  if(!sample) // TODO is this an error?
    return 0;

  for(size_t i=0; i<sz; i++) {
    if(!sample[i])
      continue;

    if(sample[i]->location_id) {
      free(sample[i]->location_id);
      sample[i]->location_id = NULL;
    }

    if(sample[i]->value) {
      free(sample[i]->value);
      sample[i]->value = NULL;
    }

    free(sample[i]);
    sample[i] = NULL;
  }

  free(sample);
  return 0;
}

void pprof_timeUpdate(Perftools__Profiles__Profile* pprof) {
  if(!pprof) return;
  struct timeval tv = {0};
  gettimeofday(&tv, NULL);
  pprof->time_nanos = (tv.tv_sec*1000*1000 + tv.tv_usec)*1000;
}

void pprof_durationUpdate(Perftools__Profiles__Profile* pprof) {
  if(!pprof) return;
  struct timeval tv = {0};
  gettimeofday(&tv, NULL);
  pprof->duration_nanos = (tv.tv_sec*1000*1000 + tv.tv_usec)*1000 - pprof->time_nanos;
}

char pprof_Init(Perftools__Profiles__Profile* pprof) {
  // Initialize the top-level container and the type holders
  perftools__profiles__profile__init(pprof);
  pprof_strIntern(pprof,""); // Initialize

  // Initialize sample_type
  pprof->sample_type = calloc(2,sizeof(Perftools__Profiles__ValueType*));
  if(!pprof->sample_type) {} // TODO error

  //   Initialize the time tracker
  pprof->sample_type[0] = calloc(1,sizeof(Perftools__Profiles__ValueType));
  if(!pprof->sample_type[0]) {} // TODO error
  perftools__profiles__value_type__init(pprof->sample_type[0]);
  pprof->sample_type[0]->type = pprof_strIntern(pprof, "samples");
  pprof->sample_type[0]->unit = pprof_strIntern(pprof, "count");

  //   Initialize the count tracker
  pprof->sample_type[1] = calloc(1,sizeof(Perftools__Profiles__ValueType));
  if(!pprof->sample_type[1]) {} // TODO error
  perftools__profiles__value_type__init(pprof->sample_type[1]);
  pprof->sample_type[1]->type = pprof_strIntern(pprof, "cpu");
  pprof->sample_type[1]->unit = pprof_strIntern(pprof, "nanoseconds");

  pprof->n_sample_type = 2;
  pprof->n_sample = 0;

  // Initialize period_type
  pprof->period_type = calloc(1,sizeof(Perftools__Profiles__ValueType));
  if(!pprof->period_type) {} // TODO error
  perftools__profiles__value_type__init(pprof->period_type);
  pprof->period_type->type = pprof_strIntern(pprof, "cpu");
  pprof->period_type->unit = pprof_strIntern(pprof, "nanoseconds");

  return 0;
}

char pprof_mapFree(Perftools__Profiles__Mapping** map, size_t sz) {
  if(!map)    // TODO err?
    return 0;

  // TODO when we change all mappings to get allocated into a common arena,
  // update this
  for(size_t i=0; i<sz; i++)
    if(map[i]) {
      free(map[i]);
      map[i] = NULL;
    }

  free(map);
  map = NULL;
  return 0;
}

char pprof_lineFree(Perftools__Profiles__Line** line, size_t sz) {
  if(!line)
    return 0;

  // TODO refactor when ready
  for(size_t i=0; i<sz; i++)
    if(line[i]) {
      free(line[i]);
      line[i] = NULL;
    }

  free(line);
  line = NULL;
  return 0;
}

char pprof_locFree(Perftools__Profiles__Location** loc, size_t sz) {
  if(!loc)
    return 0;

  // TODO refactor when ready
  for(size_t i=0; i<sz; i++)
    if(loc[i]) {
      pprof_lineFree(loc[i]->line, loc[i]->n_line);
      loc[i]->line = NULL;
      free(loc[i]);
      loc[i] = NULL;
    }

  free(loc);
  loc = NULL;
  return 0;
}

char pprof_funFree(Perftools__Profiles__Function** fun, size_t sz) {
  if(!fun)
    return 0;

  // TODO refactor when ready
  for(size_t i=0; i<sz; i++)
    if(fun[i]) {
      free(fun[i]);
      fun[i] = NULL;
    }

  free(fun);
  fun = NULL;
  return 0;
}

char pprof_sampleClear(Perftools__Profiles__Profile* pprof) {
  if(!pprof)
    return 0;

  if(pprof->sample)
    pprof_sampleFree(pprof->sample, pprof->n_sample);
  pprof->sample = NULL;
  pprof->n_sample = 0;

  return 0;
}

char pprof_Free(Perftools__Profiles__Profile* pprof) {
  if(!pprof)   // Is this an error?
    return 0;

  if(pprof->sample_type) {
    for(size_t i=0; i<pprof->n_sample_type; i++)
      if(pprof->sample_type[i]) {
        free(pprof->sample_type[i]);
        pprof->sample_type[i] = NULL;
      }
    free(pprof->sample_type);
    pprof->sample_type = NULL;
  }

  if(pprof->period_type) {
    free(pprof->period_type);
    pprof->period_type = NULL;
  }

  if(pprof->mapping) {
    pprof_mapFree(pprof->mapping, pprof->n_mapping);
    pprof->mapping = NULL;
  }

  if(pprof->location) {
    pprof_locFree(pprof->location, pprof->n_location);
    pprof->location = NULL;
  }

  if(pprof->function) {
    pprof_funFree(pprof->function, pprof->n_function);
    pprof->function = NULL;
  }

  if(pprof->sample) {
    pprof_sampleFree(pprof->sample, pprof->n_sample);
    pprof->sample = NULL;
  }

  if(pprof->string_table) {
    for(size_t i=0; i<pprof->n_string_table; i++)
      if(pprof->string_table[i]) {
        free(pprof->string_table[i]);
        pprof->string_table[i] = NULL;
      }
    free(pprof->string_table);
    pprof->string_table = NULL;
  }

  if(pprof->comment) {
    free(pprof->comment);
    pprof->comment = NULL;
  }
  return 0;
}


/******************************************************************************\
|*                        Compression Helper Functions                        *|
\******************************************************************************/
void GZip(char* file, const char* data, const size_t sz_data) {
  gzFile fi = gzopen(file, "wb9");
  gzwrite(fi, data, sz_data);
  gzclose(fi);
  struct stat st = {0}; stat(file, &st);
}

size_t pprof_zip(Perftools__Profiles__Profile* pprof, unsigned char* ret, const size_t sz_packed) {
  // Assumes the ret buffer has already been sized to at least sz_packed
  // Serialized pprof
  void* packed = malloc(sz_packed); // TODO check for err?
  perftools__profiles__profile__pack(pprof, packed);
  memset(ret, 0, sz_packed);

  // Compress
  z_stream zs = {.avail_in = sz_packed,
                 .avail_out = sz_packed,
                 .next_in = packed,
                 .next_out = ret};
  deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  deflate(&zs, Z_FINISH);
  deflateEnd(&zs);
  free(packed);

  return zs.total_out;
}

#endif
