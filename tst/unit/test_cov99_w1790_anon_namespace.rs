//@ proc-macro-aux-build: cov99_w1790_anon_namespace.rs
//@ compile-fail: Couldn't find node 0 of path
//@ compile-flags: --emit=metadata -Cpanic=abort

const _: () = {
    pub struct Witness;
};

cov99_w1790_anon_namespace::import_anon!();
