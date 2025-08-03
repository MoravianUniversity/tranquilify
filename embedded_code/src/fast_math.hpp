/**
 * Math utilities for ESP32. These are primarily optimized for speed on the ESP32.
 */
#pragma once

// Optimize for speed, not size; put in a function definition right before
// function name to apply it to that function. This is more aggression than
// even -O3 and may result in larger code size and inaccurate floating-point
// results. In some cases (due to larger code size), it may even be slower.
// Use with caution.
#define OPTIMIZE_FOR_SPEED __attribute__((optimize("Ofast")))


#include <dsp_common.h>
#define is_power_of_two(x) dsp_is_power_of_two(x)
inline static bool OPTIMIZE_FOR_SPEED is_power_of_four_given_is_pow_2(int n) { return (n & 0xAAAAAAAA) == 0; }  // assuming n is a power of 2, checks if it is also a power of 4
inline static bool OPTIMIZE_FOR_SPEED is_power_of_four(int n) { return is_power_of_two(n) && is_power_of_four_given_is_pow_2(n); }


//////////////////////////////
////////// Division //////////
//////////////////////////////
#if CONFIG_DSP_OPTIMIZED && CONFIG_IDF_TARGET_ESP32P4 != 1

 /**
  * Calculate the reciprocal of a float using inline assembly.
  * This is MUCH faster than using doing 1.0f/x and fully accurate.
  * From https://github.com/espressif/esp-dsp/issues/95.
  */
static __attribute__((always_inline)) inline
float recip(float x) {
    float result, temp;
    asm(
        "recip0.s %0, %2\n"
        "const.s %1, 1\n"
        "msub.s %1, %2, %0\n"
        "madd.s %0, %0, %1\n"
        "const.s %1, 1\n"
        "msub.s %1, %2, %0\n"
        "maddn.s %0, %0, %1\n"
        :"=&f"(result),"=&f"(temp):"f"(x)
    );
    return result;
}

/**
 * Divide two floats using the reciprocal method.
 * This is faster than using a/b for floats and fully accurate.
 */
#define DIVIDE(a, b) (a)*recip(b)

#else

// The ESP32-P4 has a 3-cycle fdiv.s instruction which is faster than the above.
// Additionally, the code above may not work on other CPU architectures. In those
// cases, we can use the standard division operator and hope that the compiler
// optimizes it.

static __attribute__((always_inline)) inline
float recip(float x) { return 1.0f / x; }
#define DIVIDE(a, b) ((a) / (b))

#endif


/////////////////////////////////
////////// Square Root //////////
/////////////////////////////////
// Approximate square root and inverse square root functions
// Much faster but not as accurate as the standard library functions
// TODO: maybe use InvSqrt43 from https://www.researchgate.net/publication/349173096_SIMPLE_EFFECTIVE_FAST_INVERSE_SQUARE_ROOT_ALGORITHM_WITH_TWO_MAGIC_CONSTANTS
//  which is tested on the ESP32, fully accurate, and as fast as possible
#include <dsps_sqrt.h> // for square root and inv square root approximation
#define sqrt_fast(x) dsps_sqrtf_f32(x)  // approximate square root
#define recip_sqrt_fast(x) dsps_inverted_sqrtf_f32(x)  // approximate inverse square root (much closer than the sqrt approximation)


//////////////////////////////
////////// Exponent //////////
//////////////////////////////
/**
 * Fast exponentiation function using a second-order approximation and floating-
 * point tricks. This is ~5.2x faster than the standard expf() function but has
 * up to ±0.48% error. It does not maintain monotonicity.
 * From https://specbranch.com/posts/fast-exp/.
 */
static float OPTIMIZE_FOR_SPEED exp_fast_o2(float x) {
    float xb = x * 12102203;
    int ib = (int)xb;
    int ir = ib + (127 << 23) - 345088;
    float first_order = *(float*)&ir;

    int correction_xi = (ir & 0x7fffff) | (127 << 23);
    float correction_x = *(float*)&correction_xi;

    float correction = fmaf(correction_x, 0.22670517861843109130859375f, -0.671999752521514892578125f);
    correction = fmaf(correction, correction_x, 1.469318866729736328125f);
    return first_order * correction;
}

/**
 * Fast exponentiation function using a first-order approximation and floating-
 * point tricks. This is ~14.7x faster than the standard expf() function but has
 * up to ±2.98% error. It does maintain monotonicity.
 * From https://specbranch.com/posts/fast-exp/.
 */
