#![feature(rustc_attrs)]
#![allow(internal_features)]
#[macro_export]
#[rustc_builtin_macro]
macro_rules! cfg { ($($tokens:tt)*) => {}; }
use self::cfg as enabled;
use enabled as predicate;
fn main() {
    assert!(predicate!(all()));
    assert!(!predicate!(any()));
}
