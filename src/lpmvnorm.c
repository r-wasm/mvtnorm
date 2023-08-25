
/* C Header */

/*
    Copyright (C) 2022- Torsten Hothorn

    This file is part of the 'mvtnorm' R add-on package.

    'mvtnorm' is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    'mvtnorm' is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with 'mvtnorm'.  If not, see <http://www.gnu.org/licenses/>.


    DO NOT EDIT THIS FILE

    Edit 'lmvnorm_src.w' and run 'nuweb -r lmvnorm_src.w'
*/

#include <R.h>
#include <Rmath.h>
#include <Rinternals.h>
#include <Rdefines.h>
#include <Rconfig.h>
#include <R_ext/BLAS.h> /* for dtrmm */
/* pnorm fast */

/* see https://doi.org/10.2139/ssrn.2842681  */
const double g2 =  -0.0150234471495426236132;
const double g4 = 0.000666098511701018747289;
const double g6 = 5.07937324518981103694e-06;
const double g8 = -2.92345273673194627762e-06;
const double g10 = 1.34797733516989204361e-07;
const double m2dpi = -2.0 / M_PI; //3.141592653589793115998;

double C_pnorm_fast (double x, double m) {

    double tmp, ret;
    double x2, x4, x6, x8, x10;

    if (R_FINITE(x)) {
        x = x - m;
        x2 = x * x;
        x4 = x2 * x2;
        x6 = x4 * x2;
        x8 = x6 * x2;
        x10 = x8 * x2;
        tmp = 1 + g2 * x2 + g4 * x4 + g6 * x6  + g8 * x8 + g10 * x10;
        tmp = m2dpi * x2 * tmp;
        ret = .5 + ((x > 0) - (x < 0)) * sqrt(1 - exp(tmp)) / 2.0;
    } else {
        ret = (x > 0 ? 1.0 : 0.0);
    }
    return(ret);
}

/* pnorm slow */

double C_pnorm_slow (double x, double m) {
    return(pnorm(x, m, 1.0, 1L, 0L));
}

/* R lpmvnorm */