static float OPTIMIZE_FOR_SPEED exp_fast_o1(float x) {
    float xb = x * 12102203;
    int i = ((int)xb) + 1064986823;
    return *(float*)(&i);
}

/* Testing Code
    #include "esp32/clk.h"
    #define CPU_FREQ 240000.0 // ESP32 CPU frequency in 1/ms
    #include "esp_math.h"
    #include "bootloader_random.h"

    bootloader_random_enable();
    srand(esp_random());
    bootloader_random_disable();

    esp_cpu_ccount_t start, end, diff;

    float* xs = (float*)malloc(10000 * sizeof(float)); // -6 to 0
    float* ys = (float*)malloc(10000 * sizeof(float));
    for (int i = 0; i < 10000; i++) { xs[i] = random() / (float)RAND_MAX * -6; }

    start = esp_cpu_get_ccount();
    for (int i = 0; i < 10000; i++) { ys[i] = expf(xs[i]); }
    end = esp_cpu_get_ccount();
    diff = end - start;
    total += diff;
    Serial.printf("exp() took %d cycles / %0.3f ms\n", diff, diff / CPU_FREQ);

    start = esp_cpu_get_ccount();
    for (int i = 0; i < 10000; i++) { ys[i] = exp_a(xs[i]); }
    end = esp_cpu_get_ccount();
    diff = end - start;
    total += diff;
    Serial.printf("exp_a() took %d cycles / %0.3f ms\n", diff, diff / CPU_FREQ);

    start = esp_cpu_get_ccount();
    for (int i = 0; i < 10000; i++) { ys[i] = exp_b(xs[i]); }
    end = esp_cpu_get_ccount();
    diff = end - start;
    total += diff;
    Serial.printf("exp_b() took %d cycles / %0.3f ms\n", diff, diff / CPU_FREQ);

    start = esp_cpu_get_ccount();
    for (int i = 0; i < 10000; i++) { ys[i] = exp_c(xs[i]); }
    end = esp_cpu_get_ccount();
    diff = end - start;
    total += diff;
    Serial.printf("exp_c() took %d cycles / %0.3f ms\n", diff, diff / CPU_FREQ);

    start = esp_cpu_get_ccount();
    for (int i = 0; i < 10000; i++) { ys[i] = exp_d(xs[i]); }
    end = esp_cpu_get_ccount();
    diff = end - start;
    total += diff;
    Serial.printf("exp_d() took %d cycles / %0.3f ms\n", diff, diff / CPU_FREQ);
*/


//////////////////////////////////
////////// Trigonometry //////////
//////////////////////////////////

constexpr float PI_ = 3.14159265358979323846f; // pi constant (as a float)
constexpr float PI_HALF = 3.14159265358979323846f / 2; // pi/2 constant (as a float)
constexpr float PI_2 = 3.14159265358979323846f * 2; // 2*pi constant (as a float)

/**
 * Fast arctan function using a degree-5 approximation. Much faster than the
 * standard library atanf() function but less accurate. Assumes the given value
 * is in the range [-1, 1].
 * From https://github.com/RobTillaart/FastTrig.
 */
static float OPTIMIZE_FOR_SPEED atan_fast_d5(float x) {
    float x2 = x * x;
    return (((0.079331f * x2) - 0.288679f) * x2 + 0.995354f) * x;
}

/**
 * Fast arctan function using a degree-7 approximation. More accurate than the
 * degree-5 approximation but still faster than the standard library atanf()
 * function. Assumes the given value is in the range [-1, 1].
 * From https://github.com/RobTillaart/FastTrig.
 */
static float OPTIMIZE_FOR_SPEED atan_fast_d7(float x) {
    float x2 = x * x;
    return ((((-0.0389929f * x2) + 0.1462766f) * x2 - 0.3211819f) * x2 + 0.9992150f) * x;
}

/**
 * Fast arctan function using a degree-9 approximation. More accurate than the
 * degree-7 approximation but still faster than the standard library atanf()
 * function. Assumes the given value is in the range [-1, 1].
 */
static float OPTIMIZE_FOR_SPEED atan_fast_d9(float x) {
    float x2 = x * x;
    return ((((0.021814232f * x2 - 0.087072968f) * x2 + 0.181389254f) * x2 - 0.330585734f) * x2 + 0.99988258f) * x;
}

/**
 * Same as atan_fast_d5() but does not assume the value is in the range [-1, 1].
 * From https://github.com/RobTillaart/FastTrig.
 */
