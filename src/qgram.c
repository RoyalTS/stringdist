/*  stringdist - a C library of string distance algorithms with an interface to R.
 *  Copyright (C) 2013  Mark van der Loo
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 *
 *  You can contact the author at: mark _dot_ vanderloo _at_ gmail _dot_ com
 */


#define USE_RINTERNALS
#include <stdlib.h>
#include <string.h>
#include <R.h>
#include <Rdefines.h>
#include "utils.h"


/* binary tree; dictionary of qgrams */

typedef struct qnode {
  unsigned int *qgram; // the q-gram.
  double *n;           // (vector of) counts.
  struct qnode *left;
  struct qnode *right;
} qtree;




/* -- Simple memory allocator to store nodes of the qtree -- */


/* Nodes are stored in boxes which are stored on a shelve.
 * Every time a new box is added to the shelve, the capacity 
 * for node storage doubles, unless MAXBOXES is surpassed.
 */
#define MAXBOXES 20           // max number of boxes
#define MIN_BOX_SIZE (1<<10)  // nr of nodes in initial box

// A Box of nodes.
typedef struct {
  
  int nnodes; // number of nodes
  int nalloc; // number of nodes allocated

  // Room in the box.
  unsigned int  *intblocks; // store qgrams
  double        *dblblocks; // store qgram-counts
  qtree         *qtrblocks; // store nodes
} Box;


static Box *new_box(int nnodes, int q, int nstr){
  Box *b = malloc(sizeof(Box));
  if ( b == NULL ) 
    return NULL;
  b->nnodes     = nnodes;
  b->nalloc     = 0L;
  b->intblocks  = (unsigned int *) malloc(sizeof(int) * nnodes * q);
  b->dblblocks  = (double *) malloc(sizeof(double) * nnodes * nstr);
  b->qtrblocks  = (qtree *) malloc(sizeof(qtree) * nnodes);
  return b;
}

static void free_box(Box *box){
  free( box->intblocks);
  free( box->dblblocks);
  free( box->qtrblocks);
  free( box);
}

// A shelve can store up to MAXBOXES Boxes.
typedef struct {
  Box *box[MAXBOXES];
  int nboxes;         // number of boxes on the shelf
  int q;              // the q in q-gram
  int nstr;           // the number of stings compared
} Shelve;

// one shelve for all.
static Shelve shelve;

static void init_shelve(int q, int nstr){
  shelve.q = q;
  shelve.nstr = nstr;
  shelve.nboxes = 0L;
  for ( int i=0; i<MAXBOXES; i++ )
    shelve.box[i] = NULL;
}

/* add box to shelve
 *
 * return values:
 * 0: okidoki
 * 1: no luck: out of memory or MAXBOXES exceeded
 *
 */
static int add_box(int nnodes){

  // is there room for another box?
  if ( shelve.nboxes >= MAXBOXES ) return 1;

  Box *b = new_box(nnodes, shelve.q, shelve.nstr);
  if ( b != NULL ){
    shelve.box[shelve.nboxes] = b;
    shelve.nboxes++;
    return 1L;
  } else {
    return 0L;
  }
  
}

static void clear_shelve(){
  for ( int i = 0; i < shelve.nboxes; i++ ){
    free_box(shelve.box[i]);
  }
  shelve.nboxes=0L;
}

/* the allocator can store ints (q-grams), doubles (counts, per string) 
 * and qtree (nodes).
 */
typedef enum { uInt, Double, Qtree } type;


// cf. n1256.pdf (C99 std) sect 6.3.2.3 for pointer conversion.
static void *alloc(type t){

  if ( 
    shelve.nboxes == 0L &&
    !add_box(MIN_BOX_SIZE)
  ) return NULL;

  Box *box = shelve.box[shelve.nboxes-1L];
  if ( box->nalloc == box->nnodes ){
    // add box such that storage size is doubled.
    if (
      !add_box( (1 << (shelve.nboxes-1L)) * MIN_BOX_SIZE )
    ) return NULL;
    box = shelve.box[shelve.nboxes-1L];
  }

  void *x;
  switch ( t){
    case uInt:
      x = (void *) (box->intblocks + box->nalloc * shelve.q);
      break;
    case Double:
      x = (void *) (box->dblblocks + box->nalloc * shelve.nstr);
      break;
    case Qtree:

      x = (void *) (box->qtrblocks + box->nalloc);
      box->nalloc += 1;
      break;
    default:
      return NULL;
      break;
  }
  
  return x;
}


