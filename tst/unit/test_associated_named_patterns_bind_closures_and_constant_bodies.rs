#![feature(more_qualified_paths)]
#![allow(incomplete_features)]
struct Pair { first: u16, second: u16 }
trait Family { type Product; }
struct Marker;
impl Family for Marker { type Product = Pair; }
const SIZE: usize = {
    let <Marker as Family>::Product { first, second } = Pair { first: 2, second: 3 };
    (first + second) as usize
};
fn main() {
    let add = |<Marker as Family>::Product { first, second }: Pair| first + second;
    assert_eq!(add(Pair { first: 37, second: 41 }), 78);
    assert_eq!([7u8; SIZE], [7, 7, 7, 7, 7]);
}
