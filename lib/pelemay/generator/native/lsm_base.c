#include <lsm_base.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pelemay_base.h>

static uint64_t sum(uint64_t *a, size_t n) {
  uint64_t sum = 0;
#pragma clang loop vectorize_width(LOOP_VECTORIZE_WIDTH)
  for(size_t i = 0; i < n; i++) {
    sum += a[i];
  }
  return sum;
}

static double avr(uint64_t sum, size_t n) {
  return (double)sum / n;
}

static double *diff(uint64_t *a, size_t n, double avr) {
  double *ret = (double *)malloc(sizeof(double) * n);
#pragma clang loop vectorize_width(LOOP_VECTORIZE_WIDTH)
  for(size_t i = 0; i < n; i++) {
    ret[i] = (a[i] - avr);
  }
  return ret;
}

static double variance(double* diff, size_t n) {
  double ret = 0.0;
#pragma clang loop vectorize_width(LOOP_VECTORIZE_WIDTH)
  for(size_t i = 0; i < n; i++) {
    ret += diff[i] * diff[i];
  }
  return ret / n;
}

static double covariance(double *diff_a, double *diff_b, size_t n) {
  double ret = 0.0;
#pragma clang loop vectorize_width(LOOP_VECTORIZE_WIDTH)
  for(int i = 0; i < n; i++) {
    ret += diff_a[i] * diff_b[i];
  }
  return ret / n;
}

static double lsm_a(double variance_x, double covariance) {
   return covariance / variance_x;
}

static double lsm_b(double lsm_a, double avr_x, double avr_y) {
  return avr_y - lsm_a * avr_x;
}

static double lsm_r(double variance_x, double variance_y, double covariance) {
  return covariance / sqrt(variance_x) / sqrt(variance_y);
}

double *pelemay_lsm(uint64_t *x, uint64_t *y, size_t n) {
  uint64_t sum_x = sum(x, n);
  uint64_t sum_y = sum(y, n);
  double avr_x = avr(sum_x, n);
  double avr_y = avr(sum_y, n);
  double *diff_x = diff(x, n, avr_x);
  double *diff_y = diff(y, n, avr_y);
  double variance_x = variance(diff_x, n);
  double variance_y = variance(diff_y, n);
  double _covariance = covariance(diff_x, diff_y, n);
  double a = lsm_a(variance_x, _covariance);
  double b = lsm_b(a, avr_x, avr_y);
  double r = lsm_r(variance_x, variance_y, _covariance);
  free(diff_x);
  free(diff_y);
  double *_lsm = (double *)malloc(sizeof(double) * 3);
  _lsm[0] = r;
  _lsm[1] = a;
  _lsm[2] = b;
  return _lsm;
}

double *pelemay_lsm_drive(pelemay_driver driver) {
  uint64_t *x = (uint64_t *)malloc(sizeof(uint64_t) * DRIVE_NUM * MAX_SHIFT_SIZE);
  uint64_t *y1 = (uint64_t *)malloc(sizeof(uint64_t) * DRIVE_NUM * MAX_SHIFT_SIZE);
  uint64_t *y2 = (uint64_t *)malloc(sizeof(uint64_t) * DRIVE_NUM * MAX_SHIFT_SIZE);
  uint64_t *t1 = (uint64_t *)malloc(sizeof(uint64_t) * DRIVE_NUM);
  uint64_t *t2 = (uint64_t *)malloc(sizeof(uint64_t) * DRIVE_NUM);
  uint64_t *r;
  double **lsm1 = (double **)malloc(sizeof(double *) * LSM_NUM);
  double **lsm2 = (double **)malloc(sizeof(double *) * LSM_NUM);
  bool cannot_measure = false;
  for(unsigned n = 0; n < LSM_NUM; n++) {
    size_t size = LOOP_VECTORIZE_WIDTH;
    size_t count = 0;
    for(unsigned i = 0; i < MAX_SHIFT_SIZE; i++) {
      for(unsigned j = 0; j < DRIVE_NUM; j++) {
        r = (* driver)((uint64_t)size);
        t1[j] = r[0];
        t2[j] = r[1];
        free(r);
        if(t2[j] == 0) {
          if(size < MAX_SIZE) {
            size <<= 1;
            j = 0;
          } else {
            cannot_measure = true;
            break;
          }
        }
      }
      if(cannot_measure) {
        break;
      }
      uint64_t sum_t1 = sum(t1, DRIVE_NUM);
      uint64_t sum_t2 = sum(t2, DRIVE_NUM);
      double avr_t1 = avr(sum_t1, DRIVE_NUM);
      double avr_t2 = avr(sum_t2, DRIVE_NUM);
      double *diff_t1 = diff(t1, DRIVE_NUM, avr_t1);
      double *diff_t2 = diff(t2, DRIVE_NUM, avr_t2);
      double variance_t1 = variance(diff_t1, DRIVE_NUM);
      double variance_t2 = variance(diff_t2, DRIVE_NUM);
      free(diff_t1);
      free(diff_t2);
      for(unsigned j = 0; j < DRIVE_NUM; j++) {
        if(fabs(t1[j] - avr_t1) / sqrt(variance_t1) < OUTLIER_FACTOR) {
          x[count] = size;
          y1[count] = t1[j];
          y2[count] = t2[j];
          count++;
        }
      }
      size <<= 1;
    }
    if(cannot_measure) {
      break;
    }
    lsm1[n] = pelemay_lsm(x, y1, count);
    lsm2[n] = pelemay_lsm(x, y2, count);
  }
  free(x);
  free(y1);
  free(y2);
  free(t1);
  free(t2);
  double *result = (double *)malloc(sizeof(double) * 6);
  if(cannot_measure) {
    result[0] = 0.0;
    result[1] = 0.0;
    result[2] = 0.0;
    result[3] = 0.0;
    result[4] = 0.0;
    result[5] = 0.0;
    free(lsm1);
    free(lsm2);
    return result;
  }
  result[0] = lsm1[0][0];
  result[1] = lsm1[0][1];
  result[2] = lsm1[0][2];
  result[3] = lsm2[0][0];
  result[4] = lsm2[0][1];
  result[5] = lsm2[0][2];
  free(lsm1[0]);
  free(lsm2[0]);

  for(unsigned n = 1; n < LSM_NUM; n++) {
    if(result[0] < lsm1[n][0]) {
      result[0] = lsm1[n][0];
      result[1] = lsm1[n][1];
      result[2] = lsm1[n][2];
      result[3] = lsm2[n][0];
      result[4] = lsm2[n][1];
      result[5] = lsm2[n][2];
    }
    free(lsm1[n]);
    free(lsm2[n]);
  }
  free(lsm1);
  free(lsm2);
  return result;
}
