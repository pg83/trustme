#![feature(arbitrary_self_types, generic_const_exprs)]
#![allow(incomplete_features)]
struct Named;
static ITEM: Named = Named;
trait Share<const N: usize> { type Ref; }
impl Share<0> for Named { type Ref = &'static Named; }
const fn extent() -> usize {
    let _closure = || 3u8;
    0
}
impl Named {
    fn value(self: <Self as Share<{ extent() }>>::Ref) -> u8 { 7 }
}
fn main() {
    assert_eq!(Named::value(&ITEM), 7);
}
