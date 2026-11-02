//@ compile-variant: -O
#![feature(extern_types)]
unsafe extern "C" { type Foreign; }
trait Describe { fn width() -> usize; }
impl Describe for Foreign { fn width() -> usize { core::mem::size_of::<*const Foreign>() } }
fn main() { assert_eq!(<Foreign as Describe>::width(), core::mem::size_of::<usize>()); }
