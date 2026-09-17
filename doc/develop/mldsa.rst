.. SPDX-License-Identifier: GPL-2.0-or-later

ML-DSA verification
===================

U-Boot provides a FIT-independent verifier for pure ML-DSA with an empty
context string. ``CONFIG_MLDSA_VERIFY`` enables ML-DSA-44, ML-DSA-65 and
ML-DSA-87; it selects the generic SHAKE implementation. The public API in
``include/crypto/mldsa.h`` accepts the raw FIPS 204 public-key and signature
encodings. It does not parse PEM or DER and contains no signing or private-key
code.

Verification makes a single heap allocation whose size depends on the
parameter set and ABI alignment. Targets with 8-byte ``u64`` alignment use
8800 bytes for ML-DSA-44, 10336 bytes for ML-DSA-65 and 12896 bytes for
ML-DSA-87. This includes x86_64 and 32-bit ARM EABI. Targets with 4-byte
``u64`` alignment use 8792 bytes, 10328 bytes and 12888 bytes respectively.
There is no message-sized allocation or copy. The workspace size does not
scale with the size of the signed data.

The initial API accepts one contiguous message. Its Linux origin does the same.
There is no FIT or mkimage integration yet. A follow-up can add a generic
incremental API that lets the verifier absorb FIT signed regions directly.

Origin
------

The verifier, SHAKE code, APIs and test vectors were adapted from these exact
Linux file revisions::

    include/crypto/sha3.h
        f1799d17285ca99243328cd92133a9f84ee3a593
    lib/crypto/sha3.c
        0354d3c1f1b8628e60eceb304b6d2ef75eea6f41
    include/crypto/mldsa.h
    lib/crypto/mldsa.c
        ffd42b6d0420c4be97cc28fd1bb5f4c29e286e98
    lib/crypto/tests/mldsa_kunit.c
    lib/crypto/tests/mldsa-testvecs.h
        ed894faccb8de55cd755e093c4b0971f190d384d

Each imported file header links to its corresponding kernel.org commit. The
SHA-3 library was introduced by Linux commit
``0593447248044ab609b43b947d0e198c887ac281``. ML-DSA verification was
introduced by Linux commit ``64edccea594cf7cb1e2975fdf44531e3377b32db``.

The Linux SPDX identifiers, copyright notices and algorithm commentary are
retained. The module boilerplate, symbol exports, FIPS self-tests and KUnit
harness were dropped. ``crypto_xor()``, ``mem_is_zero()`` and
``memzero_explicit()`` were replaced by local equivalents. ``kmalloc()`` and
``kfree_sensitive()`` were replaced by ``malloc()`` with an explicit zeroize
before ``free()``. The ``WARN_ON_ONCE()`` in ``__sha3_update()`` was replaced
by an early return.

Testing
-------

``CONFIG_UT_LIB_MLDSA`` adds unit tests that cover all three parameter sets
using the fixed Linux test vectors. It also adds FIPS 202 SHAKE128 and
SHAKE256 known-answer tests::

    ut lib lib_shake
    ut lib lib_mldsa44
    ut lib lib_mldsa65
    ut lib lib_mldsa87
    ut lib lib_mldsa_invalid_alg
    ut lib lib_mldsa_use_hint
