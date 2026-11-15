//@ crate-type: lib
//@ compile-only
//@ proc-macro-aux-build: test_cov99_w957_slot_transport.rs
//@ compile-flags: --emit=dep-info -Zmir-opt-level=0 -Cpanic=abort -Zdump-hir -Zdump-mir
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-slot

extern crate test_cov99_w957_slot_transport;

test_cov99_w957_slot_transport::slot_witness!();
