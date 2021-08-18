#include "hssl.h"

#ifdef WITH_APPLETLS

/* Disclaimer: excerpted from curl */

#include <Security/Security.h>
/* For some reason, when building for iOS, the omnibus header above does
 * not include SecureTransport.h as of iOS SDK 5.1. */
#include <Security/SecureTransport.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CommonCrypto/CommonDigest.h>

#include "hsocket.h"

/* The Security framework has changed greatly between iOS and different macOS
   versions, and we will try to support as many of them as we can (back to
   Leopard and iOS 5) by using macros and weak-linking.
   In general, you want to build this using the most recent OS SDK, since some
   features require curl to be built against the latest SDK. TLS 1.1 and 1.2
   support, for instance, require the macOS 10.8 SDK or later. TLS 1.3
   requires the macOS 10.13 or iOS 11 SDK or later. */
#if (TARGET_OS_MAC && !(TARGET_OS_EMBEDDED || TARGET_OS_IPHONE))

#if MAC_OS_X_VERSION_MAX_ALLOWED < 1050
#error "The Secure Transport back-end requires Leopard or later."
#endif /* MAC_OS_X_VERSION_MAX_ALLOWED < 1050 */

#define CURL_BUILD_IOS 0
#define CURL_BUILD_IOS_7 0
#define CURL_BUILD_IOS_9 0
#define CURL_BUILD_IOS_11 0
#define CURL_BUILD_IOS_13 0
#define CURL_BUILD_MAC 1
/* This is the maximum API level we are allowed to use when building: */
#define CURL_BUILD_MAC_10_5 MAC_OS_X_VERSION_MAX_ALLOWED >= 1050
#define CURL_BUILD_MAC_10_6 MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
#define CURL_BUILD_MAC_10_7 MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
#define CURL_BUILD_MAC_10_8 MAC_OS_X_VERSION_MAX_ALLOWED >= 1080
#define CURL_BUILD_MAC_10_9 MAC_OS_X_VERSION_MAX_ALLOWED >= 1090
#define CURL_BUILD_MAC_10_11 MAC_OS_X_VERSION_MAX_ALLOWED >= 101100
#define CURL_BUILD_MAC_10_13 MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
#define CURL_BUILD_MAC_10_15 MAC_OS_X_VERSION_MAX_ALLOWED >= 101500
/* These macros mean "the following code is present to allow runtime backward
   compatibility with at least this cat or earlier":
   (You set this at build-time using the compiler command line option
   "-mmacosx-version-min.") */
#define CURL_SUPPORT_MAC_10_5 MAC_OS_X_VERSION_MIN_REQUIRED <= 1050
#define CURL_SUPPORT_MAC_10_6 MAC_OS_X_VERSION_MIN_REQUIRED <= 1060
#define CURL_SUPPORT_MAC_10_7 MAC_OS_X_VERSION_MIN_REQUIRED <= 1070
#define CURL_SUPPORT_MAC_10_8 MAC_OS_X_VERSION_MIN_REQUIRED <= 1080
#define CURL_SUPPORT_MAC_10_9 MAC_OS_X_VERSION_MIN_REQUIRED <= 1090

#elif TARGET_OS_EMBEDDED || TARGET_OS_IPHONE
#define CURL_BUILD_IOS 1
#define CURL_BUILD_IOS_7 __IPHONE_OS_VERSION_MAX_ALLOWED >= 70000
#define CURL_BUILD_IOS_9 __IPHONE_OS_VERSION_MAX_ALLOWED >= 90000
#define CURL_BUILD_IOS_11 __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000
#define CURL_BUILD_IOS_13 __IPHONE_OS_VERSION_MAX_ALLOWED >= 130000
#define CURL_BUILD_MAC 0
#define CURL_BUILD_MAC_10_5 0
#define CURL_BUILD_MAC_10_6 0
#define CURL_BUILD_MAC_10_7 0
#define CURL_BUILD_MAC_10_8 0
#define CURL_BUILD_MAC_10_9 0
#define CURL_BUILD_MAC_10_11 0
#define CURL_BUILD_MAC_10_13 0
#define CURL_BUILD_MAC_10_15 0
#define CURL_SUPPORT_MAC_10_5 0
#define CURL_SUPPORT_MAC_10_6 0
#define CURL_SUPPORT_MAC_10_7 0
#define CURL_SUPPORT_MAC_10_8 0
#define CURL_SUPPORT_MAC_10_9 0

#else
#error "The Secure Transport back-end requires iOS or macOS."
#endif /* (TARGET_OS_MAC && !(TARGET_OS_EMBEDDED || TARGET_OS_IPHONE)) */

#if !defined(__MAC_10_8)
static const SSLProtocol kTLSProtocol11 = (SSLProtocol)7;
static const SSLProtocol kTLSProtocol12 = (SSLProtocol)8;
#endif

#if !defined(__MAC_10_13)
static const SSLProtocol kTLSProtocol13 = (SSLProtocol)10;
#endif

static inline const char* SSLProtocolToString(SSLProtocol proto) {
    switch(proto) {
    case kSSLProtocol2:  return "SSLv2";
    case kSSLProtocol3:  return "SSLv3";
    case kTLSProtocol1:  return "TLSv1";
    case kTLSProtocol11: return "TLSv1.1";
    case kTLSProtocol12: return "TLSv1.2";
    case kTLSProtocol13: return "TLSv1.3";
    default:             return "Unknown";
    }
}

struct st_cipher {
  const char *name; /* Cipher suite IANA name. It starts with "TLS_" prefix */
  const char *alias_name; /* Alias name is the same as OpenSSL cipher name */
  SSLCipherSuite num; /* Cipher suite code/number defined in IANA registry */
  bool weak; /* Flag to mark cipher as weak based on previous implementation
                of Secure Transport back-end by CURL */
};

/* Macro to initialize st_cipher data structure: stringify id to name, cipher
   number/id, 'weak' suite flag
 */
