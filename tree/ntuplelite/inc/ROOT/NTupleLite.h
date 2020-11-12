/// \file ROOT/NTupleLite.h
/// \ingroup NTuple ROOT7
/// \author Jakob Blomer <jblomer@cern.ch>
/// \date 2020-10-27
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2020, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT7_NTupleLite
#define ROOT7_NTupleLite

#include <stdint.h>


#define ROOT_NTPL_ID uint64_t
#define ROOT_NTPL_SIZE uint64_t

// Column types
#define ROOT_NTPL_TYPE_OFFSET      1
#define ROOT_NTPL_TYPE_FLOAT       2
#define ROOT_NTPL_TYPE_DOUBLE      3
// ...

// Error codes
#define ROOT_NTPL_ERR_INVALID_ID   0
#define ROOT_NTPL_ERR_UNKOWN       1
// ...

#ifdef __cplusplus
extern "C" {
#endif

struct ROOT_rawfile;
struct ROOT_ntpl;

struct ROOT_ntpl_column_list;
struct ROOT_ntpl_cluster_list;
struct ROOT_ntpl_field_list;
struct ROOT_ntpl_page_list;

struct ROOT_ntpl_column {
  ROOT_ntpl_column_list *next;
  ROOT_NTPL_ID id;
  int type;
};

struct ROOT_ntpl_field {
  ROOT_ntpl_field_list *next;
  ROOT_NTPL_ID id;
  char *name;
  char *type;
  ROOT_ntpl_field_list *parent;
  ROOT_ntpl_column_list *columns;
};

struct ROOT_ntpl_cluster {
  ROOT_ntpl_cluster_list *next;
  ROOT_NTPL_ID id;
  ROOT_NTPL_SIZE first_entry;
  ROOT_NTPL_SIZE nentries;
};

struct ROOT_ntpl_page {
  ROOT_ntpl_page_list *next;
  ROOT_NTPL_ID id;
  ROOT_NTPL_SIZE first_element;
  ROOT_NTPL_SIZE nelements;
};

struct ROOT_ntpl_page_buffer {
  void *buffer;
  ROOT_NTPL_SIZE first_element;
  ROOT_NTPL_SIZE nelements;
};


ROOT_rawfile *ROOT_rawfile_open(const char *locator);

int ROOT_rawfile_error(ROOT_rawfile *rawfile);

void ROOT_rawfile_close(ROOT_rawfile *rawfile);

// ROOT_ntpl * and derived objects are thread friendly. They can be used from multiple threads but
// not concurrently.  Multiple ROOT_ntpl * objects can be opened for the same RNTuple.
ROOT_ntpl *ROOT_ntpl_open(ROOT_rawfile *rawfile, const char *path);

int ROOT_ntpl_error(ROOT_ntpl *ntpl);

void ROOT_ntpl_close(ROOT_ntpl *ntpl);

// Meta-data
ROOT_ntpl_field *ROOT_ntpl_list_fields(ROOT_ntpl *ntpl);

ROOT_ntpl_cluster *ROOT_ntpl_list_clusters(ROOT_ntpl *ntpl);

ROOT_ntpl_page *ROOT_ntpl_list_pages(ROOT_NTPL_ID column_id, ROOT_NTPL_ID cluster_id);

void ROOT_ntpl_list_free(void *list);

ROOT_ntpl_page_buffer ROOT_ntpl_page_get(ROOT_NTPL_ID column_id, ROOT_NTPL_ID cluster_id, ROOT_NTPL_ID page_id);

ROOT_ntpl_page_buffer ROOT_ntpl_page_find(ROOT_NTPL_ID column_id, ROOT_NTPL_ID cluster_id,
                                          ROOT_NTPL_SIZE element_index);

// Releases (internal) resources acquired by ROOT_ntpl_page_get()
void ROOT_ntpl_page_release(ROOT_ntpl_page_buffer buffer);

#ifdef __cplusplus
}
#endif

#endif // ROOT7_NTupleLite.h
