#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    double re;
    double im;
} cplx;

typedef struct {
    size_t rows;
    size_t cols;
    cplx *data;
} matrix;

static int matrix_alloc(matrix *m, size_t rows, size_t cols)
{
    m->rows = rows;
    m->cols = cols;
    m->data = (cplx *)calloc(rows * cols, sizeof(cplx));
    return m->data != NULL;
}

static void matrix_free(matrix *m)
{
    free(m->data);
    m->data = NULL;
}

static void matrix_fill(matrix *m, unsigned seed)
{
    unsigned s = seed;
    size_t n = m->rows * m->cols;
    for (size_t i = 0; i < n; i++) {
        m->data[i].re = (double)(rand_r(&s) % 2001) / 1000.0 - 1.0;
        m->data[i].im = (double)(rand_r(&s) % 2001) / 1000.0 - 1.0;
    }
}

static void multiply_rows(const matrix *a, const matrix *b, matrix *c, size_t row_begin, size_t row_end)
{
    const size_t m = a->cols;
    const size_t k = b->cols;
    for (size_t i = row_begin; i < row_end; i++) {
        cplx *crow = &c->data[i * k];
        for (size_t j = 0; j < k; j++) {
            crow[j].re = 0.0;
            crow[j].im = 0.0;
        }
        for (size_t p = 0; p < m; p++) {
            const double ar = a->data[i * m + p].re;
            const double ai = a->data[i * m + p].im;
            const cplx *brow = &b->data[p * k];
            for (size_t j = 0; j < k; j++) {
                crow[j].re += ar * brow[j].re - ai * brow[j].im;
                crow[j].im += ar * brow[j].im + ai * brow[j].re;
            }
        }
    }
}

static void multiply_seq(const matrix *a, const matrix *b, matrix *c)
{
    multiply_rows(a, b, c, 0, a->rows);
}

typedef struct {
    const matrix *a;
    const matrix *b;
    matrix *c;
    size_t chunk;
    size_t next_row;
    pthread_mutex_t lock;
} shared_t;

static void *worker(void *arg)
{
    shared_t *sh = (shared_t *)arg;
    const size_t rows = sh->a->rows;
    for (;;) {
        pthread_mutex_lock(&sh->lock);
        size_t begin = sh->next_row;
        size_t end = begin + sh->chunk;
        if (begin < rows) {
            if (end > rows)
                end = rows;
            sh->next_row = end;
        }
        pthread_mutex_unlock(&sh->lock);
        if (begin >= rows)
            break;
        multiply_rows(sh->a, sh->b, sh->c, begin, end);
    }
    return NULL;
}

static int multiply_par(const matrix *a, const matrix *b, matrix *c, unsigned max_threads, size_t chunk, double pause_sec)
{
    size_t nthreads = max_threads;
    if (nthreads > a->rows)
        nthreads = a->rows;
    if (nthreads == 0 || chunk == 0)
        return 0;
    shared_t sh;
    sh.a = a;
    sh.b = b;
    sh.c = c;
    sh.chunk = chunk;
    sh.next_row = 0;
    if (pthread_mutex_init(&sh.lock, NULL) != 0)
        return -1;
    pthread_t *tid = (pthread_t *)malloc(nthreads * sizeof(pthread_t));
    if (!tid) {
        pthread_mutex_destroy(&sh.lock);
        return -1;
    }
    size_t created = 0;
    for (size_t i = 0; i < nthreads; i++) {
        if (pthread_create(&tid[created], NULL, worker, &sh) == 0)
            created++;
    }

    if (created < nthreads)
        worker(&sh);
    if (pause_sec > 0.0) {
        struct timespec ts;
        ts.tv_sec  = (time_t)pause_sec;
        ts.tv_nsec = (long)((pause_sec - (double)ts.tv_sec) * 1e9);
        nanosleep(&ts, NULL);
    }
    for (size_t i = 0; i < created; i++)
        pthread_join(tid[i], NULL);
    free(tid);
    pthread_mutex_destroy(&sh.lock);
    return 0;
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static double max_abs_diff(const matrix *x, const matrix *y)
{
    double d = 0.0;
    size_t n = x->rows * x->cols;
    for (size_t i = 0; i < n; i++) {
        double dr = x->data[i].re - y->data[i].re;
        double di = x->data[i].im - y->data[i].im;
        if (dr < 0) dr = -dr;
        if (di < 0) di = -di;
        if (dr > d) d = dr;
        if (di > d) d = di;
    }
    return d;
}

static void matrix_print(const char *name, const matrix *m){
    printf("%s (%zux%zu):\n", name, m->rows, m->cols);
    for (size_t i = 0; i < m->rows; i++){
	for (size_t j = 0; j < m->cols; j++){
		const cplx *z = &m->data[i * m->cols + j];
		printf(" %7.3f%+7.3fi", z->re, z->im);
		}
		printf("\n");
	}
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Использование: %s [-n N] [-m M] [-k K] [-t THREADS] [-s SEED] "
            "[-r REPEATS] [-c CHUNK] [-w SEC] [-v]\n", prog);
}

