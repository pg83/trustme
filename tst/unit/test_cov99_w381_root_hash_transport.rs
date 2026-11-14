//@ proc-macro-aux-build: cov99_w381_hash_root_macro.rs
//@ compile-fail: Invalid path (no nodes)
//@ compile-flags: --emit=metadata -Cpanic=abort

extern crate cov99_w381_hash_root_macro;

cov99_w381_hash_root_macro::root_path!();

fn main() {}
