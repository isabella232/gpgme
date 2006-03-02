/* context.h 
 *	Copyright (C) 2000 Werner Koch (dd9jn)
 *      Copyright (C) 2001, 2002 g10 Code GmbH
 *
 * This file is part of GPGME.
 *
 * GPGME is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GPGME is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef CONTEXT_H
#define CONTEXT_H

#include "gpgme.h"
#include "types.h"
#include "engine.h"

struct key_queue_item_s {
    struct key_queue_item_s *next;
    GpgmeKey key;
};
struct trust_queue_item_s {
    struct trust_queue_item_s *next;
    GpgmeTrustItem item;
};


/* Currently we need it at several places, so we put the definition 
 * into this header file */
struct gpgme_context_s {
    int initialized;
    int pending;   /* a gpg request is still pending */

    int use_cms;

    /* At some points we need to remember an error which we can't report
       immediately.  */
    GpgmeError error;   
    /* Cancel operation requested.  */
    int cancel;

    EngineObject engine; /* The running engine process.  */

    int verbosity;  /* level of verbosity to use */
    int use_armor;  
    int use_textmode;
    int keylist_mode;

    int signers_len;   /* The number of keys in signers.  */
    int signers_size;  /* size of the following array */
    GpgmeKey *signers;

    struct {
        VerifyResult verify;
        DecryptResult decrypt;
        SignResult sign;
        EncryptResult encrypt;
        PassphraseResult passphrase;
        ImportResult import;
        DeleteResult delete;
        GenKeyResult genkey;
    } result;

    GpgmeData notation;    /* last signature notation */
    GpgmeData op_info;     /* last operation info */

    GpgmeKey tmp_key;       /* used by keylist.c */
    volatile int key_cond;  /* something new is available */
    struct key_queue_item_s *key_queue;
    struct trust_queue_item_s *trust_queue;

    GpgmePassphraseCb passphrase_cb;
    void *passphrase_cb_value;

    GpgmeProgressCb progress_cb;
    void *progress_cb_value;

    GpgmeData help_data_1;
};


struct gpgme_data_s {
    size_t len;
    const char *data;
    GpgmeDataType type;
    GpgmeDataMode mode;

    int (*read_cb)( void *, char *, size_t, size_t *);
    void *read_cb_value;
    int read_cb_eof;

    size_t readpos;
    size_t writepos;
    size_t private_len;
    char *private_buffer;
};

struct user_id_s {
    struct user_id_s *next;
    unsigned int revoked:1;
    unsigned int invalid:1;
    GpgmeValidity validity; 
    const char *name_part;    /* all 3 point into strings behind name */
    const char *email_part;   /* or to read-only strings */
    const char *comment_part;
    char name[1];
};

struct gpgme_recipients_s {
    struct user_id_s *list;
    int checked;   /* wether the recipients are all valid */
};


#define fail_on_pending_request(c)                            \
          do {                                                \
                if (!(c))         return GPGME_Invalid_Value; \
                if ((c)->pending) return GPGME_Busy;          \
             } while (0)

#define wait_on_request_or_fail(c)                            \
          do {                                                \
                if (!(c))          return GPGME_Invalid_Value;\
                if (!(c)->pending) return GPGME_No_Request;   \
                gpgme_wait ((c), 1);                          \
             } while (0)

#endif /* CONTEXT_H */