/*
 * simple utility functions
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <pthread.h>

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "daapd.h"
#include "err.h"
#include "util.h"


/* Globals */
pthread_mutex_t util_locks[(int)l_last];
pthread_mutex_t util_mutex = PTHREAD_MUTEX_INITIALIZER;
int _util_initialized=0;


/* Forwards */
void _util_mutex_init(void);

/**
 * Simple hash generator
 */
uint32_t util_djb_hash_block(unsigned char *data, uint32_t len) {
    uint32_t hash = 5381;
    unsigned char *pstr = data;

    while(len--) {
        hash = ((hash << 5) + hash) + *pstr;
        pstr++;
    }
    return hash;
}

/**
 * simple hash generator
 */
uint32_t util_djb_hash_str(char *str) {
    uint32_t len;

    len = (uint32_t)strlen(str);
    return util_djb_hash_block((unsigned char *)str,len);
}

/**
 * Dumb utility function that should probably be somehwere else
 */
int util_must_exit(void) {
    return config.stop;
}

void util_hexdump(unsigned char *block, int len) {
    char charmap[256];
    int index;
    int row, offset;
    char output[80];
    char tmp[20];

    memset(charmap,'.',sizeof(charmap));

    for(index=' ';index<'~';index++) charmap[index]=index;
    for(row=0;row<(len+15)/16;row++) {
        sprintf(output,"%04X: ",row*16);
        for(offset=0; offset < 16; offset++) {
            if(row * 16 + offset < len) {
                sprintf(tmp,"%02X ",block[row*16 + offset]);
            } else {
                sprintf(tmp,"   ");
            }
            strcat(output,tmp);
        }

        for(offset=0; offset < 16; offset++) {
            if(row * 16 + offset < len) {
                sprintf(tmp,"%c",charmap[block[row*16 + offset]]);
            } else {
                sprintf(tmp," ");
            }
            strcat(output,tmp);
        }

        DPRINTF(E_LOG,L_MISC,"%s\n",output);
    }
}

/**
 * simple mutex wrapper for better debugging
 */
void util_mutex_lock(ff_lock_t which) {
    if(!_util_initialized)
        _util_mutex_init();

    if(pthread_mutex_lock(&util_locks[(int)which])) {
        fprintf(stderr,"Cannot lock mutex\n");
        exit(-1);
    }
}

/**
 * simple mutex wrapper for better debugging
 */
void util_mutex_unlock(ff_lock_t which) {
    if(pthread_mutex_unlock(&util_locks[(int)which])) {
        fprintf(stderr,"Cannot unlock mutex\n");
        exit(-1);
    }

}

/**
 * mutex initializer.  This might should be done from the
 * main thread.
 */
void _util_mutex_init(void) {
    int err;
    ff_lock_t lock;

    if((err = pthread_mutex_lock(&util_mutex))) {
        fprintf(stderr,"Error locking mutex\n");
        exit(-1);
    }

    if(!_util_initialized) {
        /* now, walk through and manually initialize the mutexes */
        for(lock=(ff_lock_t)0; lock < l_last; lock++) {
            if((err = pthread_mutex_init(&util_locks[(int)lock],NULL))) {
                fprintf(stderr,"Error initializing mutex\n");
                exit(-1);
            }
        }
        _util_initialized=1;
    }

    pthread_mutex_unlock(&util_mutex);
}

/**
 * split a string on delimiter boundaries, filling
 * a string-pointer array.
 *
 * The user must free both the first element in the array,
 * and the array itself.
 *
 * @param s string to split
 * @param delimiters boundaries to split on
 * @param argvp an argv array to be filled
 * @returns number of tokens
 */