static float OPTIMIZE_FOR_SPEED atan_fast_d5_ur(float x) {
    if (unlikely(x > +1)) return (+PI_HALF) - atan_fast_d5(recip(x));
    if (unlikely(x < -1)) return (-PI_HALF) - atan_fast_d5(recip(x));
    return atan_fast_d5(x);
}

/**
 * Same as atan_fast_d7() but does not assume the value is in the range [-1, 1].
 * From https://github.com/RobTillaart/FastTrig.
 */
static float OPTIMIZE_FOR_SPEED atan_fast_d7_ur(float x) {
    if (unlikely(x > +1)) return (+PI_HALF) - atan_fast_d7(recip(x));
    if (unlikely(x < -1)) return (-PI_HALF) - atan_fast_d7(recip(x));
    return atan_fast_d7(x);
}

/**
 * Same as atan_fast_d9() but does not assume the value is in the range [-1, 1].
 */
static float OPTIMIZE_FOR_SPEED atan_fast_d9_ur(float x) {
    if (unlikely(x > +1)) return (+PI_HALF) - atan_fast_d9(recip(x));
    if (unlikely(x < -1)) return (-PI_HALF) - atan_fast_d9(recip(x));
    return atan_fast_d9(x);
}

/**
 * Fast 2-argument arctangent function. It uses atan_fast_d5() for the main
 * calculation and adjusts the result based on the signs of x and y.
 * 
 * This is ~3.6x faster (966 vs 4093 cycles, 4 vs 17 us).
 * Max error is ~4.9e-3 radians. Average error of ~3.9e-4 radians.
 */
static float OPTIMIZE_FOR_SPEED atan2_fast_d5(float y, float x) {
    if (unlikely(x == 0)) return y > 0 ? PI_HALF : (y < 0 ? -PI_HALF : NAN);
    if (fabsf(y) >= fabsf(x)) return ((y > 0) ? PI_HALF : -PI_HALF) - atan_fast_d5(DIVIDE(x, y));
    return (x >= 0) ? atan_fast_d5(DIVIDE(y, x)) : (((y > 0) ? PI_: -PI_) + atan_fast_d5(DIVIDE(y, x)));
}

/**
 * Fast 2-argument arctangent function. It uses atan_fast_d7() for the main
 * calculation and adjusts the result based on the signs of x and y.
 *
 * This is ~4.8x faster (678 vs 4093 cycles, 2 vs 17 us).
 * Max error is ~7.5e-4 radians. Average error of ~5.2e-5 radians.
 */
static float OPTIMIZE_FOR_SPEED atan2_fast_d7(float y, float x) {
    if (unlikely(x == 0)) return y > 0 ? PI_HALF : (y < 0 ? -PI_HALF : NAN);
    if (fabsf(y) >= fabsf(x)) return ((y > 0) ? PI_HALF : -PI_HALF) - atan_fast_d7(DIVIDE(x, y));
    return (x >= 0) ? atan_fast_d7(DIVIDE(y, x)) : (((y >= 0) ? PI_: -PI_) + atan_fast_d7(DIVIDE(y, x)));
}

/**
 * Fast 2-argument arctangent function. It uses atan_fast_d9() for the main
 * calculation and adjusts the result based on the signs of x and y.
 *
 * This is ~4.8x faster (1332 vs 4093 cycles, 6 vs 17 us).
 * Max error is ~9.8e-5 radians. Average error of ~6.7e-6 radians.
 */
static float OPTIMIZE_FOR_SPEED atan2_fast_d9(float y, float x) {
    if (unlikely(x == 0)) return y > 0 ? PI_HALF : (y < 0 ? -PI_HALF : NAN);
    if (fabsf(y) >= fabsf(x)) return ((y > 0) ? PI_HALF : -PI_HALF) - atan_fast_d9(DIVIDE(x, y));
    return (x >= 0) ? atan_fast_d9(DIVIDE(y, x)) : (((y > 0) ? PI_: -PI_) + atan_fast_d9(DIVIDE(y, x)));
}

