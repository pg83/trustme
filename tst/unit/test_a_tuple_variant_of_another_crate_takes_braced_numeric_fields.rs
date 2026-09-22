//@ aux-build: tuple_variant_braced_aux.rs
// time-macros' `format_description!` expands to
// `BorrowedFormatItem::Component { 0: .. }` naming an enum of the `time` crate:
// a struct expression whose path is a tuple variant of another crate, with
// numbered fields. Upstream accepts a tuple variant or tuple struct in a struct
// expression or pattern (`check_expr_struct_fields` by field index), wherever
// it is defined.
extern crate tuple_variant_braced_aux as aux;
use aux::Item;

fn main() {
    let c = aux::Item::Component { 0: 7 };
    assert_eq!(c, Item::Component(7));
    let list = ::aux::Item::Compound { 0: &[Item::Literal { 0: b"x" }, Item::Component { 0: 1 }] };
    match list {
        Item::Compound { 0: items } => assert_eq!(items.len(), 2),
        _ => panic!(),
    }
    let p = aux::nested::Pair { 0: 1, 1: 2 };
    assert_eq!(p, aux::nested::Pair(1, 2));
}
