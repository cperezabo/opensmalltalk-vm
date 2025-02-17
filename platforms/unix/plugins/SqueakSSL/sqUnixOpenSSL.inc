/* -*- mode: c; -*- */

#include "openssl_overlay.h"

#include <strings.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/param.h>

#ifdef __sun
/* MIN is not C stdlib - perhaps <sys/param.h> is defining it on Linux */
static int MIN(int a,int b) { return (a<b)?a:b; }
#endif

typedef struct sqSSL {
	int state;
	int certFlags;
	int loglevel;

	char *certName;
	char *peerName;
	char *serverName;

	SSL_METHOD *method;
	SSL_CTX *ctx;
	SSL *ssl;
	BIO *bioRead;
	BIO *bioWrite;
} sqSSL;


static bool wasInitialized = false;

static sqSSL **handleBuf = NULL;
static sqInt handleMax = 0;


#define MAX_HOSTNAME_LENGTH 253
enum sqMatchResult {
	MATCH_FOUND = 1, // matches OpenSSL X509_check_host
	NO_MATCH_DONE_YET = -1,
	NO_MATCH_FOUND = 0, // matches OpenSSL X509_check_host
	INVALID_IP_STRING = -2, // matches OpenSSL X509_check_ip_asc
	NO_SAN_PRESENT = -3
};

static char* emptyString = "";

/********************************************************************/
/********************************************************************/
/********************************************************************/

/* sslFromHandle: Maps a handle to an SSL */
static sqSSL *sslFromHandle(sqInt handle) {
	return handle < handleMax ? handleBuf[handle] : NULL;
}

/* sqCopyBioSSL: Copies data from a BIO into an out buffer */
sqInt sqCopyBioSSL(sqSSL *ssl, BIO *bio, char *dstBuf, sqInt dstLen) {
	int nbytes = sqo_BIO_ctrl_pending(bio);

	if(ssl->loglevel) printf("sqCopyBioSSL: %d bytes pending; buffer size %ld\n",
				nbytes, (long)dstLen);
	if(nbytes > dstLen) return -1;
	return sqo_BIO_read(bio, dstBuf, dstLen);
}


