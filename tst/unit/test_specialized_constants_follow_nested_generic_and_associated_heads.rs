//@ compile-variant: -O
#![feature(specialization)]
#![allow(incomplete_features)]

trait Key { const VALUE: u8; }
impl<T> Key for T { default const VALUE: u8 = 3; }
impl<T: Copy> Key for Option<T> { default const VALUE: u8 = 5; }
impl Key for Option<u16> { const VALUE: u8 = 7; }

trait Associated { type Item; }
struct Small;
impl Associated for Small { type Item = u16; }
struct Other;
impl Associated for Other { type Item = String; }

fn generic<T: Key>() -> u8 { T::VALUE }
fn nested<T: Copy>() -> u8 { <Option<T> as Key>::VALUE }
fn projected<T: Associated>() -> u8 { <Option<T::Item> as Key>::VALUE }

fn main() {
    assert_eq!(generic::<String>(), 3);
    assert_eq!(generic::<Option<String>>(), 3);
    assert_eq!(generic::<Option<u8>>(), 5);
    assert_eq!(generic::<Option<u16>>(), 7);
    assert_eq!(nested::<u8>(), 5);
    assert_eq!(nested::<u16>(), 7);
    assert_eq!(projected::<Small>(), 7);
    assert_eq!(projected::<Other>(), 3);
}
