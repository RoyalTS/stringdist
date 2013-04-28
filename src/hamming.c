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
 *
 *
 * This code is gratefully based on Nick Logan's github repository
 * https://github.com/ugexe/Text--Levenshtein--Damerau--XS/blob/master/damerau-int.c
 *
 */ 



#define USE_RINTERNALS
#include <R.h>
#include <Rdefines.h>
#include "utils.h"

static int hamming(unsigned int *a, unsigned int *b, int n, int maxDistance){
   int h=0;
   for(int i=0; i<n; ++i){
      if (a[i] != b[i]) h++;
      if ( maxDistance > 0 && maxDistance < h ){
         return -1;
      }
   }
   return h;
}


// -- R interface

SEXP R_hm(SEXP a, SEXP b, SEXP maxDistance){
  PROTECT(a);
  PROTECT(b);
  PROTECT(maxDistance);

  int na = length(a)
    , nb = length(b)
    , nt = ( na > nb) ? na : nb
    , bytes = IS_CHARACTER(a)
    , maxDist = INTEGER(maxDistance)[0]
    , ml_a = max_length(a)
    , ml_b = max_length(b);

  unsigned int *s = NULL, *t = NULL;
  if ( bytes ){
    s = (unsigned int *) malloc( (ml_a + ml_b) * sizeof(int));
    if ( s == NULL ) error("Unable to allocate enough memory");
    t = s + ml_a;
  }

  SEXP yy;
  PROTECT(yy = allocVector(REALSXP,nt));
  double *y = REAL(yy);

  int i=0, j=0, k=0, len_s, len_t, isna_s, isna_t;
  for ( k=0; k<nt; 
        ++k
      , i = RECYCLE(i+1,na)
      , j = RECYCLE(j+1,nb) ){

    s = get_elem(a, i, bytes, &len_s, &isna_s, s);
    t = get_elem(b, j, bytes, &len_t, &isna_t, t);
    if ( isna_s || isna_t ){
      y[k] = NA_REAL;
      continue;         
    }
    if ( len_s != len_t ){
      y[k] = R_PosInf;
      continue;
    }
    y[k] = (double) hamming(s, t, len_s, maxDist);
    if (y[k] < 0) y[k] = R_PosInf;
  }

  if (bytes) free(s);
  UNPROTECT(4);
  return yy;
}


//-- Match function interface with R

SEXP R_match_hm(SEXP x, SEXP table, SEXP nomatch, SEXP matchNA, SEXP maxDistance){
  PROTECT(x);
  PROTECT(table);
  PROTECT(nomatch);
  PROTECT(matchNA);
  PROTECT(maxDistance);

  int nx = length(x)
    , ntable = length(table)
    , no_match = INTEGER(nomatch)[0]
    , match_na = INTEGER(matchNA)[0]
    , max_dist = INTEGER(maxDistance)[0]
    , bytes = IS_CHARACTER(x);


  unsigned int *X = NULL, *T = NULL;
  if ( bytes ){
    int ml_x = max_length(x);
    X = (unsigned int *) malloc((ml_x + max_length(table)) * sizeof(int));
    T = X + ml_x;
    if ( X == NULL ){
      UNPROTECT(5);
      error("Unable to allocate enough memory");
    }
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
      T = get_elem(table, j, bytes, &len_T, &isna_T, T);
      if ( len_X != len_T ) continue;

      if ( !isna_X && !isna_T ){        // both are char (usual case)
        d = (double) hamming( X, T, len_X, max_dist );
        if ( d > -1 && d < d1){ 
          index = j + 1;
          if ( d == 0.0 ) break;
          d1 = d;
        }
      } else if ( isna_X && isna_T ) {  // both are NA
        index = match_na ? j + 1 : no_match;
        break;
      }
    }
    
    y[i] = index;
  }  
  UNPROTECT(6);
  if (bytes) free(X);
  return(yy);
}

