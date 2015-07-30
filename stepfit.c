/*

                                COPYRIGHT
    Copyright 2015 Kyle Briggs <kbrig035@uottawa.ca>

    This file is part of CUSUM.

    CUSUM is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CUSUM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CUSUM.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "stepfit.h"

int expb_f (const gsl_vector * x, void *data, gsl_vector * f)
{
    uint64_t n = ((struct data *)data)->n;
    double *y = ((struct data *)data)->y;

    double i0 = gsl_vector_get (x, 0);
    double a = gsl_vector_get (x, 1);
    double u1 = gsl_vector_get (x, 2);
    double tau1 = gsl_vector_get (x, 3);
    double b = gsl_vector_get (x, 4);
    double u2 = gsl_vector_get (x, 5);
    double tau2 = gsl_vector_get (x, 6);
    uint64_t i;
    for (i = 0; i < n; i++)
    {
        double t = i;
        double Yi = i0;
        if (t > u1)
        {
            Yi += a*(exp(-(t-u1)/tau1)-1);
        }
        if (t > u2)
        {
            Yi += b*(1-exp(-(t-u2)/tau2));
        }
        gsl_vector_set (f, i, (Yi - y[i]));
    }
    return GSL_SUCCESS;
}

int expb_df (const gsl_vector * x, void *data, gsl_matrix * J)
{
    uint64_t n = ((struct data *)data)->n;

    double a = gsl_vector_get (x, 1);
    double u1 = gsl_vector_get (x, 2);
    double tau1 = gsl_vector_get (x, 3);
    double b = gsl_vector_get (x, 4);
    double u2 = gsl_vector_get (x, 5);
    double tau2 = gsl_vector_get (x, 6);

    uint64_t i;

    for (i = 0; i < n; i++)
    {
        double t = i;
        double s = 1;
        gsl_matrix_set (J, i, 0, 1);
        gsl_matrix_set (J, i, 1, t < u1 ? 0 : 1/s*(exp(-(t-u1)/tau1)-1));
        gsl_matrix_set (J, i, 2, t < u1 ? 0 : 1/s*(a/tau1 * exp(-(t-u1)/tau1)));
        gsl_matrix_set (J, i, 3, t < u1 ? 0 : 1/s*(a * (t-u1)/(tau1*tau1) * exp(-(t-u1)/tau1)));
        gsl_matrix_set (J, i, 4, t < u2 ? 0 : 1/s*(1-exp(-(t-u2)/tau2)));
        gsl_matrix_set (J, i, 5, t < u2 ? 0 : 1/s*(b/tau2 * (-exp(-(t-u2)/tau2))));
        gsl_matrix_set (J, i, 6, t < u2 ? 0 :  1/s*(-b * (t-u2)/(tau2*tau2) * exp(-(t-u2)/tau2)));
    }
    return GSL_SUCCESS;
}

int expb_fdf (const gsl_vector * x, void *data, gsl_vector * f, gsl_matrix * J)
{
    expb_f (x, data, f);
    expb_df (x, data, J);

    return GSL_SUCCESS;
}


int stepResponse(event *current, double risetime, uint64_t maxiters)
{
    const gsl_multifit_fdfsolver_type *T;
    gsl_multifit_fdfsolver *s;
    int status = GSL_CONTINUE;
    uint64_t i,iter = 0;
    uint64_t p = 7;
    uint64_t n = current->length + current->padding_before + current->padding_after;
    gsl_matrix *covar = gsl_matrix_alloc (p, p);
    struct data d = {n, current->signal};
    gsl_multifit_function_fdf f;

    gsl_vector *x = gsl_vector_alloc(p);
    gsl_vector_set(x,0,current->signal[0]);
    gsl_vector_set(x,1,current->signal[current->padding_before + current->length/2]);
    gsl_vector_set(x,2,current->padding_before);
    gsl_vector_set(x,3,risetime);
    gsl_vector_set(x,4,current->padding_before + current->length);
    gsl_vector_set(x,5,current->signal[current->padding_before + current->length/2]);
    gsl_vector_set(x,6,risetime);


    f.f = &expb_f;
    f.df = &expb_df;
    f.fdf = &expb_fdf;
    f.n = n;
    f.p = p;
    f.params = &d;

    T = gsl_multifit_fdfsolver_lmsder;
    s = gsl_multifit_fdfsolver_alloc (T, n, p);
    gsl_multifit_fdfsolver_set (s, &f, x);

    while (iter < maxiters && status == GSL_CONTINUE)
    {
        iter++;
        status = gsl_multifit_fdfsolver_iterate (s);

        printf ("status = %s\n", gsl_strerror (status));

        if (status)
        break;

        status = gsl_multifit_test_delta (s->dx, s->x,
                                        1e-4, 1e-4);
    }


    double i0 = FIT(0);
    double a = FIT(1);
    uint64_t u1 = (uint64_t) FIT(2);
    double b = FIT(4);
    uint64_t u2 = (uint64_t) FIT(5);

    if (u1 > u2) //if the fit got stuck in a variable swapped equivalent minimum, we can just reverse the paramters
    {
        printf("Backwards fit, swapping\n");
        uint64_t temp;
        double dtemp;
        temp = u1;
        u1 = u2;
        u2 = temp;
        dtemp = a;
        a = -b;
        b = -dtemp;
    }
    if (u2 > n || u1 <= 0) //if for some reason we are out of range
    {
        printf("Fitting error, discarding\n");
        current->type = BADFIT;
        return BADFIT;
    }
    for (i=0; i<u1; i++) //if all went well, populate the filtered trace
    {
        current->filtered_signal[i] = i0;
    }
    for (i=u1; i<u2; i++)
    {
        current->filtered_signal[i] = i0-a;
    }
    for (i=u2; i<n; i++)
    {
        current->filtered_signal[i] = i0-a+b;
    }

    gsl_multifit_fdfsolver_free (s);
    gsl_matrix_free (covar);
    gsl_vector_free(x);
    return status;
}