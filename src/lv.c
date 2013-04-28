
#define USE_RINTERNALS
#include <stdlib.h>
#include <R.h>
#include <Rdefines.h>
#include "utils.h"

/* Levenshtein distance
 * Computes Levenshtein distance
 * - Simplified from restricted DL pseudocode at http://en.wikipedia.org/wiki/Damerau%E2%80%93Levenshtein_distance
 * - Extended with custom weights and maxDistance
 */
static double lv(unsigned int *a, int na, unsigned int *b, int nb, double *weight, double maxDistance, double *scores){
  if (na == 0){
    if ( maxDistance > 0 && maxDistance < nb ){
      return -1;
    } else {
      return (double) nb;
    }
  }
  if (nb == 0){
    if (maxDistance > 0 && maxDistance < na){
      return -1;
    } else {
      return (double) na;
    }
  }
  
 
   int i, j;
   int I = na+1, J = nb+1;
   double sub;

   for ( i = 0; i < I; ++i ){
      scores[i] = i;
   }
   for ( j = 1; j < J; ++j ){
      scores[I*j] = j;
   }

   for ( i = 1; i <= na; ++i ){
      
      for ( j = 1; j <= nb; ++j ){
         sub = (a[i-1] == b[j-1]) ? 0 : weight[2];
         scores[i + I*j] = min3( 
            scores[i-1 + I*j    ] + weight[0],     // deletion
            scores[i   + I*(j-1)] + weight[1],     // insertion
            scores[i-1 + I*(j-1)] + sub            // substitution
         );
     
      }
   }
   double score = scores[I*J-1];
   return (maxDistance > 0 && maxDistance < score)?(-1):score;
}

//-- interface with R


SEXP R_lv(SEXP a, SEXP b, SEXP weight, SEXP maxDistance){
  PROTECT(a);
  PROTECT(b);
  PROTECT(weight);
  PROTECT(maxDistance);

  int na = length(a), nb = length(b);
  double *scores; 
  double *w = REAL(weight);
  double maxDist = REAL(maxDistance)[0];

  scores = (double *) malloc((max_length(a) + 1) * (max_length(b) + 1) * sizeof(double)); 
  if ( scores == NULL ){
    UNPROTECT(4);
    error("%s\n","unable to allocate enough memory for workspace");
  }

  // output vector
  int nt = (na > nb) ? na : nb;   
  int i=0,j=0;
  SEXP yy;
  PROTECT(yy = allocVector(REALSXP, nt));
  double *y = REAL(yy);   
  
  for ( int k=0; k < nt; ++k ){
    if (INTEGER(VECTOR_ELT(a,i))[0] == NA_INTEGER || INTEGER(VECTOR_ELT(b,j))[0] == NA_INTEGER){
      y[k] = NA_REAL;
      continue;
    }
    y[k] = lv(
     (unsigned int *) INTEGER(VECTOR_ELT(a,i)), 
     length(VECTOR_ELT(a,i)), 
     (unsigned int *) INTEGER(VECTOR_ELT(b,j)), 
     length(VECTOR_ELT(b,j)), 
     w,
     maxDist,
     scores
    );
    if (y[k] < 0 ) y[k] = R_PosInf;
    i = RECYCLE(i+1,na);
    j = RECYCLE(j+1,nb);
  }
  
  free(scores);
  UNPROTECT(5);
  return(yy);
}


//-- Match function interface with R

SEXP R_match_lv(SEXP x, SEXP table, SEXP nomatch, SEXP matchNA, SEXP weight, SEXP maxDistance){
  PROTECT(x);
  PROTECT(table);
  PROTECT(nomatch);
  PROTECT(matchNA);
  PROTECT(weight);
  PROTECT(maxDistance);

  int nx = length(x), ntable = length(table);
  int no_match = INTEGER(nomatch)[0];
  int match_na = INTEGER(matchNA)[0];
  double *w = REAL(weight);
  double maxDist = REAL(maxDistance)[0];
  
  /* claim space for workhorse */
  double *scores = (double *) malloc((max_length(x) + 1) * (max_length(table) + 1) * sizeof(double)); 
  if ( scores == NULL ){
     UNPROTECT(6);
     error("%s\n","unable to allocate enough memory");
  }

  // output vector
  SEXP yy;
  PROTECT(yy = allocVector(INTSXP, nx));
  int *y = INTEGER(yy);
  int *X, *T;


  double d = R_PosInf, d1 = R_PosInf;
  int index, xNA, tNA;

  for ( int i=0; i<nx; i++){
    index = no_match;

    X = INTEGER(VECTOR_ELT(x,i));
    xNA = (X[0] == NA_INTEGER);

    for ( int j=0; j<ntable; j++){

      T = INTEGER(VECTOR_ELT(table,j));
      tNA = (T[0] == NA_INTEGER);

      if ( !xNA && !tNA ){        // both are char (usual case)
        d = lv(
          (unsigned int *) X, 
          length(VECTOR_ELT(x,i)), 
          (unsigned int *) T, 
          length(VECTOR_ELT(table,j)), 
          w,
          maxDist,
          scores
        );
        if ( d > -1 && d < d1){ 
          index = j + 1;
          if ( d == 0.0 ) break;
          d1 = d;
        }
      } else if ( xNA && tNA ) {  // both are NA
        index = match_na ? j + 1 : no_match;
        break;
      }
    }
    
    y[i] = index;
  }  
  UNPROTECT(7);
  free(scores);
  return(yy);
}