/* -- END OF ALLOCATOR -- */


/* Lexicographical comparison of two qgrams.
 * output:
 * -1 : q1 < q2
 *  0 : q1 = q2
 *  1 : q1 > q2
 */
static int compare(unsigned int *q1, unsigned int *q2, int q){
  if (q==0) return 0;
  if (q1[0] > q2[0]) return 1;
  if (q1[0] < q2[0]) return -1;
  return compare( q1 + 1, q2 + 1, q - 1 );
}

// helper functions
static qtree *new_qtree(int q, int nstr){
  init_shelve(q, nstr);
  return NULL;
}

static void free_qtree(){
  clear_shelve();
}


/* push qgram into binary tree
 * 
 * qtree : see above
 * qgram : see above
 * q     : the 'q' in q-gram
 * iLoc  : To wich count location does this q-gram contribute?
 * nLoc  : how many locations are there?
 */
static qtree *push(qtree *Q, unsigned int *qgram, unsigned int q, int iLoc, int nLoc ){
  int cond;  
  if( Q == NULL ){ // new qgram
    Q = (qtree *) alloc( Qtree);
    if ( Q == NULL ) return NULL;
    Q->qgram = (unsigned int *) alloc( uInt);
    if (Q->qgram == NULL ) return NULL;

    Q->n = (double *) alloc( Double);
    if (Q->n == NULL) return NULL;
    for (int i=0; i<nLoc; ++i) Q->n[i] = 0.0;

    Q->n[iLoc]++;
    memcpy(Q->qgram, qgram, sizeof(int) * q);
    Q->left = NULL;
    Q->right= NULL;

  } else if ( ( cond = compare(qgram, Q->qgram, q) ) == 1)  { // qgram larger than the stored qgram
    Q->left = push(Q->left, qgram, q, iLoc, nLoc);
  } else if ( cond == -1 ){ // qgram smaller than the stored qgram
    Q->right = push(Q->right, qgram, q, iLoc, nLoc);
  } else { // qgram equal to stored qgram
    Q->n[iLoc] += 1;
  }
  return Q;
}

/* push qgrams of a string into binary tree */
static qtree *push_string(unsigned int *str, int strlen, unsigned int q, qtree *Q, int iLoc, int nLoc){
  qtree *P;
  for ( int i=0; i < strlen - q + 1; ++i ){
    P = push(Q, str + i, q, iLoc, nLoc);
    if ( P == NULL ){ 
      free_qtree();
      return NULL;
    }
    Q = P;
  }
  return Q;
}



/* The real work starts here */

/* get qgram-distance from tree and set all qgram-freqencies 
 * to 0 (so the tree can be reused).
 */
static void getdist(qtree *Q, double *d){
  if (Q == NULL) return;
  d[0] += (double) abs(Q->n[0] - Q->n[1]);
  Q->n[0] = 0;
  Q->n[1] = 0;
  getdist(Q->left, d);
  getdist(Q->right,d);
}

/* get x.y,||x||and ||y|| for cosine distance from the tree and set all qgram-freqencies 
 * to 0 so the tree van be reused.
 */
static void getcosine(qtree *Q, double *d){
  if ( Q == NULL ) return;
  // inner product
  d[0] += (double) Q->n[0] * Q->n[1];
  // norm of v(s,q)
  d[1] += (double) Q->n[0]*Q->n[0];
  d[2] += (double) Q->n[1]*Q->n[1];
  // clean up and continue
  Q->n[0] = 0;
  Q->n[1] = 0;
  getcosine(Q->left,d);
  getcosine(Q->right,d);
}

/* get jaccard distance from the tree and set all qgram-freqencies 
 * to 0 so the tree van be reused.
 */
static void getjaccard(qtree *Q, double *d){
  if ( Q == NULL ) return;
  // numerator: |x A y|
  if ( Q->n[0] > 0 && Q->n[1] > 0){
    ++d[0];
  } 
  // denominator: |x V y|
  ++d[1];
  // clean up and continue
  Q->n[0] = 0;
  Q->n[1] = 0;
  getjaccard(Q->left,d);
  getjaccard(Q->right,d);
}



/*Get qgram distances 
 * Input
 * s: a string
 * t: a string
 * x: length of s
 * y: length of t
 * q: the 'q' in q-gram
 * Q: a qtree
 * int: distance distance function to compute:
 *  0 : q-gram distance
 *  1 : cosine distance
 *  2 : jaccard distance
 *
 *
 * Return values:
 *  >=0 : qgram distance
 * -1   : infinite distance
 * -2   : Not enough memory
 */