/* Testing Code
    #include "esp32/clk.h"
    #define CPU_FREQ 240000.0 // ESP32 CPU frequency in 1/ms
    #include "esp_math.h"
    #include "bootloader_random.h"

    bootloader_random_enable();
    srand(esp_random());
    bootloader_random_disable();

    esp_cpu_ccount_t start, end, diff;

    float x = random() / (float)RAND_MAX;
    float y = random() / (float)RAND_MAX;

    start = esp_cpu_get_ccount();
    float ans1 = atan2f(y, x); ans1 += atan2f(y, x); ans1 += atan2f(y, x); ans1 += atan2f(y, x); ans1 += atan2f(y, x);
    ans1 += atan2f(y, x); ans1 += atan2f(y, x); ans1 += atan2f(y, x); ans1 += atan2f(y, x); ans1 += atan2f(y, x);
    end = esp_cpu_get_ccount();
    diff = (end - start) / 10;
    Serial.printf("atan2f: %d cycles / %0.4f ms\n", diff, (diff) / CPU_FREQ);

    start = esp_cpu_get_ccount();
    float ans2 = atan2_fast_d5(y, x); ans2 += atan2_fast_d5(y, x); ans2 += atan2_fast_d5(y, x); ans2 += atan2_fast_d5(y, x); ans2 += atan2_fast_d5(y, x);
    ans2 += atan2_fast_d5(y, x); ans2 += atan2_fast_d5(y, x); ans2 += atan2_fast_d5(y, x); ans2 += atan2_fast_d5(y, x); ans2 += atan2_fast_d5(y, x);
    end = esp_cpu_get_ccount();
    diff = (end - start) / 10;
    Serial.printf("atan2_fast_d5: %d cycles / %0.4f ms\n", diff, (diff) / CPU_FREQ);

    start = esp_cpu_get_ccount();
    float ans3 = atan2_fast_d7(y, x); ans3 += atan2_fast_d7(y, x); ans3 += atan2_fast_d7(y, x); ans3 += atan2_fast_d7(y, x); ans3 += atan2_fast_d7(y, x);
    ans3 += atan2_fast_d7(y, x); ans3 += atan2_fast_d7(y, x); ans3 += atan2_fast_d7(y, x); ans3 += atan2_fast_d7(y, x); ans3 += atan2_fast_d7(y, x);
    end = esp_cpu_get_ccount();
    diff = (end - start) / 10;
    Serial.printf("atan2_fast_d7: %d cycles / %0.4f ms\n", diff, (diff) / CPU_FREQ);

    start = esp_cpu_get_ccount();
    float ans4 = atan2_fast_d9(y, x); ans4 += atan2_fast_d9(y, x); ans4 += atan2_fast_d9(y, x); ans4 += atan2_fast_d9(y, x); ans4 += atan2_fast_d9(y, x);
    ans4 += atan2_fast_d9(y, x); ans4 += atan2_fast_d9(y, x); ans4 += atan2_fast_d9(y, x); ans4 += atan2_fast_d9(y, x); ans4 += atan2_fast_d9(y, x);
    end = esp_cpu_get_ccount();
    diff = (end - start) / 10;
    Serial.printf("atan2_fast_d9: %d cycles / %0.4f ms\n", diff, (diff) / CPU_FREQ);

    Serial.printf("atan2f: %f, atan2_fast_d5: %f, atan2_fast_d7: %f, atan2_fast_d9: %f\n", ans1/10, ans2/10, ans3/10, ans4/10);

    float max_d5_err = fabsf(ans1 - ans2);
    float max_d7_err = fabsf(ans1 - ans3);
    float max_d9_err = fabsf(ans1 - ans4);
    float sum_d5_err = 0.0;
    float sum_d7_err = 0.0;
    float sum_d9_err = 0.0;
    for (int i = 0; i < 1000000; i++) {
        x = random() / (float)RAND_MAX;
        y = random() / (float)RAND_MAX;
        ans1 = atan2f(y, x);
        ans2 = atan2_fast_d5(y, x);
        ans3 = atan2_fast_d7(y, x);
        ans4 = atan2_fast_d9(y, x);
        max_d5_err = fmaxf(max_d5_err, fabsf(ans1 - ans2));
        max_d7_err = fmaxf(max_d7_err, fabsf(ans1 - ans3));
        max_d9_err = fmaxf(max_d9_err, fabsf(ans1 - ans4));
        sum_d5_err += fabsf(ans1 - ans2);
        sum_d7_err += fabsf(ans1 - ans3);
        sum_d9_err += fabsf(ans1 - ans4);
    }

    Serial.printf("Max error for atan2_fast_d5: %.9f, atan2_fast_d7: %.9f, atan2_fast_d9: %.9f\n", max_d5_err, max_d7_err, max_d9_err);
    Serial.printf("Average error for atan2_fast_d5: %.9f, atan2_fast_d7: %.9f, atan2_fast_d9: %.9f\n", sum_d5_err / 1000000.0, sum_d7_err / 1000000.0, sum_d9_err / 1000000.0);
 */

