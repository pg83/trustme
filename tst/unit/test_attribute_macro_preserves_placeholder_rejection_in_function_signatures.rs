//@ proc-macro-aux-build: preserve_bound_types.rs
//@ compile-fail: the placeholder `_` is not allowed in function signatures
use preserve_bound_types::preserve;

#[preserve]
fn inferred(value: _) -> u16 { let _ = value; 0 }

fn main() {}
