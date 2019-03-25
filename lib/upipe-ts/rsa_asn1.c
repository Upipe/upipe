#include <libtasn1.h>

/* generated with asn1Parser, using:

RSA {}

DEFINITIONS EXPLICIT TAGS ::=
BEGIN

RSAPublicKey ::= SEQUENCE {
    modulus         INTEGER, -- n
    publicExponent  INTEGER  -- e
}

RSAPrivateKey ::= SEQUENCE {
    version INTEGER,
    modulus INTEGER, -- n
    publicExponent INTEGER, -- e
    privateExponent INTEGER, -- d
    prime1 INTEGER, -- p
    prime2 INTEGER, -- q
    exponent1 INTEGER, -- d mod (p-1)
    exponent2 INTEGER, -- d mod (q-1)
    coefficient INTEGER -- (inverse of q) mod p
}

PublicKeyInfo ::= SEQUENCE {
    algorithm       AlgorithmIdentifier,
    PublicKey       BIT STRING
}

PrivateKeyInfo ::= SEQUENCE {
    version     INTEGER,
    algorithm   AlgorithmIdentifier,
    PrivateKey  OCTET STRING
}

AlgorithmIdentifier ::= SEQUENCE {
    algorithm       OBJECT IDENTIFIER,
    parameters      NULL
}

rsaEncryption OBJECT IDENTIFIER ::= {
    iso(1) member-body(2) us(840) rsadsi(113549) pkcs(1) pkcs-1(1) 1 }

END
*/

const asn1_static_node rsa_asn1_tab[] = {
  { "RSA", 536872976, NULL },
  { NULL, 1073741836, NULL },
  { "RSAPublicKey", 1610612741, NULL },
  { "modulus", 1073741827, NULL },
  { "publicExponent", 3, NULL },
  { "RSAPrivateKey", 1610612741, NULL },
  { "version", 1073741827, NULL },
  { "modulus", 1073741827, NULL },
  { "publicExponent", 1073741827, NULL },
  { "privateExponent", 1073741827, NULL },
  { "prime1", 1073741827, NULL },
  { "prime2", 1073741827, NULL },
  { "exponent1", 1073741827, NULL },
  { "exponent2", 1073741827, NULL },
  { "coefficient", 3, NULL },
  { "PublicKeyInfo", 1610612741, NULL },
  { "algorithm", 1073741826, "AlgorithmIdentifier"},
  { "PublicKey", 6, NULL },
  { "PrivateKeyInfo", 1610612741, NULL },
  { "version", 1073741827, NULL },
  { "algorithm", 1073741826, "AlgorithmIdentifier"},
  { "PrivateKey", 7, NULL },
  { "AlgorithmIdentifier", 1610612741, NULL },
  { "algorithm", 1073741836, NULL },
  { "parameters", 20, NULL },
  { "rsaEncryption", 805306380, NULL },
  { "iso", 1073741825, "1"},
  { "member-body", 1073741825, "2"},
  { "us", 1073741825, "840"},
  { "rsadsi", 1073741825, "113549"},
  { "pkcs", 1073741825, "1"},
  { "pkcs-1", 1073741825, "1"},
  { NULL, 1, "1"},
  { NULL, 0, NULL }
};
