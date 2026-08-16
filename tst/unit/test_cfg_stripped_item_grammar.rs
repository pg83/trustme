// Items that a `cfg` removes still have to parse: the checks that reject them
// run later. Several forms failed outright — a free const or static without a
// value, a type alias with bounds or without a body, an impl type without a
// body, and an extern type with the full alias grammar.
//
// Same shapes as the ui tests parser/item-free-*-syntactic-pass.rs,
// parser/impl-item-type-no-body-pass.rs and parser/foreign-ty-syntactic-pass.rs.
#[cfg(FALSE)]
const FREE_CONST: u8;

#[cfg(FALSE)]
static FREE_STATIC: u8;

#[cfg(FALSE)]
fn aliases() {
    type A: Ord;
    type B: Ord = u8;
    type D<_T>: Ord;
}

struct X;

#[cfg(FALSE)]
impl X {
    type Y;
    type Z: Ord;
    type W: Ord where Self: Eq;
}

#[cfg(FALSE)]
extern "C" {
    type A: Ord;
    type B<'a> where 'a: 'static;
    type C = u8;
}

fn main() {
    let _ = X;
}
