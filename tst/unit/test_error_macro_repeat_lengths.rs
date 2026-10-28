//@ compile-fail: Mismatch in loop iterations
macro_rules! bad { (($($x:ident),*); ($($y:ident),*)) => { $(let $x = stringify!($y);)* } }
fn main() { bad!((a, b); (c)); }