SEXP R_lpmvnorm(SEXP a, SEXP b, SEXP C, SEXP center, SEXP N, SEXP J, 
                SEXP W, SEXP M, SEXP tol, SEXP logLik, SEXP fast) {

    /* R slpmvnorm variables */
    
    SEXP ans;
    double *da, *db, *dC, *dW, *dans, dtol = REAL(tol)[0];
    double *dcenter;
    double mdtol = 1.0 - dtol;
    double d0, e0, emd0, f0, q0;
    
    double l0, lM, x0, intsum;
    int p, len;

    Rboolean RlogLik = asLogical(logLik);

    /* pnorm */
    
    Rboolean Rfast = asLogical(fast);
    double (*pnorm_ptr)(double, double) = C_pnorm_slow;
    if (Rfast)
        pnorm_ptr = C_pnorm_fast;
    
    /* dimensions */
    
    int iM = INTEGER(M)[0]; 
    int iN = INTEGER(N)[0]; 
    int iJ = INTEGER(J)[0]; 

    da = REAL(a);
    db = REAL(b);
    dC = REAL(C);
    dW = REAL(C); // make -Wmaybe-uninitialized happy

    if (LENGTH(C) == iJ * (iJ - 1) / 2)
        p = 0;
    else 
        p = LENGTH(C) / iN;
    
    /* W length */
    
    int pW = 0;
    if (W != R_NilValue) {
        if (LENGTH(W) == (iJ - 1) * iM) {
            pW = 0;
        } else {
            if (LENGTH(W) != (iJ - 1) * iN * iM)
                error("Length of W incorrect");
            pW = 1;
        }
        dW = REAL(W);
    }
    
    /* init center */
    
    dcenter = REAL(center);
    if (LENGTH(center)) {
        if (LENGTH(center) != iN * iJ)
            error("incorrect dimensions of center");
    }
    

    int start, j, k;
    double tmp, Wtmp, e, d, f, emd, x, y[(iJ > 1 ? iJ - 1 : 1)];

    /* setup return object */
    
    len = (RlogLik ? 1 : iN);
    PROTECT(ans = allocVector(REALSXP, len));
    dans = REAL(ans);
    for (int i = 0; i < len; i++)
        dans[i] = 0.0;
    

    q0 = qnorm(dtol, 0.0, 1.0, 1L, 0L);
    l0 = log(dtol);

    /* univariate problem */
    
    if (iJ == 1) {
        iM = 0; 
        lM = 0.0;
    } else {
        lM = log((double) iM);
    }
    

    if (W == R_NilValue)
        GetRNGstate();

    for (int i = 0; i < iN; i++) {

        x0 = 0;
        /* initialisation */
        
        x0 = 0.0;
        if (LENGTH(center))
            x0 = -dcenter[0];
        d0 = pnorm_ptr(da[0], x0);
        e0 = pnorm_ptr(db[0], x0);
        emd0 = e0 - d0;
        f0 = emd0;
        intsum = (iJ > 1 ? 0.0 : f0);
        

        if (W != R_NilValue && pW == 0)
            dW = REAL(W);

        for (int m = 0; m < iM; m++) {

            /* init logLik loop */
            
            d = d0;
            f = f0;
            emd = emd0;
            start = 0;
            
            /* inner logLik loop */
            
            for (j = 1; j < iJ; j++) {

                /* compute y */
                
                Wtmp = (W == R_NilValue ? unif_rand() : dW[j - 1]);
                tmp = d + Wtmp * emd;
                if (tmp < dtol) {
                    y[j - 1] = q0;
                } else {
                    if (tmp > mdtol)
                        y[j - 1] = -q0;
                    else
                        y[j - 1] = qnorm(tmp, 0.0, 1.0, 1L, 0L);
                }
                
                /* compute x */
                
                x = 0.0;
                if (LENGTH(center)) {
                    for (k = 0; k < j; k++)
                        x += dC[start + k] * (y[k] - dcenter[k]);
                    x -= dcenter[j]; 
                } else {
                    for (k = 0; k < j; k++)
                        x += dC[start + k] * y[k];
                }
                
                /* update d, e */
                
                d = pnorm_ptr(da[j], x);
                e = pnorm_ptr(db[j], x);
                emd = e - d;
                
                /* update f */
                
                start += j;
                f *= emd;
                
            }
            
            /* increment */
            
            intsum += f;
            

            if (W != R_NilValue)
                dW += iJ - 1;
        }

        /* output */
        
        dans[0] += (intsum < dtol ? l0 : log(intsum)) - lM;
        if (!RlogLik)
            dans += 1L;
        
        /* move on */
        
        da += iJ;
        db += iJ;
        dC += p;
        if (LENGTH(center)) dcenter += iJ;
        
    }

    if (W == R_NilValue)
        PutRNGstate();

    UNPROTECT(1);
    return(ans);
}

/* R slpmvnorm */

