# Apple TLS test certificates

All private keys in this directory are disposable test fixtures and must never
be used outside the unit tests. Run `generate.sh` from any directory to replace
the fixture set. The checked-in files let tests run without an OpenSSL runtime
dependency.