int util_split(char *s, char *delimiters, char ***argvp) {
    int i;
    int numtokens;
    const char *snew;
    char *t;
    char *tokptr;
    char *tmp;
    char *fix_src, *fix_dst;

    if ((s == NULL) || (delimiters == NULL) || (argvp == NULL))
        return -1;
    *argvp = NULL;
    snew = s + strspn(s, delimiters);
    if ((t = malloc(strlen(snew) + 1)) == NULL)
        return -1;

    strcpy(t, snew);
    numtokens = 1;
    tokptr = NULL;
    tmp = t;

    tmp = s;
    while(*tmp) {
        if(strchr(delimiters,*tmp) && (*(tmp+1) == *tmp)) {
            tmp += 2;
        } else if(strchr(delimiters,*tmp)) {
            numtokens++;
            tmp++;
        } else {
            tmp++;
        }
    }

    DPRINTF(E_DBG,L_CONF,"Found %d tokens in %s\n",numtokens,s);

    if ((*argvp = malloc((numtokens + 1)*sizeof(char *))) == NULL) {
        free(t);
        return -1;
    }

    if (numtokens == 0)
        free(t);
    else {
        tokptr = t;
        tmp = t;
        for (i = 0; i < numtokens; i++) {
            while(*tmp) {
                if(strchr(delimiters,*tmp) && (*(tmp+1) != *tmp))
                    break;
                if(strchr(delimiters,*tmp)) {
                    tmp += 2;
                } else {
                    tmp++;
                }
            }
            *tmp = '\0';
            tmp++;
            (*argvp)[i] = tokptr;

            fix_src = fix_dst = tokptr;
            while(*fix_src) {
                if(strchr(delimiters,*fix_src) && (*(fix_src+1) == *fix_src)) {
                    fix_src++;
                }
                *fix_dst++ = *fix_src++;
            }
            *fix_dst = '\0';

            tokptr = tmp;
            DPRINTF(E_DBG,L_CONF,"Token %d: %s\n",i+1,(*argvp)[i]);
        }
    }

    *((*argvp) + numtokens) = NULL;
    return numtokens;
}

/**
 * dispose of the argv set that was created in util_split
 *
 * @param argv string array to delete
 */
void util_dispose_split(char **argv) {
    if(!argv)
        return;

    if(argv[0])
        free(argv[0]);

    free(argv);
}

/**
 * Write a formatted string to an allocated string.  Leverage
 * the existing util_vasprintf to do so
 */
char *util_asprintf(char *fmt, ...) {
    char *outbuf;
    va_list ap;

    ASSERT(fmt);

    if(!fmt)
        return NULL;

    va_start(ap,fmt);
    outbuf = util_vasprintf(fmt, ap);
    va_end(ap);

    return outbuf;
}

/**
 * Write a formatted string to an allocated string.  This deals with
 * versions of vsnprintf that return either the C99 way, or the pre-C99
 * way, by increasing the buffer until it works.
 *
 * @param
 * @param fmt format string of print (compatible with printf(2))
 * @returns TRUE on success
 */

#ifdef HAVE_VA_COPY
# define VA_COPY(a,b) va_copy((a),(b))
#else
# ifdef HAVE___VA_COPY
#  define VA_COPY(a,b) __va_copy((a),(b))
# else
#  define VA_COPY(a,b) memcpy((&a),(&b),sizeof(b))
# endif
#endif

char *util_vasprintf(char *fmt, va_list ap) {
    char *outbuf;
    char *newbuf;
    va_list ap2;
    int size=200;
    int new_size;

    outbuf = (char*)malloc(size);
    if(!outbuf)
        DPRINTF(E_FATAL,L_MISC,"Could not allocate buffer in vasprintf\n");

    VA_COPY(ap2,ap);

    while(1) {
        new_size=vsnprintf(outbuf,size,fmt,ap);

        if(new_size > -1 && new_size < size)
            break;

        if(new_size > -1)
            size = new_size + 1;
        else
            size *= 2;

        if((newbuf = realloc(outbuf,size)) == NULL) {
            free(outbuf);
            DPRINTF(E_FATAL,L_MISC,"malloc error in vasprintf\n");
            exit(1);
        }
        outbuf = newbuf;
        VA_COPY(ap,ap2);
    }

    return outbuf;
}
