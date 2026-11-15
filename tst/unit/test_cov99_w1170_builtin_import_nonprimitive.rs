//@ compile-only
//@ compile-flags: -Z stop-after=resolve -Z dump-ast
//@ proc-macro-aux-build: cov99_w1170_builtin_path_macro.rs
//@ aux-build: cov99_w1170_builtin_path_exports.rs

extern crate cov99_w1170_builtin_path_exports;

type Resolved = cov99_w1170_builtin_path_exports::BuiltinAlias;

fn main() {}
