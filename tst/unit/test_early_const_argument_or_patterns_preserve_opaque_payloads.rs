#![feature(type_alias_impl_trait)]
type Hidden = impl Copy;
#[define_opaque(Hidden)]
const fn make() -> Hidden { 37u32 }
const fn choose((true | false, _payload): (bool, Hidden)) -> usize { 3 }
type Selected = [u8; choose((true, make()))];
fn main() {
    let value: Selected = [17; 3];
    assert_eq!(value, [17; 3]);
    assert_eq!((choose((false, make())), choose((true, make()))), (3, 3));
}
