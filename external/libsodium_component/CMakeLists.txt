set(SODIUM_ROOT ${CMAKE_SOURCE_DIR}/external/libsodium-esphome/libsodium/src/libsodium)
set(SODIUM_PORT_ROOT ${CMAKE_SOURCE_DIR}/external/libsodium-esphome/port)

set(srcs
    ${SODIUM_ROOT}/crypto_stream/salsa20/ref/salsa20_ref.c
    ${SODIUM_ROOT}/crypto_core/salsa/ref/core_salsa_ref.c
    ${SODIUM_ROOT}/randombytes/randombytes.c
    ${SODIUM_ROOT}/crypto_aead/chacha20poly1305/sodium/aead_chacha20poly1305.c
    ${SODIUM_ROOT}/crypto_hash/crypto_hash.c
    ${SODIUM_ROOT}/crypto_hash/sha256/cp/hash_sha256_cp.c
    ${SODIUM_ROOT}/crypto_hash/sha256/hash_sha256.c
    ${SODIUM_ROOT}/crypto_onetimeauth/crypto_onetimeauth.c
    ${SODIUM_ROOT}/crypto_onetimeauth/poly1305/donna/poly1305_donna.c
    ${SODIUM_ROOT}/crypto_onetimeauth/poly1305/onetimeauth_poly1305.c
    ${SODIUM_ROOT}/crypto_scalarmult/crypto_scalarmult.c
    ${SODIUM_ROOT}/crypto_scalarmult/curve25519/ref10/x25519_ref10.c
    ${SODIUM_ROOT}/crypto_core/ed25519/ref10/ed25519_ref10.c
    ${SODIUM_ROOT}/crypto_scalarmult/curve25519/scalarmult_curve25519.c
    ${SODIUM_ROOT}/crypto_stream/chacha20/ref/chacha20_ref.c
    ${SODIUM_ROOT}/crypto_stream/chacha20/stream_chacha20.c
    ${SODIUM_ROOT}/crypto_stream/crypto_stream.c
    ${SODIUM_ROOT}/crypto_verify/verify.c
    ${SODIUM_ROOT}/sodium/codecs.c
    ${SODIUM_ROOT}/sodium/core.c
    ${SODIUM_ROOT}/sodium/runtime.c
    ${SODIUM_ROOT}/sodium/utils.c
    ${SODIUM_PORT_ROOT}/version.c
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        ${SODIUM_ROOT}/include
        ${SODIUM_ROOT}/include/sodium
        ${CMAKE_SOURCE_DIR}/external/libsodium-esphome/port_include
)

target_compile_definitions(${COMPONENT_TARGET} PRIVATE CONFIGURED=1)
component_compile_options(
    -Wno-unused-function
    -Wno-unknown-pragmas
    -Wno-unused-variable
    -Wno-type-limits
)
