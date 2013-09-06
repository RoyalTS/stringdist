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
#include <R.h>
#include <Rdefines.h>
#include "utils.h"

/* Longest common substring
 * - basically edit distance, only allowing insertions and deletions, at the cost of 1.
 */
static int lcs(unsigned int *a, int na, unsigned int *b, int nb, int maxDistance, int *scores){
  if (!na){
    if ( maxDistance > 0 && maxDistance < nb ){
      return -1.0;
    } else {
      return (double) nb;
    }
  }
  if (!nb){
    if (maxDistance > 0 && maxDistance < na){
      return -1.0;
    } else {
      return (double) na;
    }
  }

  int i, j;
  int M, I = na+1, L = na+1, J = nb+1;
  
  for ( i = 0; i < I; ++i ){
    scores[i] = i;
  }
  for ( j = 1; j < J; ++j, L += I ){
    scores[L] = j;
  }

  for ( i = 1; i <= na; ++i ){
    M = 0; L = I;
    for ( j = 1; j <= nb; ++j, L += I, M += I ){
      if ( a[i-1] == b[j-1] ){ // equality, copy previous score
        scores[i + L] = scores[i-1 + M];
      } else {
        scores[i + L] = MIN(
          scores[i-1 + L] + 1 ,     // deletion
          scores[i   + M] + 1       // insertion
        );
      }
      
    }
  }
  int score = scores[I*J - 1];
  return (maxDistance > 0 && maxDistance < score )?(-1):score;
}

//-- interface with R


SEXP R_lcs(SEXP a, SEXP b, SEXP maxDistance){
  PROTECT(a);
  PROTECT(b);
  PROTECT(maxDistance);

  int na = length(a)
    , nb = length(b)
    , ml_a = max_length(a)
    , ml_b = max_length(b)
    , maxDist = INTEGER(maxDistance)[0]
    , bytes = IS_CHARACTER(a);

  // space for the workfunction
  int *scores; 
  scores = (int *) malloc( (ml_a + 1) * (ml_b + 1) * sizeof(int)); 

  unsigned int *s = NULL, *t = NULL;
  if ( bytes ){
    s = (unsigned int *) malloc( (ml_a + ml_b) * sizeof(int));
    t = s + ml_a; 
  }

  if ( (scores == NULL) | (bytes && s == NULL) ){
    UNPROTECT(3); free(scores); free(s);
    error("%s\n","unable to allocate enough memory for workspace");
  }

  // output vector
  int nt = (na > nb) ? na : nb;   
  SEXP yy;
  PROTECT(yy = allocVector(REALSXP, nt));
  double *y = REAL(yy);   
  
  int i=0, j=0, len_s, len_t, isna_s, isna_t;
  for ( int k=0; k < nt; 
      ++k 
     , i = RECYCLE(i+1,na)
     , j = RECYCLE(j+1,nb) ){

    s = get_elem(a, i, bytes, &len_s, &isna_s, s);
    t = get_elem(b, j, bytes, &len_t, &isna_t, t);
    if ( isna_s || isna_t ){
      y[k] = NA_REAL;
      continue;
    }
    y[k] = lcs(s, len_s, t, len_t, maxDist, scores );
    if (y[k] < 0 ) y[k] = R_PosInf;
  }
  
  free(scores);
  if (bytes) free(s);
  UNPROTECT(4);
  return(yy);
}



//-- Match function interface with R

SEXP R_match_lcs(SEXP x, SEXP table, SEXP nomatch, SEXP matchNA, SEXP maxDistance){
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
    , bytes = IS_CHARACTER(x)
    , ml_x = max_length(x)
    , ml_t = max_length(table);

  // space for the workfunction
  int *work = (int *) malloc( (max_length(x) + 1) * (max_length(table) + 1) * sizeof(int)); 
  unsigned int *X = NULL, *T = NULL;
  if ( bytes ){
    X = (unsigned int *) malloc((ml_x + ml_t)*sizeof(int));
    T = X + ml_x;
  }
  if ( ( work == NULL) | (bytes && X == NULL) ){
    UNPROTECT(3); free(work); free(X);
    error("%s\n","unable to allocate enough memory for workspace");
  }

  // output vector
  SEXP yy;
  PROTECT(yy = allocVector(INTSXP, nx));
  int *y = INTEGER(yy);

  double d = R_PosInf, d1 = R_PosInf;

  int index, len_X, isna_X, len_T, isna_T;

  for ( int i=0; i<nx; i++){
    index = no_match;


    X = get_elem(x, i, bytes, &len_X, &isna_X, X);
    d1 = R_PosInf;
    for ( int j=0; j<ntable; j++){

      T = get_elem(table, j, bytes, &len_T, &isna_T, T);
      if ( !isna_X && !isna_T ){        // both are char (usual case)
        d = (double) lcs(
          X, len_X, T, len_T, max_dist, work
        );
        if ( d > -1 && d < d1){ 
          index = j + 1;
          if ( d == 0.0  ) break;
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
  free(work);  
  UNPROTECT(6);
  return(yy);
}