#define CIPHER_DEF(num, alias, weak) \
  { #num, alias, num, weak }

/*
 Macro to initialize st_cipher data structure with name, code (IANA cipher
 number/id value), and 'weak' suite flag. The first 28 cipher suite numbers
 have the same IANA code for both SSL and TLS standards: numbers 0x0000 to
 0x001B. They have different names though. The first 4 letters of the cipher
 suite name are the protocol name: "SSL_" or "TLS_", rest of the IANA name is
 the same for both SSL and TLS cipher suite name.
 The second part of the problem is that macOS/iOS SDKs don't define all TLS
 codes but only 12 of them. The SDK defines all SSL codes though, i.e. SSL_NUM
 constant is always defined for those 28 ciphers while TLS_NUM is defined only
 for 12 of the first 28 ciphers. Those 12 TLS cipher codes match to
 corresponding SSL enum value and represent the same cipher suite. Therefore
 we'll use the SSL enum value for those cipher suites because it is defined
 for all 28 of them.
 We make internal data consistent and based on TLS names, i.e. all st_cipher
 item names start with the "TLS_" prefix.
 Summarizing all the above, those 28 first ciphers are presented in our table
 with both TLS and SSL names. Their cipher numbers are assigned based on the
 SDK enum value for the SSL cipher, which matches to IANA TLS number.
 */
#define CIPHER_DEF_SSLTLS(num_wo_prefix, alias, weak) \
  { "TLS_" #num_wo_prefix, alias, SSL_##num_wo_prefix, weak }

/*
 Cipher suites were marked as weak based on the following:
 RC4 encryption - rfc7465, the document contains a list of deprecated ciphers.
     Marked in the code below as weak.
 RC2 encryption - many mentions, was found vulnerable to a relatively easy
     attack https://link.springer.com/chapter/10.1007%2F3-540-69710-1_14
     Marked in the code below as weak.
 DES and IDEA encryption - rfc5469, has a list of deprecated ciphers.
     Marked in the code below as weak.
 Anonymous Diffie-Hellman authentication and anonymous elliptic curve
     Diffie-Hellman - vulnerable to a man-in-the-middle attack. Deprecated by
     RFC 4346 aka TLS 1.1 (section A.5, page 60)
 Null bulk encryption suites - not encrypted communication
 Export ciphers, i.e. ciphers with restrictions to be used outside the US for
     software exported to some countries, they were excluded from TLS 1.1
     version. More precisely, they were noted as ciphers which MUST NOT be
     negotiated in RFC 4346 aka TLS 1.1 (section A.5, pages 60 and 61).
     All of those filters were considered weak because they contain a weak
     algorithm like DES, RC2 or RC4, and already considered weak by other
     criteria.
 3DES - NIST deprecated it and is going to retire it by 2023
 https://csrc.nist.gov/News/2017/Update-to-Current-Use-and-Deprecation-of-TDEA
     OpenSSL https://www.openssl.org/blog/blog/2016/08/24/sweet32/ also
     deprecated those ciphers. Some other libraries also consider it
     vulnerable or at least not strong enough.
 CBC ciphers are vulnerable with SSL3.0 and TLS1.0:
 https://www.cisco.com/c/en/us/support/docs/security/email-security-appliance
 /118518-technote-esa-00.html
     We don't take care of this issue because it is resolved by later TLS
     versions and for us, it requires more complicated checks, we need to
     check a protocol version also. Vulnerability doesn't look very critical
     and we do not filter out those cipher suites.
 */

#define CIPHER_WEAK_NOT_ENCRYPTED   TRUE
#define CIPHER_WEAK_RC_ENCRYPTION   TRUE
#define CIPHER_WEAK_DES_ENCRYPTION  TRUE
#define CIPHER_WEAK_IDEA_ENCRYPTION TRUE
#define CIPHER_WEAK_ANON_AUTH       TRUE
#define CIPHER_WEAK_3DES_ENCRYPTION TRUE
#define CIPHER_STRONG_ENOUGH        FALSE

/* Please do not change the order of the first ciphers available for SSL.
   Do not insert and do not delete any of them. Code below
   depends on their order and continuity.
   If you add a new cipher, please maintain order by number, i.e.
   insert in between existing items to appropriate place based on
   cipher suite IANA number
*/
const static struct st_cipher ciphertable[] = {
  /* SSL version 3.0 and initial TLS 1.0 cipher suites.
     Defined since SDK 10.2.8 */
  CIPHER_DEF_SSLTLS(NULL_WITH_NULL_NULL,                           /* 0x0000 */
                    NULL,
                    CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF_SSLTLS(RSA_WITH_NULL_MD5,                             /* 0x0001 */
                    "NULL-MD5",
                    CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF_SSLTLS(RSA_WITH_NULL_SHA,                             /* 0x0002 */
                    "NULL-SHA",
                    CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF_SSLTLS(RSA_EXPORT_WITH_RC4_40_MD5,                    /* 0x0003 */
                    "EXP-RC4-MD5",
                    CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF_SSLTLS(RSA_WITH_RC4_128_MD5,                          /* 0x0004 */
                    "RC4-MD5",
                    CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF_SSLTLS(RSA_WITH_RC4_128_SHA,                          /* 0x0005 */
                    "RC4-SHA",
                    CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF_SSLTLS(RSA_EXPORT_WITH_RC2_CBC_40_MD5,                /* 0x0006 */
                    "EXP-RC2-CBC-MD5",
                    CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF_SSLTLS(RSA_WITH_IDEA_CBC_SHA,                         /* 0x0007 */
                    "IDEA-CBC-SHA",
                    CIPHER_WEAK_IDEA_ENCRYPTION),
  CIPHER_DEF_SSLTLS(RSA_EXPORT_WITH_DES40_CBC_SHA,                 /* 0x0008 */
                    "EXP-DES-CBC-SHA",
                    CIPHER_WEAK_DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(RSA_WITH_DES_CBC_SHA,                          /* 0x0009 */
                    "DES-CBC-SHA",
                    CIPHER_WEAK_DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(RSA_WITH_3DES_EDE_CBC_SHA,                     /* 0x000A */
                    "DES-CBC3-SHA",
                    CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DH_DSS_EXPORT_WITH_DES40_CBC_SHA,              /* 0x000B */
                    "EXP-DH-DSS-DES-CBC-SHA",
                    CIPHER_WEAK_DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DH_DSS_WITH_DES_CBC_SHA,                       /* 0x000C */
                    "DH-DSS-DES-CBC-SHA",
                    CIPHER_WEAK_DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DH_DSS_WITH_3DES_EDE_CBC_SHA,                  /* 0x000D */
                    "DH-DSS-DES-CBC3-SHA",
                    CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DH_RSA_EXPORT_WITH_DES40_CBC_SHA,              /* 0x000E */
                    "EXP-DH-RSA-DES-CBC-SHA",
                    CIPHER_WEAK_DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DH_RSA_WITH_DES_CBC_SHA,                       /* 0x000F */
                    "DH-RSA-DES-CBC-SHA",
                    CIPHER_WEAK_DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DH_RSA_WITH_3DES_EDE_CBC_SHA,                  /* 0x0010 */
                    "DH-RSA-DES-CBC3-SHA",
                    CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DHE_DSS_EXPORT_WITH_DES40_CBC_SHA,             /* 0x0011 */
                    "EXP-EDH-DSS-DES-CBC-SHA",
                    CIPHER_WEAK_DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DHE_DSS_WITH_DES_CBC_SHA,                      /* 0x0012 */
                    "EDH-DSS-CBC-SHA",
                    CIPHER_WEAK_DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DHE_DSS_WITH_3DES_EDE_CBC_SHA,                 /* 0x0013 */
                    "DHE-DSS-DES-CBC3-SHA",
                    CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DHE_RSA_EXPORT_WITH_DES40_CBC_SHA,             /* 0x0014 */
                    "EXP-EDH-RSA-DES-CBC-SHA",
                    CIPHER_WEAK_DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DHE_RSA_WITH_DES_CBC_SHA,                      /* 0x0015 */
                    "EDH-RSA-DES-CBC-SHA",
                    CIPHER_WEAK_DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DHE_RSA_WITH_3DES_EDE_CBC_SHA,                 /* 0x0016 */
                    "DHE-RSA-DES-CBC3-SHA",
                    CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF_SSLTLS(DH_anon_EXPORT_WITH_RC4_40_MD5,                /* 0x0017 */
                    "EXP-ADH-RC4-MD5",
                    CIPHER_WEAK_ANON_AUTH),
  CIPHER_DEF_SSLTLS(DH_anon_WITH_RC4_128_MD5,                      /* 0x0018 */
                    "ADH-RC4-MD5",
                    CIPHER_WEAK_ANON_AUTH),
  CIPHER_DEF_SSLTLS(DH_anon_EXPORT_WITH_DES40_CBC_SHA,             /* 0x0019 */
                    "EXP-ADH-DES-CBC-SHA",
                    CIPHER_WEAK_ANON_AUTH),
  CIPHER_DEF_SSLTLS(DH_anon_WITH_DES_CBC_SHA,                      /* 0x001A */
                    "ADH-DES-CBC-SHA",
                    CIPHER_WEAK_ANON_AUTH),
  CIPHER_DEF_SSLTLS(DH_anon_WITH_3DES_EDE_CBC_SHA,                 /* 0x001B */
                    "ADH-DES-CBC3-SHA",
                    CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF(SSL_FORTEZZA_DMS_WITH_NULL_SHA,                       /* 0x001C */
             NULL,
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(SSL_FORTEZZA_DMS_WITH_FORTEZZA_CBC_SHA,               /* 0x001D */
             NULL,
             CIPHER_STRONG_ENOUGH),

#if CURL_BUILD_MAC_10_9 || CURL_BUILD_IOS_7
  /* RFC 4785 - Pre-Shared Key (PSK) Ciphersuites with NULL Encryption */
  CIPHER_DEF(TLS_PSK_WITH_NULL_SHA,                                /* 0x002C */
             "PSK-NULL-SHA",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_DHE_PSK_WITH_NULL_SHA,                            /* 0x002D */
             "DHE-PSK-NULL-SHA",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_RSA_PSK_WITH_NULL_SHA,                            /* 0x002E */
             "RSA-PSK-NULL-SHA",
             CIPHER_WEAK_NOT_ENCRYPTED),
#endif /* CURL_BUILD_MAC_10_9 || CURL_BUILD_IOS_7 */

  /* TLS addenda using AES, per RFC 3268. Defined since SDK 10.4u */
  CIPHER_DEF(TLS_RSA_WITH_AES_128_CBC_SHA,                         /* 0x002F */
             "AES128-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_DSS_WITH_AES_128_CBC_SHA,                      /* 0x0030 */
             "DH-DSS-AES128-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_RSA_WITH_AES_128_CBC_SHA,                      /* 0x0031 */
             "DH-RSA-AES128-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_DSS_WITH_AES_128_CBC_SHA,                     /* 0x0032 */
             "DHE-DSS-AES128-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_RSA_WITH_AES_128_CBC_SHA,                     /* 0x0033 */
             "DHE-RSA-AES128-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_anon_WITH_AES_128_CBC_SHA,                     /* 0x0034 */
             "ADH-AES128-SHA",
             CIPHER_WEAK_ANON_AUTH),
  CIPHER_DEF(TLS_RSA_WITH_AES_256_CBC_SHA,                         /* 0x0035 */
             "AES256-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_DSS_WITH_AES_256_CBC_SHA,                      /* 0x0036 */
             "DH-DSS-AES256-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_RSA_WITH_AES_256_CBC_SHA,                      /* 0x0037 */
             "DH-RSA-AES256-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_DSS_WITH_AES_256_CBC_SHA,                     /* 0x0038 */
             "DHE-DSS-AES256-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_RSA_WITH_AES_256_CBC_SHA,                     /* 0x0039 */
             "DHE-RSA-AES256-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_anon_WITH_AES_256_CBC_SHA,                     /* 0x003A */
             "ADH-AES256-SHA",
             CIPHER_WEAK_ANON_AUTH),

#if CURL_BUILD_MAC_10_8 || CURL_BUILD_IOS
  /* TLS 1.2 addenda, RFC 5246 */
  /* Server provided RSA certificate for key exchange. */
  CIPHER_DEF(TLS_RSA_WITH_NULL_SHA256,                             /* 0x003B */
             "NULL-SHA256",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_RSA_WITH_AES_128_CBC_SHA256,                      /* 0x003C */
             "AES128-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_RSA_WITH_AES_256_CBC_SHA256,                      /* 0x003D */
             "AES256-SHA256",
             CIPHER_STRONG_ENOUGH),
  /* Server-authenticated (and optionally client-authenticated)
     Diffie-Hellman. */
  CIPHER_DEF(TLS_DH_DSS_WITH_AES_128_CBC_SHA256,                   /* 0x003E */
             "DH-DSS-AES128-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_RSA_WITH_AES_128_CBC_SHA256,                   /* 0x003F */
             "DH-RSA-AES128-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_DSS_WITH_AES_128_CBC_SHA256,                  /* 0x0040 */
             "DHE-DSS-AES128-SHA256",
             CIPHER_STRONG_ENOUGH),

  /* TLS 1.2 addenda, RFC 5246 */
  CIPHER_DEF(TLS_DHE_RSA_WITH_AES_128_CBC_SHA256,                  /* 0x0067 */
             "DHE-RSA-AES128-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_DSS_WITH_AES_256_CBC_SHA256,                   /* 0x0068 */
             "DH-DSS-AES256-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_RSA_WITH_AES_256_CBC_SHA256,                   /* 0x0069 */
             "DH-RSA-AES256-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_DSS_WITH_AES_256_CBC_SHA256,                  /* 0x006A */
             "DHE-DSS-AES256-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_RSA_WITH_AES_256_CBC_SHA256,                  /* 0x006B */
             "DHE-RSA-AES256-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_anon_WITH_AES_128_CBC_SHA256,                  /* 0x006C */
             "ADH-AES128-SHA256",
             CIPHER_WEAK_ANON_AUTH),
  CIPHER_DEF(TLS_DH_anon_WITH_AES_256_CBC_SHA256,                  /* 0x006D */
             "ADH-AES256-SHA256",
             CIPHER_WEAK_ANON_AUTH),
#endif /* CURL_BUILD_MAC_10_8 || CURL_BUILD_IOS */

#if CURL_BUILD_MAC_10_9 || CURL_BUILD_IOS_7
  /* Addendum from RFC 4279, TLS PSK */
  CIPHER_DEF(TLS_PSK_WITH_RC4_128_SHA,                             /* 0x008A */
             "PSK-RC4-SHA",
             CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF(TLS_PSK_WITH_3DES_EDE_CBC_SHA,                        /* 0x008B */
             "PSK-3DES-EDE-CBC-SHA",
             CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF(TLS_PSK_WITH_AES_128_CBC_SHA,                         /* 0x008C */
             "PSK-AES128-CBC-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_PSK_WITH_AES_256_CBC_SHA,                         /* 0x008D */
             "PSK-AES256-CBC-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_PSK_WITH_RC4_128_SHA,                         /* 0x008E */
             "DHE-PSK-RC4-SHA",
             CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF(TLS_DHE_PSK_WITH_3DES_EDE_CBC_SHA,                    /* 0x008F */
             "DHE-PSK-3DES-EDE-CBC-SHA",
             CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF(TLS_DHE_PSK_WITH_AES_128_CBC_SHA,                     /* 0x0090 */
             "DHE-PSK-AES128-CBC-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_PSK_WITH_AES_256_CBC_SHA,                     /* 0x0091 */
             "DHE-PSK-AES256-CBC-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_RSA_PSK_WITH_RC4_128_SHA,                         /* 0x0092 */
             "RSA-PSK-RC4-SHA",
             CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF(TLS_RSA_PSK_WITH_3DES_EDE_CBC_SHA,                    /* 0x0093 */
             "RSA-PSK-3DES-EDE-CBC-SHA",
             CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF(TLS_RSA_PSK_WITH_AES_128_CBC_SHA,                     /* 0x0094 */
             "RSA-PSK-AES128-CBC-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_RSA_PSK_WITH_AES_256_CBC_SHA,                     /* 0x0095 */
             "RSA-PSK-AES256-CBC-SHA",
             CIPHER_STRONG_ENOUGH),
#endif /* CURL_BUILD_MAC_10_9 || CURL_BUILD_IOS_7 */

#if CURL_BUILD_MAC_10_8 || CURL_BUILD_IOS
  /* Addenda from rfc 5288 AES Galois Counter Mode (GCM) Cipher Suites
     for TLS. */
  CIPHER_DEF(TLS_RSA_WITH_AES_128_GCM_SHA256,                      /* 0x009C */
             "AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_RSA_WITH_AES_256_GCM_SHA384,                      /* 0x009D */
             "AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_RSA_WITH_AES_128_GCM_SHA256,                  /* 0x009E */
             "DHE-RSA-AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_RSA_WITH_AES_256_GCM_SHA384,                  /* 0x009F */
             "DHE-RSA-AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_RSA_WITH_AES_128_GCM_SHA256,                   /* 0x00A0 */
             "DH-RSA-AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_RSA_WITH_AES_256_GCM_SHA384,                   /* 0x00A1 */
             "DH-RSA-AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_DSS_WITH_AES_128_GCM_SHA256,                  /* 0x00A2 */
             "DHE-DSS-AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_DSS_WITH_AES_256_GCM_SHA384,                  /* 0x00A3 */
             "DHE-DSS-AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_DSS_WITH_AES_128_GCM_SHA256,                   /* 0x00A4 */
             "DH-DSS-AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_DSS_WITH_AES_256_GCM_SHA384,                   /* 0x00A5 */
             "DH-DSS-AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DH_anon_WITH_AES_128_GCM_SHA256,                  /* 0x00A6 */
             "ADH-AES128-GCM-SHA256",
             CIPHER_WEAK_ANON_AUTH),
  CIPHER_DEF(TLS_DH_anon_WITH_AES_256_GCM_SHA384,                  /* 0x00A7 */
             "ADH-AES256-GCM-SHA384",
             CIPHER_WEAK_ANON_AUTH),
#endif /* CURL_BUILD_MAC_10_8 || CURL_BUILD_IOS */

#if CURL_BUILD_MAC_10_9 || CURL_BUILD_IOS_7
  /* RFC 5487 - PSK with SHA-256/384 and AES GCM */
  CIPHER_DEF(TLS_PSK_WITH_AES_128_GCM_SHA256,                      /* 0x00A8 */
             "PSK-AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_PSK_WITH_AES_256_GCM_SHA384,                      /* 0x00A9 */
             "PSK-AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_PSK_WITH_AES_128_GCM_SHA256,                  /* 0x00AA */
             "DHE-PSK-AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_PSK_WITH_AES_256_GCM_SHA384,                  /* 0x00AB */
             "DHE-PSK-AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_RSA_PSK_WITH_AES_128_GCM_SHA256,                  /* 0x00AC */
             "RSA-PSK-AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_RSA_PSK_WITH_AES_256_GCM_SHA384,                  /* 0x00AD */
             "RSA-PSK-AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_PSK_WITH_AES_128_CBC_SHA256,                      /* 0x00AE */
             "PSK-AES128-CBC-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_PSK_WITH_AES_256_CBC_SHA384,                      /* 0x00AF */
             "PSK-AES256-CBC-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_PSK_WITH_NULL_SHA256,                             /* 0x00B0 */
             "PSK-NULL-SHA256",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_PSK_WITH_NULL_SHA384,                             /* 0x00B1 */
             "PSK-NULL-SHA384",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_DHE_PSK_WITH_AES_128_CBC_SHA256,                  /* 0x00B2 */
             "DHE-PSK-AES128-CBC-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_PSK_WITH_AES_256_CBC_SHA384,                  /* 0x00B3 */
             "DHE-PSK-AES256-CBC-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_DHE_PSK_WITH_NULL_SHA256,                         /* 0x00B4 */
             "DHE-PSK-NULL-SHA256",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_DHE_PSK_WITH_NULL_SHA384,                         /* 0x00B5 */
             "DHE-PSK-NULL-SHA384",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_RSA_PSK_WITH_AES_128_CBC_SHA256,                  /* 0x00B6 */
             "RSA-PSK-AES128-CBC-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_RSA_PSK_WITH_AES_256_CBC_SHA384,                  /* 0x00B7 */
             "RSA-PSK-AES256-CBC-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_RSA_PSK_WITH_NULL_SHA256,                         /* 0x00B8 */
             "RSA-PSK-NULL-SHA256",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_RSA_PSK_WITH_NULL_SHA384,                         /* 0x00B9 */
             "RSA-PSK-NULL-SHA384",
             CIPHER_WEAK_NOT_ENCRYPTED),
#endif /* CURL_BUILD_MAC_10_9 || CURL_BUILD_IOS_7 */

  /* RFC 5746 - Secure Renegotiation. This is not a real suite,
     it is a response to initiate negotiation again */
  CIPHER_DEF(TLS_EMPTY_RENEGOTIATION_INFO_SCSV,                    /* 0x00FF */
             NULL,
             CIPHER_STRONG_ENOUGH),

#if CURL_BUILD_MAC_10_13 || CURL_BUILD_IOS_11
  /* TLS 1.3 standard cipher suites for ChaCha20+Poly1305.
     Note: TLS 1.3 ciphersuites do not specify the key exchange
     algorithm -- they only specify the symmetric ciphers.
     Cipher alias name matches to OpenSSL cipher name, and for
     TLS 1.3 ciphers */
  CIPHER_DEF(TLS_AES_128_GCM_SHA256,                               /* 0x1301 */
             NULL,  /* The OpenSSL cipher name matches to the IANA name */
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_AES_256_GCM_SHA384,                               /* 0x1302 */
             NULL,  /* The OpenSSL cipher name matches to the IANA name */
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_CHACHA20_POLY1305_SHA256,                         /* 0x1303 */
             NULL,  /* The OpenSSL cipher name matches to the IANA name */
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_AES_128_CCM_SHA256,                               /* 0x1304 */
             NULL,  /* The OpenSSL cipher name matches to the IANA name */
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_AES_128_CCM_8_SHA256,                             /* 0x1305 */
             NULL,  /* The OpenSSL cipher name matches to the IANA name */
             CIPHER_STRONG_ENOUGH),
#endif /* CURL_BUILD_MAC_10_13 || CURL_BUILD_IOS_11 */

#if CURL_BUILD_MAC_10_6 || CURL_BUILD_IOS
  /* ECDSA addenda, RFC 4492 */
  CIPHER_DEF(TLS_ECDH_ECDSA_WITH_NULL_SHA,                         /* 0xC001 */
             "ECDH-ECDSA-NULL-SHA",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_ECDH_ECDSA_WITH_RC4_128_SHA,                      /* 0xC002 */
             "ECDH-ECDSA-RC4-SHA",
             CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF(TLS_ECDH_ECDSA_WITH_3DES_EDE_CBC_SHA,                 /* 0xC003 */
             "ECDH-ECDSA-DES-CBC3-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA,                  /* 0xC004 */
             "ECDH-ECDSA-AES128-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA,                  /* 0xC005 */
             "ECDH-ECDSA-AES256-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_ECDSA_WITH_NULL_SHA,                        /* 0xC006 */
             "ECDHE-ECDSA-NULL-SHA",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_ECDHE_ECDSA_WITH_RC4_128_SHA,                     /* 0xC007 */
             "ECDHE-ECDSA-RC4-SHA",
             CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF(TLS_ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA,                /* 0xC008 */
             "ECDHE-ECDSA-DES-CBC3-SHA",
             CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF(TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,                 /* 0xC009 */
             "ECDHE-ECDSA-AES128-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,                 /* 0xC00A */
             "ECDHE-ECDSA-AES256-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_RSA_WITH_NULL_SHA,                           /* 0xC00B */
             "ECDH-RSA-NULL-SHA",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_ECDH_RSA_WITH_RC4_128_SHA,                        /* 0xC00C */
             "ECDH-RSA-RC4-SHA",
             CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF(TLS_ECDH_RSA_WITH_3DES_EDE_CBC_SHA,                   /* 0xC00D */
             "ECDH-RSA-DES-CBC3-SHA",
             CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF(TLS_ECDH_RSA_WITH_AES_128_CBC_SHA,                    /* 0xC00E */
             "ECDH-RSA-AES128-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_RSA_WITH_AES_256_CBC_SHA,                    /* 0xC00F */
             "ECDH-RSA-AES256-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_RSA_WITH_NULL_SHA,                          /* 0xC010 */
             "ECDHE-RSA-NULL-SHA",
             CIPHER_WEAK_NOT_ENCRYPTED),
  CIPHER_DEF(TLS_ECDHE_RSA_WITH_RC4_128_SHA,                       /* 0xC011 */
             "ECDHE-RSA-RC4-SHA",
             CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF(TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA,                  /* 0xC012 */
             "ECDHE-RSA-DES-CBC3-SHA",
             CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF(TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,                   /* 0xC013 */
             "ECDHE-RSA-AES128-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,                   /* 0xC014 */
             "ECDHE-RSA-AES256-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_anon_WITH_NULL_SHA,                          /* 0xC015 */
             "AECDH-NULL-SHA",
             CIPHER_WEAK_ANON_AUTH),
  CIPHER_DEF(TLS_ECDH_anon_WITH_RC4_128_SHA,                       /* 0xC016 */
             "AECDH-RC4-SHA",
             CIPHER_WEAK_ANON_AUTH),
  CIPHER_DEF(TLS_ECDH_anon_WITH_3DES_EDE_CBC_SHA,                  /* 0xC017 */
             "AECDH-DES-CBC3-SHA",
             CIPHER_WEAK_3DES_ENCRYPTION),
  CIPHER_DEF(TLS_ECDH_anon_WITH_AES_128_CBC_SHA,                   /* 0xC018 */
             "AECDH-AES128-SHA",
             CIPHER_WEAK_ANON_AUTH),
  CIPHER_DEF(TLS_ECDH_anon_WITH_AES_256_CBC_SHA,                   /* 0xC019 */
             "AECDH-AES256-SHA",
             CIPHER_WEAK_ANON_AUTH),
#endif /* CURL_BUILD_MAC_10_6 || CURL_BUILD_IOS */

#if CURL_BUILD_MAC_10_8 || CURL_BUILD_IOS
  /* Addenda from rfc 5289  Elliptic Curve Cipher Suites with
     HMAC SHA-256/384. */
  CIPHER_DEF(TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,              /* 0xC023 */
             "ECDHE-ECDSA-AES128-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384,              /* 0xC024 */
             "ECDHE-ECDSA-AES256-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA256,               /* 0xC025 */
             "ECDH-ECDSA-AES128-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA384,               /* 0xC026 */
             "ECDH-ECDSA-AES256-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,                /* 0xC027 */
             "ECDHE-RSA-AES128-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,                /* 0xC028 */
             "ECDHE-RSA-AES256-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_RSA_WITH_AES_128_CBC_SHA256,                 /* 0xC029 */
             "ECDH-RSA-AES128-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_RSA_WITH_AES_256_CBC_SHA384,                 /* 0xC02A */
             "ECDH-RSA-AES256-SHA384",
             CIPHER_STRONG_ENOUGH),
  /* Addenda from rfc 5289  Elliptic Curve Cipher Suites with
     SHA-256/384 and AES Galois Counter Mode (GCM) */
  CIPHER_DEF(TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,              /* 0xC02B */
             "ECDHE-ECDSA-AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,              /* 0xC02C */
             "ECDHE-ECDSA-AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_ECDSA_WITH_AES_128_GCM_SHA256,               /* 0xC02D */
             "ECDH-ECDSA-AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_ECDSA_WITH_AES_256_GCM_SHA384,               /* 0xC02E */
             "ECDH-ECDSA-AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,                /* 0xC02F */
             "ECDHE-RSA-AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,                /* 0xC030 */
             "ECDHE-RSA-AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_RSA_WITH_AES_128_GCM_SHA256,                 /* 0xC031 */
             "ECDH-RSA-AES128-GCM-SHA256",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDH_RSA_WITH_AES_256_GCM_SHA384,                 /* 0xC032 */
             "ECDH-RSA-AES256-GCM-SHA384",
             CIPHER_STRONG_ENOUGH),
#endif /* CURL_BUILD_MAC_10_8 || CURL_BUILD_IOS */

#if CURL_BUILD_MAC_10_15 || CURL_BUILD_IOS_13
  /* ECDHE_PSK Cipher Suites for Transport Layer Security (TLS), RFC 5489 */
  CIPHER_DEF(TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA,                   /* 0xC035 */
             "ECDHE-PSK-AES128-CBC-SHA",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_PSK_WITH_AES_256_CBC_SHA,                   /* 0xC036 */
             "ECDHE-PSK-AES256-CBC-SHA",
             CIPHER_STRONG_ENOUGH),
#endif /* CURL_BUILD_MAC_10_15 || CURL_BUILD_IOS_13 */

#if CURL_BUILD_MAC_10_13 || CURL_BUILD_IOS_11
  /* Addenda from rfc 7905  ChaCha20-Poly1305 Cipher Suites for
     Transport Layer Security (TLS). */
  CIPHER_DEF(TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,          /* 0xCCA8 */
             "ECDHE-RSA-CHACHA20-POLY1305",
             CIPHER_STRONG_ENOUGH),
  CIPHER_DEF(TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,        /* 0xCCA9 */
             "ECDHE-ECDSA-CHACHA20-POLY1305",
             CIPHER_STRONG_ENOUGH),
#endif /* CURL_BUILD_MAC_10_13 || CURL_BUILD_IOS_11 */

#if CURL_BUILD_MAC_10_15 || CURL_BUILD_IOS_13
  /* ChaCha20-Poly1305 Cipher Suites for Transport Layer Security (TLS),
     RFC 7905 */
  CIPHER_DEF(TLS_PSK_WITH_CHACHA20_POLY1305_SHA256,                /* 0xCCAB */
             "PSK-CHACHA20-POLY1305",
             CIPHER_STRONG_ENOUGH),
#endif /* CURL_BUILD_MAC_10_15 || CURL_BUILD_IOS_13 */

  /* Tags for SSL 2 cipher kinds which are not specified for SSL 3.
     Defined since SDK 10.2.8 */
  CIPHER_DEF(SSL_RSA_WITH_RC2_CBC_MD5,                             /* 0xFF80 */
             NULL,
             CIPHER_WEAK_RC_ENCRYPTION),
  CIPHER_DEF(SSL_RSA_WITH_IDEA_CBC_MD5,                            /* 0xFF81 */
             NULL,
             CIPHER_WEAK_IDEA_ENCRYPTION),
  CIPHER_DEF(SSL_RSA_WITH_DES_CBC_MD5,                             /* 0xFF82 */
             NULL,
             CIPHER_WEAK_DES_ENCRYPTION),
  CIPHER_DEF(SSL_RSA_WITH_3DES_EDE_CBC_MD5,                        /* 0xFF83 */
             NULL,
             CIPHER_WEAK_3DES_ENCRYPTION),
};

#define NUM_OF_CIPHERS sizeof(ciphertable)/sizeof(ciphertable[0])

static const char* SSLCipherSuiteToString(SSLCipherSuite cipher)
{
  /* The first ciphers in the ciphertable are continuos. Here we do small
     optimization and instead of loop directly get SSL name by cipher number.
   */
  if(cipher <= SSL_FORTEZZA_DMS_WITH_FORTEZZA_CBC_SHA) {
    return ciphertable[cipher].name;
  }
  /* Iterate through the rest of the ciphers */
  for(size_t i = SSL_FORTEZZA_DMS_WITH_FORTEZZA_CBC_SHA + 1;
      i < NUM_OF_CIPHERS;
      ++i) {
    if(ciphertable[i].num == cipher) {
      return ciphertable[i].name;
    }
  }
  return ciphertable[SSL_NULL_WITH_NULL_NULL].name;
}

static bool is_cipher_suite_strong(SSLCipherSuite suite_num)
{
  for(size_t i = 0; i < NUM_OF_CIPHERS; ++i) {
    if(ciphertable[i].num == suite_num) {
      return !ciphertable[i].weak;
    }
  }
  /* If the cipher is not in our list, assume it is a new one
     and therefore strong. Previous implementation was the same,
     if cipher suite is not in the list, it was considered strong enough */
  return true;
}

const char* hssl_backend() {
    return "appletls";
}

typedef struct appletls_ctx {
    SecIdentityRef cert;
    hssl_ctx_init_param_t* param;
} appletls_ctx_t;

hssl_ctx_t hssl_ctx_init(hssl_ctx_init_param_t* param) {
    appletls_ctx_t* ctx = (appletls_ctx_t*)malloc(sizeof(appletls_ctx_t));
    if (ctx == NULL) return NULL;
    ctx->cert = NULL;
    ctx->param = param;
    g_ssl_ctx = ctx;
    return ctx;
}

void hssl_ctx_cleanup(hssl_ctx_t ssl_ctx) {
    if (ssl_ctx == NULL) return;
    appletls_ctx_t* ctx = (appletls_ctx_t*)ssl_ctx;
    if (ctx->cert) {
        CFRelease(ctx->cert);
        ctx->cert = NULL;
    }
    free(ctx);
}

typedef struct appletls_s {
    SSLContextRef session;
    appletls_ctx_t* ctx;
    int fd;
} appletls_t;

hssl_t hssl_new(hssl_ctx_t ssl_ctx, int fd) {
    if (ssl_ctx == NULL) return NULL;
    appletls_t* appletls = (appletls_t*)malloc(sizeof(appletls_t));
    if (appletls == NULL) return NULL;
    appletls->session = NULL;
    appletls->ctx = (appletls_ctx_t*)ssl_ctx;
    appletls->fd = fd;
    return (hssl_t)appletls;
}

static OSStatus SocketRead(SSLConnectionRef conn, void* data, size_t* len) {
    // printf("SocketRead(%d)\n", (int)*len);
    appletls_t* appletls = (appletls_t*)conn;
    uint8_t* buffer = (uint8_t*)data;
    size_t remain = *len;
    *len = 0;
    int fd = appletls->fd;
    // int timeout = 1000;
    // struct timeval tv = { timeout / 1000, (timeout % 1000) * 1000 };
    // fd_set readfds;
    while (remain) {
        /*
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        int nselect = select(fd + 1, &readfds, 0, 0, &tv);
        printf("nselect=%d\n", nselect);
        if (nselect < 0) {
            return errSSLClosedAbort;
        }
        if (nselect == 0) {
            return errSSLWouldBlock;
        }
        */
        // printf("read(%d)\n", (int)remain);
        // NOTE: avoid blocking
        if (remain < 16) {
            so_rcvtimeo(fd, 1000);
        }
        ssize_t nread = read(fd, buffer, remain);
        // printf("nread=%d errno=%d\n", (int)nread, (int)errno);
        if (nread == 0) return errSSLClosedGraceful;
        if (nread < 0) {
            switch (errno) {
            case ENOENT:    return errSSLClosedGraceful;
            case ECONNRESET:return errSSLClosedAbort;
            case EAGAIN:    return errSSLWouldBlock;
            default:        return errSSLClosedAbort;
            }
        }
        *len += nread;
        remain -= nread;
        buffer += nread;
    }
    return noErr;
}

static OSStatus SocketWrite(SSLConnectionRef conn, const void* data, size_t* len) {
    // printf("SocketWrite(%d)\n", (int)*len);
    appletls_t* appletls = (appletls_t*)conn;
    uint8_t* buffer = (uint8_t*)data;
    size_t remain = *len;
    *len = 0;
    int fd = appletls->fd;
    while (remain) {
        if (remain < 16) {
            so_sndtimeo(fd, 1000);
        }
        // printf("write(%d)\n", (int)remain);
        ssize_t nwrite = write(fd, buffer, remain);
        // printf("nwrite=%d errno=%d\n", (int)nwrite, (int)errno);
        if (nwrite <= 0) {
            switch (errno) {
            case EAGAIN:    return errSSLWouldBlock;
            default:        return errSSLClosedAbort;
            }
        }
        remain -= nwrite;
        buffer += nwrite;
        *len += nwrite;
    }
    return noErr;
}

static int hssl_init(hssl_t ssl, int endpoint) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    OSStatus ret = noErr;
    if (appletls->session == NULL) {
#if defined(__MAC_10_8)
        appletls->session = SSLCreateContext(NULL, endpoint == HSSL_SERVER ? kSSLServerSide : kSSLClientSide, kSSLStreamType);
#else
        SSLNewContext(endpoint == HSSL_SERVER, &(appletls->session));
#endif
    }
    if (appletls->session == NULL) {
        fprintf(stderr, "SSLCreateContext failed!\n");
        return HSSL_ERROR;
    }

    ret = SSLSetProtocolVersionEnabled(appletls->session, kSSLProtocolAll, true);
    if (ret != noErr) {
        fprintf(stderr, "SSLSetProtocolVersionEnabled failed!\n");
        return HSSL_ERROR;
    }

    bool verify_peer = false;
    if (appletls->ctx->param && appletls->ctx->param->verify_peer) {
        verify_peer = true;
    }
#if defined(__MAC_10_8)
    ret = SSLSetSessionOption(appletls->session, kSSLSessionOptionBreakOnServerAuth, !verify_peer);
#else
    ret = SSLSetEnableCertVerify(appletls->session, verify_peer);
#endif
    if (ret != noErr) {
        fprintf(stderr, "SSLSetEnableCertVerify failed!\n");
        return HSSL_ERROR;
    }

    if (appletls->ctx->cert) {
        CFArrayRef certs = CFArrayCreate(NULL, (const void**)&appletls->ctx->cert, 1, NULL);
        if (!certs) {
            fprintf(stderr, "CFArrayCreate failed!\n");
            return HSSL_ERROR;
        }
        ret = SSLSetCertificate(appletls->session, certs);
        CFRelease(certs);
        if (ret != noErr) {
            fprintf(stderr, "SSLSetCertificate failed!\n");
            return HSSL_ERROR;
        }
    }

    size_t all_ciphers_count = 0, allowed_ciphers_count = 0;
    SSLCipherSuite *all_ciphers = NULL, *allowed_ciphers = NULL;
    ret = SSLGetNumberSupportedCiphers(appletls->session, &all_ciphers_count);
    if (ret != noErr) {
        fprintf(stderr, "SSLGetNumberSupportedCiphers failed!\n");
        goto error;
    }
    all_ciphers = (SSLCipherSuite*)malloc(all_ciphers_count * sizeof(SSLCipherSuite));
    allowed_ciphers = (SSLCipherSuite*)malloc(all_ciphers_count * sizeof(SSLCipherSuite));
    if (all_ciphers == NULL || allowed_ciphers == NULL) {
        fprintf(stderr, "malloc failed!\n");
        goto error;
    }
    ret = SSLGetSupportedCiphers(appletls->session, all_ciphers, &all_ciphers_count);
    if (ret != noErr) {
        fprintf(stderr, "SSLGetSupportedCiphers failed!\n");
        goto error;
    }
    for (size_t i = 0; i < all_ciphers_count; ++i) {
        if (is_cipher_suite_strong(all_ciphers[i])) {
            allowed_ciphers[allowed_ciphers_count++] = all_ciphers[i];
        }
    }
    ret = SSLSetEnabledCiphers(appletls->session, allowed_ciphers, allowed_ciphers_count);
    if (ret != noErr) {
        fprintf(stderr, "SSLSetEnabledCiphers failed!\n");
        goto error;
    }
    if (all_ciphers) {
        free(all_ciphers);
        all_ciphers = NULL;
    }
    if (allowed_ciphers) {
        free(allowed_ciphers);
        allowed_ciphers = NULL;
    }

    ret = SSLSetIOFuncs(appletls->session, SocketRead, SocketWrite);
    if (ret != noErr) {
        fprintf(stderr, "SSLSetIOFuncs failed!\n");
        return HSSL_ERROR;
    }
    ret = SSLSetConnection(appletls->session, appletls);
    if (ret != noErr) {
        fprintf(stderr, "SSLSetConnection failed!\n");
        return HSSL_ERROR;
    }

    /*
    char session_id[64] = {0};
    int session_id_len = snprintf(session_id, sizeof(session_id), "libhv:appletls:%p", appletls->session);
    ret = SSLSetPeerID(appletls->session, session_id, session_id_len);
    if (ret != noErr) {
        fprintf(stderr, "SSLSetPeerID failed!\n");
        return HSSL_ERROR;
    }
    */

    return HSSL_OK;
error:
    if (all_ciphers) {
        free(all_ciphers);
    }
    if (allowed_ciphers) {
        free(allowed_ciphers);
    }
    return HSSL_ERROR;
}

void hssl_free(hssl_t ssl) {
    if (ssl == NULL) return;
    appletls_t* appletls = (appletls_t*)ssl;
    if (appletls->session) {
#if defined(__MAC_10_8)
        CFRelease(appletls->session);
#else
        SSLDisposeContext(appletls->session);
#endif
        appletls->session = NULL;
    }
    free(appletls);
}

static int hssl_handshake(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    OSStatus ret = SSLHandshake(appletls->session);
    // printf("SSLHandshake retval=%d\n", (int)ret);
    switch(ret) {
    case noErr:
        break;
    case errSSLWouldBlock:
        return HSSL_WANT_READ;
    case errSSLPeerAuthCompleted: /* peer cert is valid, or was ignored if verification disabled */
        return hssl_handshake(ssl);
    case errSSLBadConfiguration:
        return HSSL_WANT_READ;
    default:
        return HSSL_ERROR;
    }

    /*
    SSLProtocol protocol = kSSLProtocolUnknown;
    SSLGetNegotiatedProtocolVersion(appletls->session, &protocol);
    SSLCipherSuite cipher = SSL_NO_SUCH_CIPHERSUITE;
    SSLGetNegotiatedCipher(appletls->session, &cipher);
    printf("* %s connection using %s\n", SSLProtocolToString(protocol), SSLCipherSuiteToString(cipher));
    */

    return HSSL_OK;
}

int hssl_accept(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    if (appletls->session == NULL) {
        hssl_init(ssl, HSSL_SERVER);
    }
    return hssl_handshake(ssl);
}

int hssl_connect(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    if (appletls->session == NULL) {
        hssl_init(ssl, HSSL_CLIENT);
    }
    return hssl_handshake(ssl);
}

int hssl_read(hssl_t ssl, void* buf, int len) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    size_t processed = 0;
    // printf("SSLRead(%d)\n", len);
    OSStatus ret = SSLRead(appletls->session, buf, len, &processed);
    // printf("SSLRead retval=%d processed=%d\n", (int)ret, (int)processed);
    switch (ret) {
    case noErr:
        return processed;
    case errSSLWouldBlock:
        return processed ? processed : HSSL_WOULD_BLOCK;
    case errSSLClosedGraceful:
    case errSSLClosedNoNotify:
        return 0;
    default:
        return HSSL_ERROR;
    }
}

int hssl_write(hssl_t ssl, const void* buf, int len) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    size_t processed = 0;
    // printf("SSLWrite(%d)\n", len);
    OSStatus ret = SSLWrite(appletls->session, buf, len, &processed);
    // printf("SSLWrite retval=%d processed=%d\n", (int)ret, (int)processed);
    switch (ret) {
    case noErr:
        return processed;
    case errSSLWouldBlock:
        return processed ? processed : HSSL_WOULD_BLOCK;
    case errSSLClosedGraceful:
    case errSSLClosedNoNotify:
        return 0;
    default:
        return HSSL_ERROR;
    }
}

int hssl_close(hssl_t ssl) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    SSLClose(appletls->session);
    return 0;
}

int hssl_set_sni_hostname(hssl_t ssl, const char* hostname) {
    if (ssl == NULL) return HSSL_ERROR;
    appletls_t* appletls = (appletls_t*)ssl;
    if (appletls->session == NULL) {
        hssl_init(ssl, HSSL_CLIENT);
    }
    SSLSetPeerDomainName(appletls->session, hostname, strlen(hostname));
    return 0;
}

#endif // WITH_APPLETLS
