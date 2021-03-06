//****************************************************************************
//
// Copyright (c) ALTAP, spol. s r.o. All rights reserved.
//
// This is a part of the Altap Salamander SDK library.
//
// The SDK is provided "AS IS" and without warranty of any kind and 
// ALTAP EXPRESSLY DISCLAIMS ALL WARRANTIES, EXPRESS AND IMPLIED, INCLUDING,
// BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE and NON-INFRINGEMENT.
//
//****************************************************************************

#include "precomp.h"

#include "openssl_helpers.h"

#include <openssl/err.h>
#include <openssl/buffer.h>
#include <openssl/pkcs12.h>
#include <openssl/asn1_mac.h>

// prints out a separator between certificates
void PrintSeparator(BIO *bio_out)
{
	BIO_printf(bio_out, "\n\n=======================================================================\n\n");
}


// static function taken from crypto/asn1/a_d2i_fp.c

#define HEADER_SIZE   8
#define ASN1_CHUNK_INITIAL_SIZE (16 * 1024)
int asn1_d2i_read_bio(BIO *in, BUF_MEM **pb)
{
	BUF_MEM *b;
	unsigned char *p;
	int i;
	ASN1_const_CTX c;
	size_t want = HEADER_SIZE;
	int eos = 0;
	size_t off = 0;
	size_t len = 0;

	b = BUF_MEM_new();
	if (b == NULL) {
		ASN1err(ASN1_F_ASN1_D2I_READ_BIO, ERR_R_MALLOC_FAILURE);
		return -1;
	}

	ERR_clear_error();
	for (;;) {
		if (want >= (len - off)) {
			want -= (len - off);

			if (len + want < len || !BUF_MEM_grow_clean(b, len + want)) {
				ASN1err(ASN1_F_ASN1_D2I_READ_BIO, ERR_R_MALLOC_FAILURE);
				goto err;
			}
			i = BIO_read(in, &(b->data[len]), want);
			if ((i < 0) && ((len - off) == 0)) {
				ASN1err(ASN1_F_ASN1_D2I_READ_BIO, ASN1_R_NOT_ENOUGH_DATA);
				goto err;
			}
			if (i > 0) {
				if (len + i < len) {
					ASN1err(ASN1_F_ASN1_D2I_READ_BIO, ASN1_R_TOO_LONG);
					goto err;
				}
				len += i;
			}
		}
		/* else data already loaded */

		p = (unsigned char *)&(b->data[off]);
		c.p = p;
		c.inf = ASN1_get_object(&(c.p), &(c.slen), &(c.tag), &(c.xclass),
								len - off);
		if (c.inf & 0x80) {
			unsigned long e;

			e = ERR_GET_REASON(ERR_peek_error());
			if (e != ASN1_R_TOO_LONG)
				goto err;
			else
				ERR_clear_error(); /* clear error */
		}
		i = c.p - p;            /* header length */
		off += i;               /* end of data */

		if (c.inf & 1) {
			/* no data body so go round again */
			eos++;
			if (eos < 0) {
				ASN1err(ASN1_F_ASN1_D2I_READ_BIO, ASN1_R_HEADER_TOO_LONG);
				goto err;
			}
			want = HEADER_SIZE;
		} else if (eos && (c.slen == 0) && (c.tag == V_ASN1_EOC)) {
			/* eos value, so go back and read another header */
			eos--;
			if (eos <= 0)
				break;
			else
				want = HEADER_SIZE;
		} else {
			/* suck in c.slen bytes of data */
			want = c.slen;
			if (want > (len - off)) {
				size_t chunk_max = ASN1_CHUNK_INITIAL_SIZE;

				want -= (len - off);
				if (want > INT_MAX /* BIO_read takes an int length */  ||
					len + want < len) {
					ASN1err(ASN1_F_ASN1_D2I_READ_BIO, ASN1_R_TOO_LONG);
					goto err;
				}
				while (want > 0) {
					/*
					 * Read content in chunks of increasing size
					 * so we can return an error for EOF without
					 * having to allocate the entire content length
					 * in one go.
					 */
					size_t chunk = want > chunk_max ? chunk_max : want;

					if (!BUF_MEM_grow_clean(b, len + chunk)) {
						ASN1err(ASN1_F_ASN1_D2I_READ_BIO, ERR_R_MALLOC_FAILURE);
						goto err;
					}
					want -= chunk;
					while (chunk > 0) {
						i = BIO_read(in, &(b->data[len]), chunk);
						if (i <= 0) {
							ASN1err(ASN1_F_ASN1_D2I_READ_BIO,
									ASN1_R_NOT_ENOUGH_DATA);
							goto err;
						}
					/*
					 * This can't overflow because |len+want| didn't
					 * overflow.
					 */
						len += i;
						chunk -= i;
					}
					if (chunk_max < INT_MAX/2)
						chunk_max *= 2;
				}
			}
			if (off + c.slen < off) {
				ASN1err(ASN1_F_ASN1_D2I_READ_BIO, ASN1_R_TOO_LONG);
				goto err;
			}
			off += c.slen;
			if (eos <= 0) {
				break;
			} else
				want = HEADER_SIZE;
		}
	}

	if (off > INT_MAX) {
		ASN1err(ASN1_F_ASN1_D2I_READ_BIO, ASN1_R_TOO_LONG);
		goto err;
	}

	*pb = b;
	return off;
 err:
	if (b != NULL)
		BUF_MEM_free(b);
	return -1;
}

// code taken from apps/pkcs7.c from `if (print_certs)` block

void PKCS7_print_certs(BIO *bio_out, const PKCS7 *pkcs7)
{
	STACK_OF(X509) *certs = NULL;
	STACK_OF(X509_CRL) *crls = NULL;

	BIO_printf(bio_out, "=======================================================================\n\n");

	auto i = OBJ_obj2nid(pkcs7->type);
	switch (i) {
	case NID_pkcs7_signed:
		if (pkcs7->d.sign != NULL)
		{
			certs = pkcs7->d.sign->cert;
			crls = pkcs7->d.sign->crl;
		}
		break;

	case NID_pkcs7_signedAndEnveloped:
		if (pkcs7->d.signed_and_enveloped != NULL)
		{
			certs = pkcs7->d.signed_and_enveloped->cert;
			crls = pkcs7->d.signed_and_enveloped->crl;
		}
		break;

	default:
		break;
	}

	auto num_certs = 0U;

	if (certs)
	{
		for (auto i = 0; i < sk_X509_num(certs); i++)
		{
			if (num_certs)
				PrintSeparator(bio_out);
			X509_print(bio_out, sk_X509_value(certs, i));
			num_certs++;
		}
	}

	if (crls)
	{
		for (auto i = 0; i < sk_X509_CRL_num(crls); i++)
		{
			if (num_certs)
				PrintSeparator(bio_out);
			X509_CRL_print(bio_out, sk_X509_CRL_value(crls, i));
			num_certs++;
		}
	}
}
