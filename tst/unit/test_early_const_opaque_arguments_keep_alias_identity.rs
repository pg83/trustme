#![feature(type_alias_impl_trait)]
type Hidden = impl Copy;
#[define_opaque(Hidden)]
const fn make() -> Hidden { 37u32 }
const fn choose((choice, _payload): (u8, Hidden)) -> usize {
    match choice { 0 | 1 => 3, _ => 5 }
}
type Selected = [u8; choose((1, make()))];
fn main() {
    let value: Selected = [17; 3];
    assert_eq!(value, [17; 3]);
    assert_eq!(choose((2, make())), 5);
}