/* sqGetPeerCertificate: Tiny wrapper to find out which get_peer_cert to use */
static X509* sqGetPeerCertificate(const SSL* s)
{
#define DEBUG_FUNC(f) \
  DEBUG_PRINT("sqGetPeerCertificate: Using %s as get_cert func\n", #f)

  static bool initialized = false;
  static X509* (*_get_peer_certificate)(const SSL* s) = NULL;
  if (!initialized) {
    initialized = true;
    if (sqo_SSL_get1_peer_certificate) {
      DEBUG_FUNC(sqo_SSL_get1_peer_certificate);
      _get_peer_certificate = sqo_SSL_get1_peer_certificate;
    } else if (sqo_SSL_get_peer_certificate) {
      DEBUG_FUNC(sqo_SSL_get_peer_certificate);
      _get_peer_certificate = sqo_SSL_get_peer_certificate;
    }
  }
  if (_get_peer_certificate) {
    return _get_peer_certificate(s);
  } else {
    return NULL;
  }
#undef DEBUG_FUNC
}

enum sqMatchResult sqVerifyIP(sqSSL* ssl, X509* cert, const char* serverName, const size_t serverNameLength);
enum sqMatchResult sqVerifyDNS(sqSSL* ssl, X509* cert, const char* serverName, const size_t serverNameLength);
enum sqMatchResult sqVerifyNameInner(sqSSL* ssl, X509* cert, const void* serverName, const size_t serverNameLength, const int matchType);
char* sqVerifyFindStar(char* sANData, size_t sANDataSize);
sqInt sqVerifySAN(sqSSL* ssl, const GENERAL_NAME* sAN, const void* data, const size_t dataSizeIn, const int matchType);

enum sqMatchResult sqVerifyIP(sqSSL* ssl, X509* cert, const char* serverName, const size_t serverNameLength) {
	struct in6_addr addr = { 0 }; // placeholder, longest of in_addr and in6_addr
	int af = AF_INET6;
	size_t addrSize = sizeof(struct in6_addr);
	int strToAddrWorked = 0;

	if (serverName == NULL) { return INVALID_IP_STRING; }
	if (memchr(serverName, '.', MIN(INET_ADDRSTRLEN, serverNameLength))) {
		// there's a dot somewhere in the first bytes, look for IPV4
		af = AF_INET;
		addrSize = sizeof(struct in_addr);
	}
	strToAddrWorked = inet_pton(af, serverName, &addr);
	if (strToAddrWorked != 1) { return INVALID_IP_STRING; }

	return sqVerifyNameInner(ssl, cert, &addr, addrSize, GEN_IPADD);
}


enum sqMatchResult sqVerifyDNS(sqSSL* ssl, X509* cert, const char* serverName, const size_t serverNameLength) {
	return sqVerifyNameInner(ssl, cert, serverName, serverNameLength, GEN_DNS);
}

enum sqMatchResult sqVerifyNameInner(sqSSL* ssl, X509* cert, const void* serverName, const size_t serverNameLength, const int matchType) {
	enum sqMatchResult matchFound = NO_MATCH_FOUND;

	STACK_OF(GENERAL_NAME)* sANs = sqo_X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
	if (!sANs) {
		if (ssl->loglevel) printf("sqVerifyNameInner: No sAN names\n");
		matchFound = NO_SAN_PRESENT;
	} else {
		int i = 0;
		int sANCount = sqo_sk_GENERAL_NAME_num(sANs);
		for (i = 0; i < sANCount && matchFound != MATCH_FOUND; ++i) {
			const GENERAL_NAME* sAN = sqo_sk_GENERAL_NAME_value(sANs, i);
			if ((sAN->type == matchType) &&
			    sqVerifySAN(ssl, sAN, serverName, serverNameLength, matchType)) {
				matchFound = MATCH_FOUND;
				break;
			}
		}
		sqo_sk_GENERAL_NAME_pop_free(sANs, (sk_GENERAL_NAME_freefunc)sqo_sk_free);
	}
	return matchFound;
}

char* sqVerifyFindStar(char* sANData, size_t sANDataSize) {
	ptrdiff_t starPosition = 0;
	char* safeptr = NULL;
	char* label = NULL;
	int starFound = 0;
	size_t labelCount = 0;
	char ptr[MAX_HOSTNAME_LENGTH + 1] = {0};
	memcpy(ptr, sANData, MIN(MAX_HOSTNAME_LENGTH + 1, sANDataSize));

#define FAIL_STAR(x) do { if (x) { return NULL; } } while (0)

	for (label = strtok_r(ptr, ".", &safeptr);
	     label != NULL;
	     label = strtok_r(NULL, ".", &safeptr), labelCount++) {
		char* currentStar = strchr(label, '*'); // \0-termination is guaranteed by strtok_r
		size_t labelLength = strlen(label);  // \0-termination is guaranteed by strtok_r
		if (currentStar != NULL) {
			// only one star per label
			FAIL_STAR(labelLength > 1 && (NULL != strchr(currentStar + 1, '*')));
			// only one star per pattern
			FAIL_STAR(starFound);
			// cannot match partial idna
			FAIL_STAR(0 == strncasecmp(label, "xn--", MIN(labelLength, 4)));
			// star not permissible in non-leftmost label
			FAIL_STAR(labelCount >= 1);

			// first label, star is ok.
			starFound = 1;
			starPosition = currentStar - ptr;
		}
	}
	// no star found, nothing to report
	FAIL_STAR(!starFound);
	// star in last two labels
	FAIL_STAR(labelCount < 3);
	return sANData + starPosition;
#undef FAIL_STAR
}

sqInt sqVerifySAN(sqSSL* ssl, const GENERAL_NAME* sAN, const void* data, const size_t dataSizeIn, const int matchType) {
	char* sANData = (char *) (sqo_ASN1_STRING_get0_data
			? sqo_ASN1_STRING_get0_data(sAN->d.ia5)
			: sqo_ASN1_STRING_data(sAN->d.ia5));
	size_t sANDataSize = (size_t) sqo_ASN1_STRING_length(sAN->d.ia5);
	size_t dataSize = dataSizeIn;

	if (ssl->loglevel) printf("sqVerifyNameInner: checking sAN %.*s\n", matchType == GEN_DNS ? (int) sANDataSize : 5 , matchType == GEN_DNS ? sANData : "an IP");
	// For IPs, exact match only.
	if (matchType == GEN_IPADD) {
		return (sANDataSize == dataSize) && !memcmp(sANData, data, sANDataSize);
	}

	// Normalize dns names by dropping traling dots if any
	if (sANData[sANDataSize - 1] == '.') { sANDataSize--; }
	if (((char*)data)[dataSize - 1] == '.') { dataSize--; }

#define NOPE(x) do { if ((x)) return 0; } while (0)
#define YEAH(x) do { if ((x)) return 1; } while (0)

	// Exact match always wins
	YEAH((sANDataSize == dataSize) && (0 == strncasecmp(sANData, data, sANDataSize)));
	// wildcard matching not for IPs et al.
	NOPE(matchType != GEN_DNS);

	// Malformed DNS name
	NOPE(sANDataSize != strnlen(sANData, sANDataSize));

	{
		char* serverName = (char*) data;
		size_t serverNameSize = dataSize;
		char* starPosition = NULL;
		char* sANDataSuffix = NULL;
		char* serverNameSuffix = NULL;
		ptrdiff_t prefixLength = 0;
		ptrdiff_t suffixLength = 0;
		ptrdiff_t matchLength = 0;

		// Contrary to general certificate machting, we are only
		// interested in setting up an SSL connection, so we do _NOT_
		// allow data (aka serverNames) that start with a '.'
		NOPE(serverName[0] == '.');

		starPosition = sqVerifyFindStar(sANData, sANDataSize);
		// Since exact matches are already covered and we excluded
		// leading '.' in the server name, we bail if no _valid_ star
		// found in the sAN data here.
		NOPE(starPosition == NULL);

		prefixLength = starPosition - sANData;
		suffixLength = (sANData + sANDataSize - 1) - starPosition;
		matchLength = serverNameSize - (suffixLength + prefixLength);
		sANDataSuffix = starPosition + 1;
		serverNameSuffix = serverName + serverNameSize - suffixLength;

		// check that prefix matches.
		NOPE(0 != strncasecmp(sANData, serverName, (size_t) prefixLength));
		// check that suffix matches
		NOPE(0 != strncasecmp(sANDataSuffix, serverNameSuffix, (size_t) suffixLength));
		// complete star labels (*.example.com) must match at least one character
		NOPE(prefixLength == 0 && sANDataSuffix[0] == '.' && matchLength < 1);
		// no more than one serverName label can match the star -> cannot contain periods
		NOPE(matchLength > 0 && (NULL != memchr(serverName + prefixLength, '.', matchLength )));
	}
	return 1;
#undef NOPE
#undef YEAH
}

/* sqSetupSSL: Common SSL setup tasks */
sqInt sqSetupSSL(sqSSL *ssl, int server) {
	/* Fixme. Needs to use specified version */
	if(ssl->loglevel) printf("sqSetupSSL: setting method\n");
        if (sqo_TLS_method) {
            ssl->method = (SSL_METHOD*) sqo_TLS_method();
        } else {
            ssl->method = (SSL_METHOD*) sqo_SSLv23_method();
        }
	if(ssl->loglevel) printf("sqSetupSSL: Creating context\n");
	ssl->ctx = sqo_SSL_CTX_new(ssl->method);
	if(ssl->loglevel) printf("sqSetupSSL: Disabling SSLv2 and SSLv3\n");
	sqo_SSL_CTX_set_options(ssl->ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

	if(!ssl->ctx) sqo_ERR_print_errors_fp(stdout);

	if(ssl->loglevel) printf("sqSetupSSL: setting cipher list\n");
	sqo_SSL_CTX_set_cipher_list(ssl->ctx, "!ADH:HIGH:MEDIUM:@STRENGTH");

	/* if a cert is provided, use it */
	if(ssl->certName) {
		if(ssl->loglevel) { 
                	printf("sqSetupSSL: Using cert file %s\n", ssl->certName);
		}
		if(sqo_SSL_CTX_use_certificate_file(ssl->ctx, ssl->certName, SSL_FILETYPE_PEM)<=0) {
			sqo_ERR_print_errors_fp(stderr);
		}
		if(sqo_SSL_CTX_use_PrivateKey_file(ssl->ctx, ssl->certName, SSL_FILETYPE_PEM)<=0) {
			sqo_ERR_print_errors_fp(stderr);
		}
	}

	/* Set up trusted CA */
	if(ssl->loglevel) printf("sqSetupSSL: No root CA given; using default verify paths\n");
	if(sqo_SSL_CTX_set_default_verify_paths(ssl->ctx) <=0)
		sqo_ERR_print_errors_fp(stderr);

	if(ssl->loglevel) printf("sqSetupSSL: Creating SSL\n");
	ssl->ssl = sqo_SSL_new(ssl->ctx);
	if(ssl->loglevel) printf("sqSetupSSL: setting bios\n");
	sqo_SSL_set_bio(ssl->ssl, ssl->bioRead, ssl->bioWrite);
	return 1;
}
/********************************************************************/
/********************************************************************/
/********************************************************************/

/* sqCreateSSL: Creates a new SSL instance.
	Arguments: None.
	Returns: SSL handle.
*/
sqInt sqCreateSSL(void) {
	sqInt handle = 0;
	sqSSL *ssl = NULL;

	if (!wasInitialized) {
 		if (!loadLibrary()) {
			return 0;
		}
		wasInitialized = true;
	}

	ssl = calloc(1, sizeof(sqSSL));
	ssl->bioRead = sqo_BIO_new(sqo_BIO_s_mem());
	ssl->bioWrite = sqo_BIO_new(sqo_BIO_s_mem());
	sqo_BIO_set_close(ssl->bioRead, BIO_CLOSE);
	sqo_BIO_set_close(ssl->bioWrite, BIO_CLOSE);

	/* Find a free handle */
	for(handle = 1; handle < handleMax; handle++)
		if(handleBuf[handle] == NULL) break;

	if(handle >= handleMax) {
		int i, delta = 100;
		/* Resize the handle buffer */
		handleBuf = realloc(handleBuf, (handleMax+delta)*sizeof(void*));
		for(i = handleMax; i < handleMax+delta; i++)
			handleBuf[i] = NULL;
		handleMax += delta;
	}
	handleBuf[handle] = ssl;
	return handle;
}

/* sqDestroySSL: Destroys an SSL instance.
	Arguments:
		handle - the SSL handle
	Returns: Non-zero if successful.
*/
sqInt sqDestroySSL(sqInt handle) {
	sqSSL *ssl = sslFromHandle(handle);
	if(ssl == NULL) return 0;

	if(ssl->ctx) sqo_SSL_CTX_free(ssl->ctx);

	if(ssl->ssl) {
		sqo_SSL_free(ssl->ssl); // This will also free bioRead and bioWrite
	} else {
		// SSL_new didn't get called, have to free bioRead and bioWrite manually
		sqo_BIO_free_all(ssl->bioRead);
		sqo_BIO_free_all(ssl->bioWrite);
	}

	if(ssl->certName) free(ssl->certName);
	if(ssl->peerName) free(ssl->peerName);
	if(ssl->serverName) free(ssl->serverName);

	free(ssl);
	handleBuf[handle] = NULL;
	return 1;
}

/* sqConnectSSL: Start/continue an SSL client handshake.
	Arguments:
		handle - the SSL handle
		srcBuf - the input token sent by the remote peer
		srcLen - the size of the input token
		dstBuf - the output buffer for a new token
		dstLen - the size of the output buffer
	Returns: The size of the output token or an error code.
*/
sqInt sqConnectSSL(sqInt handle, char* srcBuf, sqInt srcLen, char *dstBuf, sqInt dstLen) {
	int result;
	char peerName[MAX_HOSTNAME_LENGTH + 1];
	X509 *cert;
	sqSSL *ssl = sslFromHandle(handle);

	if(ssl->loglevel) printf("sqConnectSSL: %p\n", ssl);

	/* Verify state of session */
	if(ssl == NULL || (ssl->state != SQSSL_UNUSED && ssl->state != SQSSL_CONNECTING)) {
		return SQSSL_INVALID_STATE;
	}

	/* Establish initial connection */
	if(ssl->state == SQSSL_UNUSED) {
		ssl->state = SQSSL_CONNECTING;
		if(ssl->loglevel) printf("sqConnectSSL: Setting up SSL\n");
		if(!sqSetupSSL(ssl, 0)) return SQSSL_GENERIC_ERROR;
		if(ssl->loglevel) printf("sqConnectSSL: Setting connect state\n");
		sqo_SSL_set_connect_state(ssl->ssl);
	}

	if(ssl->loglevel) printf("sqConnectSSL: BIO_write %ld bytes\n", (long)srcLen);


	if(srcLen > 0) {
		int n = sqo_BIO_write(ssl->bioRead, srcBuf, srcLen);

		if(n < srcLen) {
			if(ssl->loglevel) printf("sqConnectSSL: BIO too small for input\n");
			return SQSSL_GENERIC_ERROR;
		}
		if(n < 0) {
			if(ssl->loglevel) printf("sqConnectSSL: BIO_write failed\n");
				return SQSSL_GENERIC_ERROR;
		}
	}

	/* if a server name is provided, use it */
	if(ssl->serverName) {
		if(ssl->loglevel) printf("sqSetupSSL: Using server name %s\n", ssl->serverName);
		sqo_SSL_set_tlsext_host_name(ssl->ssl, ssl->serverName);
	}

	if(ssl->loglevel) printf("sqConnectSSL: SSL_connect\n");
	result = sqo_SSL_connect(ssl->ssl);
	if(result <= 0) {
		int error = sqo_SSL_get_error(ssl->ssl, result);
		if(error != SSL_ERROR_WANT_READ) {
			if(ssl->loglevel) printf("sqConnectSSL: SSL_connect failed\n");
			sqo_ERR_print_errors_fp(stdout);
			return SQSSL_GENERIC_ERROR;
		}
		if(ssl->loglevel) printf("sqConnectSSL: sqCopyBioSSL\n");
		return sqCopyBioSSL(ssl, ssl->bioWrite, dstBuf, dstLen);
	}

	/* We are connected. Verify the cert. */
	ssl->state = SQSSL_CONNECTED;

	if(ssl->loglevel) printf("sqConnectSSL: SSL_get_peer_certificate\n");
	cert = sqGetPeerCertificate(ssl->ssl);
	if(ssl->loglevel) printf("sqConnectSSL: cert = %p\n", cert);
	/* Fail if no cert received. */
	if(cert) {
		/*
		 * Verify that peer is the one we expect (by name, via cert)
		 *
		 * Note, this goes beyond checking the commonName:
		 * 1. If cert has sAN of dNSName type it MUST be used (even if
		 *	cn is present)
		 * 2. Cert w/o sAN (and cn only)  is DEPRECATED
		 * 3. Cert has multiple sAN/dNSName, anyone SHALL match
		 * 4. Wildcard match have several restrictions (cf RFCs 6125,
		 *	2181, 2595, or Microsoft doc 258858)
		 * 5. IP Addresses MUST be of type iPAddress, NOT dNSName
		 *
		 * Due to 4., name checking is not as simple as
		 *	"certificateName match: * peerName" on the image side.
		 *
		 * To allow image side reaction to server-name matches we
		 *	**copy the `serverName` into the `peerName` property.
		 *	Thus, the image side can check as
		 *
				peerNameMatches
					^ self peerName = self serverName
		 *
		 * or likewise.
		 */
		enum sqMatchResult matched = NO_MATCH_DONE_YET;
		if (ssl->peerName) { free(ssl->peerName); }
		ssl->peerName = NULL;

		if (ssl->serverName) {
			const size_t serverNameLength = strnlen(ssl->serverName, MAX_HOSTNAME_LENGTH);
                        if (sqo_X509_check_ip_asc && sqo_X509_check_host) {
				if(ssl->loglevel) printf("sqConnectSSL: X509_check_host.");
				/* Try IP first, expect INVALID_IP_STRING to continue with hostname */
				matched = (enum sqMatchResult) sqo_X509_check_ip_asc(cert, ssl->serverName, 0);
				if (matched == INVALID_IP_STRING) {
					matched = (enum sqMatchResult) sqo_X509_check_host(cert, ssl->serverName, serverNameLength, sqo_X509_CHECK_FLAG_SINGLE_LABEL_SUBDOMAINS, NULL);
				}
			} else {
				matched = sqVerifyIP(ssl, cert, ssl->serverName, serverNameLength);
				if (matched == INVALID_IP_STRING) {
					matched = sqVerifyDNS(ssl, cert, ssl->serverName, serverNameLength);
				}
			}
			if (matched == MATCH_FOUND) {
				if (ssl->loglevel) printf("sqConnectSSL: check hostname OK\n");
				ssl->peerName = strndup(ssl->serverName, serverNameLength);
			} else {
				if (ssl->loglevel) printf("sqConnectSSL: check hostname NOT OK\n");
			}
		}
		// fallback for missing sAN or non-provided serverName
		if (matched == NO_MATCH_DONE_YET || matched == NO_SAN_PRESENT) {
			sqo_X509_NAME_get_text_by_NID(sqo_X509_get_subject_name(cert),
						      NID_commonName, peerName,
						      sizeof(peerName));
			if(ssl->loglevel) printf("sqConnectSSL: peerName = %s\n", peerName);
			ssl->peerName = strndup(peerName, sizeof(peerName) - 1);
		}
		sqo_X509_free(cert);

		/* Check the result of verification */
		result = sqo_SSL_get_verify_result(ssl->ssl);
		if(ssl->loglevel) printf("sqConnectSSL: SSL_get_verify_result = %d\n", result);
		/* FIXME: Figure out the actual failure reason */
		ssl->certFlags = result ? SQSSL_OTHER_ISSUE : SQSSL_OK;
	} else {
		ssl->certFlags = SQSSL_NO_CERTIFICATE;
	}
	return 0;
}

/* sqAcceptSSL: Start/continue an SSL server handshake.
	Arguments:
		handle - the SSL handle
		srcBuf - the input token sent by the remote peer
		srcLen - the size of the input token
		dstBuf - the output buffer for a new token
		dstLen - the size of the output buffer
	Returns: The size of the output token or an error code.
*/
sqInt sqAcceptSSL(sqInt handle, char* srcBuf, sqInt srcLen, char *dstBuf, sqInt dstLen) {
	int result;
	char peerName[MAX_HOSTNAME_LENGTH + 1];
	X509 *cert;
	sqSSL *ssl = sslFromHandle(handle);

	/* Verify state of session */
	if(ssl == NULL || (ssl->state != SQSSL_UNUSED && ssl->state != SQSSL_ACCEPTING)) {
		return SQSSL_INVALID_STATE;
	}

	/* Establish initial connection */
	if(ssl->state == SQSSL_UNUSED) {
		ssl->state = SQSSL_ACCEPTING;
		if(ssl->loglevel) printf("sqAcceptSSL: Setting up SSL\n");
		if(!sqSetupSSL(ssl, 1)) return SQSSL_GENERIC_ERROR;
		if(ssl->loglevel) printf("sqAcceptSSL: setting accept state\n");
		sqo_SSL_set_accept_state(ssl->ssl);
	}

	if(ssl->loglevel) printf("sqAcceptSSL: BIO_write %ld bytes\n", (long)srcLen);

	if(srcLen > 0) {
		int n = sqo_BIO_write(ssl->bioRead, srcBuf, srcLen);

		if(n < srcLen) {
			if(ssl->loglevel) printf("sqAcceptSSL: BIO_write wrote less than expected\n");
			return SQSSL_GENERIC_ERROR;
		}
		if(n < 0) {
			if(ssl->loglevel) printf("sqAcceptSSL: BIO_write failed\n");
			return SQSSL_GENERIC_ERROR;
		}
	}

	if(ssl->loglevel) printf("sqAcceptSSL: SSL_accept\n");
	result = sqo_SSL_accept(ssl->ssl);

	if(result <= 0) {
		int count = 0;
		int error = sqo_SSL_get_error(ssl->ssl, result);
		if(error != SSL_ERROR_WANT_READ) {
			if(ssl->loglevel) printf("sqAcceptSSL: SSL_accept failed\n");
			sqo_ERR_print_errors_fp(stdout);
			return SQSSL_GENERIC_ERROR;
		}
		if(ssl->loglevel) printf("sqAcceptSSL: sqCopyBioSSL\n");
		count = sqCopyBioSSL(ssl, ssl->bioWrite, dstBuf, dstLen);
		return count ? count : SQSSL_NEED_MORE_DATA;
	}

	/* We are connected. Verify the cert. */
	ssl->state = SQSSL_CONNECTED;

	if(ssl->loglevel) printf("sqAcceptSSL: SSL_get_peer_certificate\n");
	cert = sqGetPeerCertificate(ssl->ssl);
	if(ssl->loglevel) printf("sqAcceptSSL: cert = %p\n", cert);

	if(cert) {
		sqo_X509_NAME_get_text_by_NID(sqo_X509_get_subject_name(cert),
					      NID_commonName, peerName,
					      sizeof(peerName));
		if(ssl->loglevel) printf("sqAcceptSSL: peerName = %s\n", peerName);
		ssl->peerName = strndup(peerName, sizeof(peerName) - 1);
		sqo_X509_free(cert);

		/* Check the result of verification */
		result = sqo_SSL_get_verify_result(ssl->ssl);
		if(ssl->loglevel) printf("sqAcceptSSL: SSL_get_verify_result = %d\n", result);
		/* FIXME: Figure out the actual failure reason */
		ssl->certFlags = result ? SQSSL_OTHER_ISSUE : SQSSL_OK;
	} else {
		ssl->certFlags = SQSSL_NO_CERTIFICATE;
	}
	return sqCopyBioSSL(ssl, ssl->bioWrite, dstBuf, dstLen);
}

/* sqEncryptSSL: Encrypt data for SSL transmission.
	Arguments:
		handle - the SSL handle
		srcBuf - the unencrypted input data
		srcLen - the size of the input data
		dstBuf - the output buffer for the encrypted contents
		dstLen - the size of the output buffer
	Returns: The size of the output generated or an error code.
*/
sqInt sqEncryptSSL(sqInt handle, char* srcBuf, sqInt srcLen, char *dstBuf, sqInt dstLen) {
	int nbytes;
	sqSSL *ssl = sslFromHandle(handle);

	if(ssl == NULL || ssl->state != SQSSL_CONNECTED) return SQSSL_INVALID_STATE;

	if(ssl->loglevel) printf("sqEncryptSSL: Encrypting %ld bytes\n", (long)srcLen);

	nbytes = sqo_SSL_write(ssl->ssl, srcBuf, srcLen);
	if(nbytes != srcLen) return SQSSL_GENERIC_ERROR;
	return sqCopyBioSSL(ssl, ssl->bioWrite, dstBuf, dstLen);
}

/* sqDecryptSSL: Decrypt data for SSL transmission.
	Arguments:
		handle - the SSL handle
		srcBuf - the encrypted input data
		srcLen - the size of the input data
		dstBuf - the output buffer for the decrypted contents
		dstLen - the size of the output buffer
	Returns: The size of the output generated or an error code.
*/
sqInt sqDecryptSSL(sqInt handle, char* srcBuf, sqInt srcLen, char *dstBuf, sqInt dstLen) {
	int nbytes;
	sqSSL *ssl = sslFromHandle(handle);

	if(ssl == NULL || ssl->state != SQSSL_CONNECTED) return SQSSL_INVALID_STATE;

	if (srcLen > 0) {
		nbytes = sqo_BIO_write(ssl->bioRead, srcBuf, srcLen);
		if(nbytes != srcLen) {
			if(ssl->loglevel) printf("sqDecryptSSL: Only wrote %ld bytes\n", (long)nbytes);
			return SQSSL_GENERIC_ERROR;
		}
	}
	nbytes = sqo_SSL_read(ssl->ssl, dstBuf, dstLen);
	if(nbytes <= 0) {
		int error = sqo_SSL_get_error(ssl->ssl, nbytes);
		if(
			error != sqo_SSL_ERROR_WANT_READ &&
			error != sqo_SSL_ERROR_ZERO_RETURN &&
			error != sqo_SSL_ERROR_WANT_X509_LOOKUP
		) {
			if(ssl->loglevel) printf("sqDecryptSSL: Got error %d\n", error);
			return SQSSL_GENERIC_ERROR;
		}
		nbytes = 0;
	} else {
		if(ssl->loglevel) printf("sqDecryptSSL: Decrypted %ld bytes\n", (long)nbytes);
	}
	return nbytes;
}

/* sqGetStringPropertySSL: Retrieve a string property from SSL.
	Arguments:
		handle - the ssl handle
		propID - the property id to retrieve
	Returns: The string value of the property.
*/
char* sqGetStringPropertySSL(sqInt handle, int propID) {
	sqSSL *ssl = sslFromHandle(handle);

	if(ssl == NULL) return NULL;
	switch(propID) {
		case SQSSL_PROP_PEERNAME:	return ssl->peerName ? ssl->peerName : emptyString;
		case SQSSL_PROP_CERTNAME:	return ssl->certName;
		case SQSSL_PROP_SERVERNAME:	return ssl->serverName;
		default:
			if(ssl->loglevel) printf("sqGetStringPropertySSL: Unknown property ID %d\n", propID);
			return NULL;
	}
	// unreachable
}

/* sqSetStringPropertySSL: Set a string property in SSL.
	Arguments:
		handle - the ssl handle
		propID - the property id to retrieve
		propName - the property string
		propLen - the length of the property string
	Returns: Non-zero if successful.
*/
sqInt sqSetStringPropertySSL(sqInt handle, int propID, char *propName, sqInt propLen) {
	sqSSL *ssl = sslFromHandle(handle);
	char *property = NULL;

	if(ssl == NULL) return 0;

	if(propLen) {
		property = strndup(propName, propLen);
	};

	if(ssl->loglevel) printf("sqSetStringPropertySSL(%d): %s\n", propID, property ? property : "(null)");

	switch(propID) {
		case SQSSL_PROP_CERTNAME:
			if (ssl->certName) free(ssl->certName);
			ssl->certName = property;
			break;
		case SQSSL_PROP_SERVERNAME:
			if (ssl->serverName) free(ssl->serverName);
			ssl->serverName = property;
			break;
		default:
			if(property) free(property);
			if(ssl->loglevel) printf("sqSetStringPropertySSL: Unknown property ID %d\n", propID);
			return 0;
	}
	return 1;
}

/* sqGetIntPropertySSL: Retrieve an integer property from SSL.
	Arguments:
		handle - the ssl handle
		propID - the property id to retrieve
	Returns: The integer value of the property.
*/
sqInt sqGetIntPropertySSL(sqInt handle, sqInt propID) {
	sqSSL *ssl = sslFromHandle(handle);

	if(ssl == NULL) return 0;
	switch(propID) {
		case SQSSL_PROP_SSLSTATE: return ssl->state;
		case SQSSL_PROP_CERTSTATE: return ssl->certFlags;
		case SQSSL_PROP_VERSION: return SQSSL_VERSION;
		case SQSSL_PROP_LOGLEVEL: return ssl->loglevel;
		default:
			if(ssl->loglevel) printf("sqGetIntPropertySSL: Unknown property ID %ld\n", (long)propID);
			return 0;
	}
	return 0;
}

 /* sqSetIntPropertySSL: Set an integer property in SSL.
	Arguments:
		handle - the ssl handle
		propID - the property id to retrieve
		propValue - the property value
	Returns: Non-zero if successful.
*/
sqInt sqSetIntPropertySSL(sqInt handle, sqInt propID, sqInt propValue) {
	sqSSL *ssl = sslFromHandle(handle);
	if(ssl == NULL) return 0;

	switch(propID) {
		case SQSSL_PROP_LOGLEVEL: ssl->loglevel = propValue; break;
		default:
			if(ssl->loglevel) printf("sqSetIntPropertySSL: Unknown property ID %ld\n", (long)propID);
			return 0;
	}
	return 1;
}
