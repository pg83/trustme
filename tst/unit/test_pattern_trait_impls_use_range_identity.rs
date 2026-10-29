#![feature(pattern_types, pattern_type_macro, const_trait_impl, pattern_type_range_trait)]
use std::pat::pattern_type;
type Closed = pattern_type!(i32 is 1..=3);
type Open = pattern_type!(i32 is 1..3);
type Wide = pattern_type!(i32 is 1..=7);
type Sparse = pattern_type!(i32 is ..=-1 | 1..);
type Unsigned = pattern_type!(u32 is 1..=3);
struct Pair<A, B>(A, B);
trait Route { fn code(self) -> u8; }
impl<T> Route for Pair<T, T> { fn code(self) -> u8 { 0 } }
impl Route for Pair<Closed, Open> { fn code(self) -> u8 { 1 } }
impl Route for Pair<Closed, Wide> { fn code(self) -> u8 { 2 } }
impl Route for Pair<Sparse, Closed> { fn code(self) -> u8 { 3 } }
impl Route for Pair<Closed, Unsigned> { fn code(self) -> u8 { 4 } }
fn route<T: Route>(value: T) -> u8 { value.code() }
fn main() {
    let closed: Closed = 2;
    let open: Open = 2;
    let wide: Wide = 2;
    let sparse: Sparse = 6;
    let signed: Unsigned = 2;
    assert_eq!(route(Pair(closed, closed)), 0);
    assert_eq!(route(Pair(closed, open)), 1);
    assert_eq!(route(Pair(closed, wide)), 2);
    assert_eq!(route(Pair(sparse, closed)), 3);
    assert_eq!(route(Pair(closed, signed)), 4);
}