static double qgram_tree(
    unsigned int *s, 
    unsigned int *t, 
    unsigned int x,
    unsigned int y,
    unsigned int q, 
    qtree *Q,
    int distance
  ){
  // return -1 when q is larger than the length of the shortest string.
  if ( q > (x <= y ? x : y) ) return -1.0;
  // rare edge cases.
  if ( q == 0 ){
    if ( x + y > 0 ){ // distance undefined
      return -1.0;
    } else { // x == y == 0.
      return 0.0;
    } 
  }

  double dist[3] = {0,0,0};
  Q = push_string(s, x, q, Q, 0, 2);
  if (Q == NULL) return -2.0;
  Q = push_string(t, y, q, Q, 1, 2);
  if (Q == NULL) return -2.0;

  switch ( distance ){
    case 0:
      getdist(Q,dist);
      break;
    case 1:
      getcosine(Q, dist);
      if (dist[0]==dist[1] && dist[0]==dist[2]){
        // strings are equal. Prevent machine rounding about 0.0
        dist[0] =  0.0;
      } else {
        // there are several ways to express the rhs (including ones that give 0L 
        // at equal strings) but this has least chance of overflow.
        dist[0] = 1.0 - dist[0]/(sqrt(dist[1]) * sqrt(dist[2]));
      }
      break;
    case 2:
      getjaccard(Q,dist);
      dist[0] = 1.0 - dist[0]/dist[1];
      break;
    default:
      break;
  }
  return dist[0];
}

/* R interface to qgram distance */
SEXP R_qgram_tree(SEXP a, SEXP b, SEXP qq, SEXP distance){
  PROTECT(a);
  PROTECT(b);
  PROTECT(qq);
  PROTECT(distance);
  // choose distance function

  int dist = INTEGER(distance)[0]
    , q = INTEGER(qq)[0]
    , na = length(a)
    , nb = length(b)
    , ml_a = max_length(a)
    , ml_b = max_length(b)
    , bytes = IS_CHARACTER(a);

  // set up a qtree; 
  qtree *Q = new_qtree(q, 2L);
  unsigned int *s = NULL, *t = NULL;
  if ( bytes ){
    s = (unsigned int *) malloc( (ml_a + ml_b) * sizeof(int) );
    if ( s == NULL ){ 
      UNPROTECT(4);
      error("Unable to allocate enough memory");
    }
    t = s + ml_a;
  }

  // output
  int nt = (na > nb) ? na : nb;
  SEXP yy; 
  PROTECT(yy = allocVector(REALSXP, nt));
  double *y = REAL(yy);

  
 
  int i=0, j=0, len_s, len_t, isna_s, isna_t;
  for ( int k=0; k < nt; ++k 
      , i = RECYCLE(i+1,na)
      , j = RECYCLE(j+1,nb) ){

    s = get_elem(a, i, bytes, &len_s, &isna_s, s);
    t = get_elem(b, j, bytes, &len_t, &isna_t, t);

    if ( isna_s || isna_t ){
      y[k] = NA_REAL;
      continue;
    }
    y[k] = qgram_tree(s, t, len_s, len_t, q, Q, dist);
    if (y[k] == -2.0){
      UNPROTECT(5);
      error("Unable to allocate enough memory");
    }
    if (y[k] == -1.0){
      y[k] = R_PosInf;
    }
  }


  free_qtree();
  if ( bytes ) free(s);
  UNPROTECT(5);
  return yy;
}


//-- Match function interface with R

