/* threshcrypt shares.c
 * Copyright 2012 Ryan Castellucci <code@ryanc.org>
 * This software is published under the terms of the Simplified BSD License.
 * Please see the 'COPYING' file for details.
 * Portions of this file are derived from libgfshare examples which are
 * Copyright Daniel Silverstone <dsilvers@digital-scurf.org> 2006-2011
*/

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <libgfshare.h>
#include <tomcrypt.h>

#include "common.h"
#include "util.h"
#include "shares.h"
#include "crypt.h"

/* put n random non-zero uchars into *sharenrs with no duplicates */
void gen_sharenrs(unsigned char *sharenrs, unsigned char n) {
  int i;
  unsigned int  r;
  unsigned char t;
  unsigned char buf[255];

  for (i = 0; i < 255; i++) buf[i] = i + 1;

  while(i > 0) {
    do {
      fill_prng((unsigned char *)&r, sizeof(r));
    } while (r > (UINT_MAX - (UINT_MAX % i) - 1)); /* avoids bias */
    r %= i--;

    t = buf[i];
    buf[i] = buf[r];
    buf[r] = t;
  }
  
  assert(i == 0);

  while (i < n) {
    sharenrs[i] = buf[i];
    i++;
  }
}

int tc_gfsplit(header_data_t *header) {
  int err;
  unsigned int i;
  share_data_t *share;
  gfshare_ctx *G = NULL;

  assert(header->nshares > 0);
  assert(header->thresh > 0);
  assert(header->thresh <= header->nshares);

  unsigned char *sharenrs = safe_malloc(header->nshares);

  /* master key setup */
  fill_rand(header->master_key,  header->key_size);
  fill_prng(header->master_salt, SALT_SIZE);
  /* Add (hopefully) some extra protection against potentially weak RNG
   * TODO use a hash for this... */
  for (i = 0; i < header->nshares; i++ ) {
    share = &(header->shares[i]);
    memxor(header->master_key, share->key, header->key_size);
  }

  size_t hmac_size = header->hmac_size;
  if ((err = pbkdf2(header->master_key, header->key_size,
                    header->master_salt, SALT_SIZE, SUBKEY_ITER,
                    find_hash("sha256"), header->master_hmac, &hmac_size)) != CRYPT_OK) {
    fprintf(stderr, "PBKDF2 failed: %d\n", err);
  }
  /* end master key setup */

#ifdef CRYPTODEBUG
  fprintf(stderr, "MasterSlt: ");
  for (i = 0;i < SALT_SIZE;i++) {
    fprintf(stderr, "%02x", header->master_salt[i]);
  }
  fprintf(stderr, "\n");
 
  fprintf(stderr, "MasterKey: ");
  for (i = 0;i < header->key_size;i++) {
    fprintf(stderr, "%02x", header->master_key[i]);
  }
  fprintf(stderr, "\n");
  
  fprintf(stderr, "MasterMAC: ");
  for (i = 0;i < header->hmac_size;i++) {
    fprintf(stderr, "%02x", header->master_hmac[i]);
  }
  fprintf(stderr, "\n\n");
#endif

  if (header->thresh > 1) {
    gen_sharenrs(sharenrs, header->nshares);
    /* TODO figure out how to mlock G's memory */
    G = gfshare_ctx_init_enc(sharenrs, header->nshares, header->thresh, header->key_size);
    /* G->buffer could be free'd and reinitialized with size G->buffersize - wrapper? */
    if (G == NULL) {
      perror("gfshare_ctx_init_enc");
      return -1;
    }
    gfshare_ctx_enc_setsecret(G, header->master_key);
  }

  for (i = 0; i < header->nshares; i++ ) {
    share = &(header->shares[i]);
    if (share->ptxt == NULL)
      share->ptxt = keymem_alloc(header->keymem, header->share_size);
    share->ctxt = safe_malloc(header->share_size);
    share->hmac = safe_malloc(header->hmac_size);

    share->ptxt[0] = sharenrs[i];
    if (header->thresh == 1) {
      memcpy(share->ptxt + 1, header->master_key, header->key_size);
    } else {
      gfshare_ctx_enc_getshare(G, i, share->ptxt + 1);
    }

#ifdef CRYPTODEBUG
    fprintf(stderr, "Salt [%02x]: ", i);
    for (j = 0;j < SALT_SIZE;j++) {
      fprintf(stderr, "%02x", share->salt[j]);
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "Key  [%02x]: ", i);
    for (j = 0;j < header->key_size;j++) {
      fprintf(stderr, "%02x", share->key[j]);
    }
    fprintf(stderr, "\n"); 
    
    fprintf(stderr, "Share[%02x]: ", i);
    for (j = 0;j < header->share_size;j++) {
      fprintf(stderr, "%02x", share->ptxt[j]);
    }
    fprintf(stderr, "\n");
#endif /* CRYPTODEBUG */

    if ((err = encrypt_data(share->ptxt, share->ctxt, header->share_size,
                            share->key,  header->key_size,
                            share->hmac, header->hmac_size)) != 0) {
      fprintf(stderr, "Encrypt failed: %d\n", err);
    }

#ifdef CRYPTODEBUG
    fprintf(stderr, "Crypt[%02x]: ", i);
    for (j = 0;j < header->share_size;j++) {
      
      fprintf(stderr, "%02x", share->ctxt[j]);
    }
    fprintf(stderr, "\n");
#endif /* CRYPTODEBUG */

#ifdef TESTDECRYPT
    if ((err = decrypt_data(share->ctxt, share->ptxt, header->share_size,
                            share->key,  header->key_size,
                            share->hmac, header->hmac_size)) != 0) {
      fprintf(stderr, "Decrypt failed: %d\n", err);
    }
#ifdef CRYPTODEBUG
    fprintf(stderr, "Plain[%02x]: ", i);
    for (j = 0;j < header->share_size;j++) {
      fprintf(stderr, "%02x", share->ptxt[j]);
    }
    fprintf(stderr, "\n");
#endif /* CRYPTODEBUG */
#endif /* TESTDECRYPT */
    MEMWIPE(share->ptxt, header->share_size);
    MEMWIPE(share->key,  header->key_size);
#ifdef CRYPTODEBUG
    fprintf(stderr, "MAC  [%02x]: ", i);
    for (j = 0;j < header->hmac_size;j++) {
      fprintf(stderr, "%02x", share->hmac[j]);
    }
    fprintf(stderr, "\n\n");
#endif /* CRYPTODEBUG */
  }

  /* wipe sensitive data and free memory */
  if (header->thresh > 1) {
    gfshare_ctx_free(G);
  }
  wipe_free(sharenrs, header->nshares);
  /* header->master_key stays so that it can be used for file encryption */
  return 0;
}

