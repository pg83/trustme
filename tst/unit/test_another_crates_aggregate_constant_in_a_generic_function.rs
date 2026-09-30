//@ aux-build: aggregate_const_item.rs
// hashbrown's `RawTableInner::fallible_with_capacity::<A>` reads `Self::NEW`, a
// constant of a 32-byte struct. Its value is kept as one encoded allocation in
// the generic function's MIR, and that MIR reaches another crate through the
// metadata: the allocation's type is bound again there, like the function's
// locals, before codegen names it.
extern crate aggregate_const_item;

use aggregate_const_item::Big;

fn main() {
    let big = Big::fresh::<u8>();
    assert_eq!(big.a + big.b + big.c, 6);
    let direct = Big::NEW;
    assert_eq!(direct.c, 3);
}