SEXP R_match_qgram_tree(SEXP x, SEXP table, SEXP nomatch, SEXP matchNA, SEXP qq, SEXP maxDist, SEXP distance){
  PROTECT(x);
  PROTECT(table);
  PROTECT(nomatch);
  PROTECT(matchNA);
  PROTECT(qq);
  PROTECT(maxDist);
  PROTECT(distance);

  double max_dist = REAL(maxDist)[0] == 0.0 ? R_PosInf : REAL(maxDist)[0];
  
  
  int dist = INTEGER(distance)[0] // choose distance function
    , q = INTEGER(qq)[0]
    , nx = length(x)
    , ntable = length(table)
    , no_match = INTEGER(nomatch)[0]
    , match_na = INTEGER(matchNA)[0]
    , bytes = IS_CHARACTER(x)
    , ml_x = max_length(x)
    , ml_t = max_length(table);
  
  // set up a qtree;
  qtree *Q = new_qtree(q, 2);

  unsigned int *X = NULL, *T = NULL;
  if (bytes){
    X = (unsigned int *) malloc( (ml_x + ml_t) * sizeof(int));
    if ( X == NULL){
      UNPROTECT(7);
      error("Unable to allocate enough memory");
    }
    T = X + ml_x;
  }

  // output vector
  SEXP yy;
  PROTECT(yy = allocVector(INTSXP, nx));
  int *y = INTEGER(yy);


  double d = R_PosInf, d1 = R_PosInf;
  int index, isna_X, isna_T, len_X, len_T;

  for ( int i=0; i<nx; i++){
    index = no_match;
    X = get_elem(x, i, bytes, &len_X, &isna_X, X);
    d1 = R_PosInf;
    for ( int j=0; j<ntable; j++){

      T = get_elem(table, j, bytes, &len_T, &isna_T,T);

      if ( !isna_X && !isna_T ){        // both are char (usual case)
        d = qgram_tree(
          X, T, len_X, len_T, q, Q, dist
        );
        if ( d == -2.0 ){
          UNPROTECT(7);
          error("Unable to allocate enough memory for qgram storage");
        }
        if ( d > max_dist ){
          continue;
        } else if ( d > -1 && d < d1){ 
          index = j + 1;
          if ( abs(d) < 1e-14 ) break; 
          d1 = d;
        }
      } else if ( isna_X && isna_T ) {  // both are NA
        index = match_na ? j + 1 : no_match;
        break;
      }
    }
    
    y[i] = index;
  } 
  if ( bytes ) free(X);
  free_qtree();
  UNPROTECT(8);
  return(yy);
}


/* R interface to qgram tabulator */

static void count_qtree(qtree *Q, int *n){
  if (Q == NULL ) return ;
  n[0]++;
  count_qtree(Q->left,n);
  count_qtree(Q->right,n);
}

static void get_counts( qtree *Q, int q, int *qgrams, int nLoc, int *index, double *count ){
  if ( Q == NULL ) return ;
  memcpy(qgrams + q*index[0], Q->qgram, sizeof(int) * q);
  memcpy(count + nLoc*index[0], Q->n, sizeof(double) * nLoc);
  ++index[0];
  get_counts(Q->left, q, qgrams, nLoc, index, count);
  get_counts(Q->right,q, qgrams, nLoc, index, count);
}

/* 
 * 
 * 
 */
SEXP R_get_qgrams(SEXP a, SEXP qq){
  PROTECT(a);
  PROTECT(qq);

  int q = INTEGER(qq)[0];

  if ( q < 0 ){
    UNPROTECT(2);
    error("q must be a nonnegative integer");
  }


  SEXP strlist;
  int nstr, nchar, nLoc = length(a);
  unsigned int *str;
  
  // set up a tree; push all the qgrams.
  qtree *Q = new_qtree( q, nLoc);
  
  for ( int iLoc = 0; iLoc < nLoc; ++iLoc ){
    strlist = VECTOR_ELT(a, iLoc);
    nstr    = length(strlist);
 
    for ( int i=0; i < nstr; ++i ){
      str   = (unsigned int *) INTEGER(VECTOR_ELT(strlist,i));
      nchar = length(VECTOR_ELT(strlist,i));
      if ( str[0] == NA_INTEGER 
          || q > nchar
          || ( q == 0 && nchar > 0 )
        ){
        continue ;
      }
      Q = push_string(str, nchar, q, Q, iLoc, nLoc);
      if ( Q == NULL ){
        UNPROTECT(2);
        error("could not allocate enough memory");
      }
    }
  }
  // pick and delete the tree

  int nqgram[1] = {0};

  // helper variable for get_counts 
  int index[1] = {0};

  count_qtree(Q,nqgram);  

  SEXP qgrams, qcount;
  PROTECT(qgrams = allocVector(INTSXP, q*nqgram[0]));
  PROTECT(qcount = allocVector(REALSXP, nLoc*nqgram[0]));

  get_counts(Q, q, INTEGER(qgrams), nLoc, index, REAL(qcount));
  
  setAttrib(qcount, install("qgrams"), qgrams);
  
  free_qtree();
  UNPROTECT(4);

  return(qcount);
}



