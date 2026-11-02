//@ aux-build: delegated_type_only_signature.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]
use delegated_type_only_signature::{Source, TypeOnly};
struct Wrapped(Source);
impl TypeOnly for Wrapped { reuse TypeOnly::echo { self.0 } }
fn main() {
    assert_eq!(Wrapped(Source).echo(Some(23u16)), Some(23));
    assert_eq!(Wrapped(Source).echo(None::<u32>), None);
}