int main(int argc, char **argv)
{
    size_t n = 500, m = 0, k = 0;
    unsigned threads = 4;
    unsigned seed = 12345u;
    int repeats = 1;
    size_t chunk = 4;
    int verify = 0;
    int print = 0;
    double pause_sec = 0.0;
    int opt;
    while ((opt = getopt(argc, argv, "n:m:k:t:s:r:c:w:pv")) != -1) {
        switch (opt) {
        case 'n': n = (size_t)strtoull(optarg, NULL, 10); break;
        case 'm': m = (size_t)strtoull(optarg, NULL, 10); break;
        case 'k': k = (size_t)strtoull(optarg, NULL, 10); break;
        case 't': threads = (unsigned)strtoul(optarg, NULL, 10); break;
        case 's': seed = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'r': repeats = atoi(optarg); break;
        case 'c': chunk = (size_t)strtoull(optarg, NULL, 10); break;
        case 'w': pause_sec = atof(optarg); break;
	case 'p': print = 1; break;
        case 'v': verify = 1; break;
        default:  usage(argv[0]); return 2;
        }
    }
    if (m == 0) m = n;
    if (k == 0) k = n;
    if (n == 0 || m == 0 || k == 0 || threads == 0 || repeats < 1 || chunk == 0) {
        fprintf(stderr, "Ошибка: размеры, -t, -r и -c должны быть > 0\n");
        usage(argv[0]);
        return 2;
    }
    matrix a, b, c_seq, c_par;
    if (!matrix_alloc(&a, n, m) || !matrix_alloc(&b, m, k) ||
        !matrix_alloc(&c_seq, n, k) || !matrix_alloc(&c_par, n, k)) {
        fprintf(stderr, "Ошибка: не хватает памяти\n");
        return 1;
    }
    matrix_fill(&a, seed);
    matrix_fill(&b, seed ^ 0x9E3779B9u);

    printf("A: %zux%zu, B: %zux%zu, потоков (максимум): %u, PID: %ld\n",
           n, m, m, k, threads, (long)getpid());
    double t_seq = 1e300, t_par = 1e300;
    for (int r = 0; r < repeats; r++) {
        double t0 = now_sec();
        multiply_seq(&a, &b, &c_seq);
        double dt = now_sec() - t0;
        if (dt < t_seq) t_seq = dt;
    }

    for (int r = 0; r < repeats; r++) {
        double t0 = now_sec();
        if (multiply_par(&a, &b, &c_par, threads, chunk, pause_sec) != 0) {
            fprintf(stderr, "Ошибка параллельного умножения\n");
            return 1;
        }
        double dt = now_sec() - t0;
        if (pause_sec > 0.0) dt -= pause_sec;
        if (dt < t_par) t_par = dt;
    }
    double speedup = t_seq / t_par;
    double eff = speedup / (double)threads;
    printf("Последовательно: %.6f c\n", t_seq);
    printf("Параллельно:     %.6f c\n", t_par);
    printf("Ускорение S = %.3f, эффективность E = %.3f\n", speedup, eff);
    if (print){
	matrix_print("A", &a);
	matrix_print("B", &b);
	matrix_print("C = A * B", &c_par);
    }
    if (verify) {
        double d = max_abs_diff(&c_seq, &c_par);
        printf("Макс. расхождение результатов: %g (%s)\n", d,
               d == 0.0 ? "идентичны" : "различаются");
    }
    matrix_free(&a);
    matrix_free(&b);
    matrix_free(&c_seq);
    matrix_free(&c_par);
    return 0;
}