SEXP R_slpmvnorm(SEXP a, SEXP b, SEXP C, SEXP center, SEXP N, SEXP J, SEXP W, 
               SEXP M, SEXP tol, SEXP fast) {

    /* R slpmvnorm variables */
    
    SEXP ans;
    double *da, *db, *dC, *dW, *dans, dtol = REAL(tol)[0];
    double *dcenter;
    double mdtol = 1.0 - dtol;
    double d0, e0, emd0, f0, q0;
    
    double intsum;
    int p, idx;

    /* dimensions */
    
    int iM = INTEGER(M)[0]; 
    int iN = INTEGER(N)[0]; 
    int iJ = INTEGER(J)[0]; 

    da = REAL(a);
    db = REAL(b);
    dC = REAL(C);
    dW = REAL(C); // make -Wmaybe-uninitialized happy

    if (LENGTH(C) == iJ * (iJ - 1) / 2)
        p = 0;
    else 
        p = LENGTH(C) / iN;
    
    /* pnorm */
    
    Rboolean Rfast = asLogical(fast);
    double (*pnorm_ptr)(double, double) = C_pnorm_slow;
    if (Rfast)
        pnorm_ptr = C_pnorm_fast;
    
    /* W length */
    
    int pW = 0;
    if (W != R_NilValue) {
        if (LENGTH(W) == (iJ - 1) * iM) {
            pW = 0;
        } else {
            if (LENGTH(W) != (iJ - 1) * iN * iM)
                error("Length of W incorrect");
            pW = 1;
        }
        dW = REAL(W);
    }
    
    /* init center */
    
    dcenter = REAL(center);
    if (LENGTH(center)) {
        if (LENGTH(center) != iN * iJ)
            error("incorrect dimensions of center");
    }
    

    int start, j, k;
    double tmp, e, d, f, emd, x, x0, y[(iJ > 1 ? iJ - 1 : 1)];

    /* score output object */
    
    int Jp = iJ * (iJ + 1) / 2;
    /* chol scores */

    double dp_c[Jp], ep_c[Jp], fp_c[Jp], yp_c[(iJ > 1 ? iJ - 1 : 1) * Jp];
    
    /* mean scores */

    double dp_m[Jp], ep_m[Jp], fp_m[Jp], yp_m[(iJ > 1 ? iJ - 1 : 1) * Jp];
    
    /* lower scores */

    double dp_l[Jp], ep_l[Jp], fp_l[Jp], yp_l[(iJ > 1 ? iJ - 1 : 1) * Jp];
    
    /* upper scores */

    double dp_u[Jp], ep_u[Jp], fp_u[Jp], yp_u[(iJ > 1 ? iJ - 1 : 1) * Jp];
    
    double dtmp, etmp, Wtmp, ytmp, xx;

    PROTECT(ans = allocMatrix(REALSXP, Jp + 1 + 3 * iJ, iN));
    dans = REAL(ans);
    for (j = 0; j < LENGTH(ans); j++) dans[j] = 0.0;
    

    q0 = qnorm(dtol, 0.0, 1.0, 1L, 0L);

    /* univariate problem */
    if (iJ == 1) iM = 0; 

    if (W == R_NilValue)
        GetRNGstate();

    for (int i = 0; i < iN; i++) {

        /* initialisation */
        
        x0 = 0.0;
        if (LENGTH(center))
            x0 = -dcenter[0];
        d0 = pnorm_ptr(da[0], x0);
        e0 = pnorm_ptr(db[0], x0);
        emd0 = e0 - d0;
        f0 = emd0;
        intsum = (iJ > 1 ? 0.0 : f0);
        
        /* score c11 */
        
        if (LENGTH(center)) {
            dp_c[0] = (R_FINITE(da[0]) ? dnorm(da[0], x0, 1.0, 0L) * (da[0] - x0 - dcenter[0]) : 0);
            ep_c[0] = (R_FINITE(db[0]) ? dnorm(db[0], x0, 1.0, 0L) * (db[0] - x0 - dcenter[0]) : 0);
        } else {
            dp_c[0] = (R_FINITE(da[0]) ? dnorm(da[0], x0, 1.0, 0L) * (da[0] - x0) : 0);
            ep_c[0] = (R_FINITE(db[0]) ? dnorm(db[0], x0, 1.0, 0L) * (db[0] - x0) : 0);
        }
        fp_c[0] = ep_c[0] - dp_c[0];
        
        /* score a, b */
        
        dp_m[0] = (R_FINITE(da[0]) ? dnorm(da[0], x0, 1.0, 0L) : 0);
        ep_m[0] = (R_FINITE(db[0]) ? dnorm(db[0], x0, 1.0, 0L) : 0);
        dp_l[0] = dp_m[0];
        ep_u[0] = ep_m[0];
        dp_u[0] = 0;
        ep_l[0] = 0;
        fp_m[0] = ep_m[0] - dp_m[0];
        fp_l[0] = -dp_m[0];
        fp_u[0] = ep_m[0];
        
        /* init dans */
        
        if (iM == 0) {
            dans[0] = intsum;
            dans[1] = fp_c[0];
            dans[2] = fp_m[0];
            dans[3] = fp_l[0];
            dans[4] = fp_u[0];
        }
        

        if (W != R_NilValue && pW == 0)
            dW = REAL(W);

        for (int m = 0; m < iM; m++) {

            /* init score loop */
            
            /* init logLik loop */

            d = d0;
            f = f0;
            emd = emd0;
            start = 0;
            
            /* score c11 */

            if (LENGTH(center)) {
                dp_c[0] = (R_FINITE(da[0]) ? dnorm(da[0], x0, 1.0, 0L) * (da[0] - x0 - dcenter[0]) : 0);
                ep_c[0] = (R_FINITE(db[0]) ? dnorm(db[0], x0, 1.0, 0L) * (db[0] - x0 - dcenter[0]) : 0);
            } else {
                dp_c[0] = (R_FINITE(da[0]) ? dnorm(da[0], x0, 1.0, 0L) * (da[0] - x0) : 0);
                ep_c[0] = (R_FINITE(db[0]) ? dnorm(db[0], x0, 1.0, 0L) * (db[0] - x0) : 0);
            }
            fp_c[0] = ep_c[0] - dp_c[0];
            
            /* score a, b */

            dp_m[0] = (R_FINITE(da[0]) ? dnorm(da[0], x0, 1.0, 0L) : 0);
            ep_m[0] = (R_FINITE(db[0]) ? dnorm(db[0], x0, 1.0, 0L) : 0);
            dp_l[0] = dp_m[0];
            ep_u[0] = ep_m[0];
            dp_u[0] = 0;
            ep_l[0] = 0;
            fp_m[0] = ep_m[0] - dp_m[0];
            fp_l[0] = -dp_m[0];
            fp_u[0] = ep_m[0];
            
            
            /* inner score loop */
            
            for (j = 1; j < iJ; j++) {

                /* compute y */
                
                Wtmp = (W == R_NilValue ? unif_rand() : dW[j - 1]);
                tmp = d + Wtmp * emd;
                if (tmp < dtol) {
                    y[j - 1] = q0;
                } else {
                    if (tmp > mdtol)
                        y[j - 1] = -q0;
                    else
                        y[j - 1] = qnorm(tmp, 0.0, 1.0, 1L, 0L);
                }
                
                /* compute x */
                
                x = 0.0;
                if (LENGTH(center)) {
                    for (k = 0; k < j; k++)
                        x += dC[start + k] * (y[k] - dcenter[k]);
                    x -= dcenter[j]; 
                } else {
                    for (k = 0; k < j; k++)
                        x += dC[start + k] * y[k];
                }
                
                /* update d, e */
                
                d = pnorm_ptr(da[j], x);
                e = pnorm_ptr(db[j], x);
                emd = e - d;
                
                /* update yp for chol */
                
                ytmp = exp(- dnorm(y[j - 1], 0.0, 1.0, 1L)); // = 1 / dnorm(y[j - 1], 0.0, 1.0, 0L)

                for (k = 0; k < Jp; k++) yp_c[k * (iJ - 1) + (j - 1)] = 0.0;

                for (idx = 0; idx < (j + 1) * j / 2; idx++) {
                    yp_c[idx * (iJ - 1) + (j - 1)] = ytmp;
                    yp_c[idx * (iJ - 1) + (j - 1)] *= (dp_c[idx] + Wtmp * (ep_c[idx] - dp_c[idx]));
                }
                
                /* update yp for means, lower and upper */
                
                for (k = 0; k < iJ; k++)
                    yp_m[k * (iJ - 1) + (j - 1)] = 0.0;

                for (idx = 0; idx < j; idx++) {
                    yp_m[idx * (iJ - 1) + (j - 1)] = ytmp;
                    yp_m[idx * (iJ - 1) + (j - 1)] *= (dp_m[idx] + Wtmp * (ep_m[idx] - dp_m[idx]));
                }
                for (k = 0; k < iJ; k++)
                    yp_l[k * (iJ - 1) + (j - 1)] = 0.0;

                for (idx = 0; idx < j; idx++) {
                    yp_l[idx * (iJ - 1) + (j - 1)] = ytmp;
                    yp_l[idx * (iJ - 1) + (j - 1)] *= (dp_l[idx] + Wtmp * (dp_u[idx] - dp_l[idx]));
                }
                for (k = 0; k < iJ; k++)
                    yp_u[k * (iJ - 1) + (j - 1)] = 0.0;

                for (idx = 0; idx < j; idx++) {
                    yp_u[idx * (iJ - 1) + (j - 1)] = ytmp;
                    yp_u[idx * (iJ - 1) + (j - 1)] *= (ep_l[idx] + Wtmp * (ep_u[idx] - ep_l[idx]));
                }
                
                /* score wrt new chol off-diagonals */
                
                dtmp = dnorm(da[j], x, 1.0, 0L);
                etmp = dnorm(db[j], x, 1.0, 0L);

                for (k = 0; k < j; k++) {
                    idx = start + j + k;
                    if (LENGTH(center)) {    
                        dp_c[idx] = dtmp * (-1.0) * (y[k] - dcenter[k]);
                        ep_c[idx] = etmp * (-1.0) * (y[k] - dcenter[k]);
                    } else {
                        dp_c[idx] = dtmp * (-1.0) * y[k];
                        ep_c[idx] = etmp * (-1.0) * y[k];
                    }
                    fp_c[idx] = (ep_c[idx] - dp_c[idx]) * f;
                }
                
                /* score wrt new chol diagonal */
                
                idx = (j + 1) * (j + 2) / 2 - 1;
                if (LENGTH(center)) {
                    dp_c[idx] = (R_FINITE(da[j]) ? dtmp * (da[j] - x - dcenter[j]) : 0);
                    ep_c[idx] = (R_FINITE(db[j]) ? etmp * (db[j] - x - dcenter[j]) : 0);
                } else {
                    dp_c[idx] = (R_FINITE(da[j]) ? dtmp * (da[j] - x) : 0);
                    ep_c[idx] = (R_FINITE(db[j]) ? etmp * (db[j] - x) : 0);
                }
                fp_c[idx] = (ep_c[idx] - dp_c[idx]) * f;
                
                /* new score means, lower and upper */
                
                dp_m[j] = (R_FINITE(da[j]) ? dtmp : 0);
                ep_m[j] = (R_FINITE(db[j]) ? etmp : 0);
                dp_l[j] = dp_m[j];
                ep_u[j] = ep_m[j];
                dp_u[j] = 0;
                ep_l[j] = 0;
                fp_l[j] = - dp_m[j] * f;
                fp_u[j] = ep_m[j] * f;
                fp_m[j] = fp_u[j] + fp_l[j];
                
                /* update score for chol */
                
                for (idx = 0; idx < j * (j + 1) / 2; idx++) {
                    xx = 0.0;
                    for (k = 0; k < j; k++)
                        xx += dC[start + k] * yp_c[idx * (iJ - 1) + k];

                    dp_c[idx] = dtmp * (-1.0) * xx;
                    ep_c[idx] = etmp * (-1.0) * xx;
                    fp_c[idx] = (ep_c[idx] - dp_c[idx]) * f + emd * fp_c[idx];
                }
                
                /* update score means, lower and upper */
                
                for (idx = 0; idx < j; idx++) {
                    xx = 0.0;
                    for (k = 0; k < j; k++)
                        xx += dC[start + k] * yp_m[idx * (iJ - 1) + k];

                    dp_m[idx] = dtmp * (-1.0) * xx;
                    ep_m[idx] = etmp * (-1.0) * xx;
                    fp_m[idx] = (ep_m[idx] - dp_m[idx]) * f + emd * fp_m[idx];
                }

                for (idx = 0; idx < j; idx++) {
                    xx = 0.0;
                    for (k = 0; k < j; k++)
                        xx += dC[start + k] * yp_l[idx * (iJ - 1) + k];

                    dp_l[idx] = dtmp * (-1.0) * xx;
                    dp_u[idx] = etmp * (-1.0) * xx;
                    fp_l[idx] = (dp_u[idx] - dp_l[idx]) * f + emd * fp_l[idx];
                }

                for (idx = 0; idx < j; idx++) {
                    xx = 0.0;
                    for (k = 0; k < j; k++)
                        xx += dC[start + k] * yp_u[idx * (iJ - 1) + k];

                    ep_l[idx] = dtmp * (-1.0) * xx;
                    ep_u[idx] = etmp * (-1.0) * xx;
                    fp_u[idx] = (ep_u[idx] - ep_l[idx]) * f + emd * fp_u[idx];
                }
                
                /* update f */
                
                start += j;
                f *= emd;
                

            }
            
            /* score output */
            
            dans[0] += f;
            for (j = 0; j < Jp; j++)
                dans[j + 1] += fp_c[j];
            for (j = 0; j < iJ; j++) {
                idx = Jp + j + 1;
                dans[idx] += fp_m[j];
                dans[idx + iJ] += fp_l[j];
                dans[idx + 2 * iJ] += fp_u[j];
            }
            

            if (W != R_NilValue)
                dW += iJ - 1;
        }

        /* move on */
        
        da += iJ;
        db += iJ;
        dC += p;
        if (LENGTH(center)) dcenter += iJ;
        

        dans += Jp + 1 + 3 * iJ;
    }

    if (W == R_NilValue)
        PutRNGstate();

    UNPROTECT(1);
    return(ans);
}