const static uint16_t sinTable16[] = {
    0, 1145, 2289, 3435, 4572, 5716, 6853, 7989, 9125, 10255, 11385,
    12508, 13631, 14745, 15859, 16963, 18067, 19165, 20253, 21342, 22417,
    23489, 24553, 25610, 26659, 27703, 28731, 29755, 30773, 31777, 32772,
    33756, 34734, 35697, 36649, 37594, 38523, 39445, 40350, 41247, 42131,
    42998, 43856, 44701, 45528, 46344, 47147, 47931, 48708, 49461, 50205,
    50933, 51646, 52342, 53022, 53686, 54334, 54969, 55579, 56180, 56760,
    57322, 57866, 58394, 58908, 59399, 59871, 60327, 60768, 61184, 61584,
    61969, 62330, 62677, 63000, 63304, 63593, 63858, 64108, 64334, 64545,
    64731, 64903, 65049, 65177, 65289, 65377, 65449, 65501, 65527, 65535,
    65535
};

 /**
  * Fast sine and cosine of an angle in degrees. This uses a precomputed 16-bit
  * sine table for degrees from 0 to 90 degrees.
  * 
  * From https://github.com/RobTillaart/FastTrig. Note: the original library
  * provides code to generate the table including ones with more bits for
  * higher accuracy if needed.
  */
static void OPTIMIZE_FOR_SPEED sincos_fast(float degrees, float *sine, float *cosine) {
    bool sin_neg = (degrees < 0);
    bool cos_neg = false;
    if (sin_neg) { degrees = -degrees; }

    long whole = (long)degrees;
    uint8_t remain = (degrees - whole) * 256;
    if (whole >= 360) { whole %= 360; }

    // deal with quadrants
    int y = whole;  // shrink data type since number is always in [0, 360)
    if (y >= 180) { y -= 180; sin_neg = !sin_neg; cos_neg = !cos_neg; }
    if (y >= 90) {
        y = 180 - y;
        if (remain != 0) { remain = -remain; y--; }
        cos_neg = !cos_neg;
    }

    // float value improves ~4% on average error for ~60 bytes.
    // SIN
    uint16_t value = sinTable16[y];
    // interpolate if needed
    if (remain > 0) { value += ((sinTable16[y+1] - value) / 8 * remain) / 32; } //  == * remain / 256
    if (sin_neg) { value = -value; }
    *sine = value * 0.0000152590219f;  //  = / 65535.0

    // COS
    value = sinTable16[90-y];
    if (remain > 0)
    {
        value = sinTable16[89-y];
        remain = 256 - remain;
        value += ((sinTable16[90-y] - value) / 8 * remain) / 32;  //  == * remain / 256
    }
    if (cos_neg) { value = -value; }
    *cosine = value * 0.0000152590219f;  //  = / 65535.0
}


/////////////////////////////////////
////////// Complex Numbers //////////
/////////////////////////////////////
 #include <complex.h>
typedef float _Complex cfloat;

/** Computes cabsf(x)^2, avoiding the sqrtf call. */
static inline OPTIMIZE_FOR_SPEED float cabs2(cfloat x) { return crealf(x) * crealf(x) + cimagf(x) * cimagf(x); }

/** Computes cargf(x) using fast atan2(). */
// TODO: tried d9 version but it was slower than using atanf() directly here?
static inline OPTIMIZE_FOR_SPEED float carg_fast(cfloat x) { return atan2_fast_d7(cimagf(x), crealf(x)); }

/**
 * Computes the imaginary exponential of x using fast trigronometry functions:
 *       iexp_fast(x) = cos(x) + i*sin(x)
 * See cexp_fast() for using any complex number.
 */
static inline OPTIMIZE_FOR_SPEED cfloat iexp_fast(float x) {
    //return cos(x) + I * sin(x);
    float real, imag;
    sincos_fast(x * (180.0f / PI_), &imag, &real);
    return real + I * imag;
}

/**
 * Computes the complex exponential of x using fast trigonometry and exp():
 *       cexp_fast(x) = exp(real(x)) * iexp_fast(imag(x))
 * This is faster than using the standard cexpf() function. This uses the
 * order-1 exp() function.
 */
//static inline OPTIMIZE_FOR_SPEED cfloat cexp_fast(cfloat x) { return exp_fast_o1(crealf(x)) * iexp_fast(cimagf(x)); }
