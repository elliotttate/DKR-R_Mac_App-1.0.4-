#pragma once

// WebRTC data channels use DTLS-SRTP key export even when DKR-R carries no
// media tracks. The upstream Mbed TLS LTS profile leaves this extension off
// by default, so enable precisely the protocol facility libdatachannel needs.
#define MBEDTLS_SSL_DTLS_SRTP
