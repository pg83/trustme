// A trait's vtable struct carries one generic argument per associated type, and each one sits
// at the slot the trait recorded for that name.  With two or more associated types the vtable
// static was spelled by walking the (hashed) name->slot map and appending, so its argument list
// came out permuted against the one the enumeration and the MIR use - a second, never-declared
// struct type for the same vtable ("unknown type name 's_ZR...'" in the generated C).  rustc
// spells a trait object's associated types once, in a canonical order, and asserts it when the
// list is interned: `TyCtxt::mk_poly_existential_predicates` (rustc_middle/src/ty/context.rs)
// rejects a list not sorted by `ExistentialPredicate::stable_cmp`, an order that by construction
// "will not change if modules are reordered".  (proptest's `Strategy`, with `Tree` and `Value`)
trait Strategy {
    type Tree;
    type Value;
    fn tag(&self) -> u32;
    fn width(&self) -> usize;
}

struct Fixed;

impl Strategy for Fixed {
    type Tree = u8;
    type Value = u16;
    fn tag(&self) -> u32 {
        7
    }
    fn width(&self) -> usize {
        3
    }
}

fn main() {
    let object: &dyn Strategy<Tree = u8, Value = u16> = &Fixed;
    assert_eq!(object.tag(), 7);
    assert_eq!(object.width(), 3);
}
