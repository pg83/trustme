//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ proc-macro-aux-build: cov99_w430_cstring_aux.rs

extern crate cov99_w430_cstring_aux;

cov99_w430_cstring_aux::cstring!();
