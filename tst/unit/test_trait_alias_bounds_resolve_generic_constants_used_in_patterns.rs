#![feature(trait_alias, generic_const_items)]
#![allow(incomplete_features)]
trait Limits { const EDGE<const N: u32>: u32; }
trait Bound = Limits;
struct Table;
impl Limits for Table { const EDGE<const N: u32>: u32 = N; }
impl Table { const END<const N: u32>: u32 = N; }
const START<const N: u32>: u32 = N;
fn limit<T: Bound, const N: u32>() -> u32 { T::EDGE::<N> }
fn inside(value: u32) -> bool {
    matches!(value, START::<{1 + 1}>..=Table::END::<{2 + 3}>)
}
fn main() {
    assert_eq!(limit::<Table, {3 + 2}>(), 5);
    assert!(!inside(1));
    assert!(inside(2));
    assert!(inside(5));
    assert!(!inside(6));
}