int unlock_shares(const unsigned char *pass, size_t pass_len, header_data_t *header) {
  int i, err, ret;
  ret = 0;
  if (header->tmp_share_key == NULL)
    header->tmp_share_key  = keymem_alloc(header->keymem, header->key_size);
  if (header->tmp_share_ptxt == NULL)
    header->tmp_share_ptxt  = keymem_alloc(header->keymem, header->share_size);

  for (i = 0; i < header->nshares; i++) {
    share_data_t *share;
    share = &(header->shares[i]);

    assert(share->key == NULL);
    if (share->ptxt == NULL) {
      share->key  = header->tmp_share_key;
      share->ptxt = header->tmp_share_ptxt;

      unsigned long key_size;
      key_size = header->key_size;

      fprintf(stderr, "\033[0G\033[2KChecking share %d", i);
      if ((err = pbkdf2(pass, pass_len, share->salt, SALT_SIZE, share->iter,
                        find_hash("sha256"), share->key, &key_size)) != CRYPT_OK) {
        /* on an hmac failure (wrong password) */
        MEMWIPE(share->key, header->key_size);
        continue;
      }
      if ((err = decrypt_data(share->ctxt, share->ptxt, header->share_size,
                              share->key,  header->key_size,
                              share->hmac, header->hmac_size)) == 0) {
        fprintf(stderr, "\033[0G\033[2KUnlocked share %d\n", i);
        /* new ptxt region for the next share */
        header->tmp_share_ptxt = keymem_alloc(header->keymem, header->share_size);
        ret++;
      } else {
        MEMWIPE(share->ptxt, header->share_size);
        share->ptxt = NULL;
      }
      MEMWIPE(share->key, header->key_size);
      share->key = NULL;
    }
  }
  fprintf(stderr, "\033[0G\033[2K");
  return ret;
}

int tc_gfcombine(header_data_t *header) {
  int i, loaded, err, ret;
  err = ret = loaded = 0;
  
  assert(header->master_key == NULL);
  header->master_key = keymem_alloc(header->keymem, header->key_size);

  if (header->thresh == 1) {
    for (i = 0; i < header->nshares; i++) {
      share_data_t *share;
      share = &(header->shares[i]);
      if (share->ptxt != NULL) {
        memcpy(header->master_key, share->ptxt + 1, header->key_size);
        break;
      }
    }
  } else {
    gfshare_ctx *G;
    unsigned char sharenrs[256];
    MEMZERO(sharenrs, sizeof(sharenrs));

    /* Initialize a gfshare context for decoding */
    G = gfshare_ctx_init_dec(sharenrs, header->thresh, header->key_size);

    for (i = 0; i < header->nshares; i++) {
      share_data_t *share;
      share = &(header->shares[i]);
      if (share->ptxt != NULL) {
        sharenrs[loaded] = share->ptxt[0];
        gfshare_ctx_dec_newshares(G, sharenrs);
        gfshare_ctx_dec_giveshare(G, loaded, share->ptxt + 1);
        if (++loaded == header->thresh)
          break;
      }
    }
    gfshare_ctx_dec_extract(G, header->master_key);
    gfshare_ctx_free(G);
  }
  return ret;
}

/* vim: set ts=2 sw=2 et ai si: */
