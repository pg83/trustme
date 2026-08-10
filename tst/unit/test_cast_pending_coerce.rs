// A pending coercion to <B as Cbp>::Bits was clobbered by the pointer-cast
// fallback (bytemuck's checked slice casts).
trait Abp: Copy + 'static {}
impl Abp for u8 {}
trait Cbp { type Bits: Abp; fn ok(b: &Self::Bits) -> bool; }
impl<T: Abp> Cbp for T { type Bits = T; fn ok(_: &T) -> bool { true } }
#[derive(Clone, Copy)] enum E { A, B }
impl Cbp for E { type Bits = u8; fn ok(b: &u8) -> bool { *b < 2 } }
fn cast<A: Abp, B: Abp>(a: &[A]) -> &[B] {
    unsafe { core::slice::from_raw_parts(a.as_ptr() as *const B, a.len()) }
}
fn checked<A: Abp, B: Cbp>(a: &[A]) -> bool {
    let pod = cast::<A, B::Bits>(a);
    pod.iter().all(|p| <B as Cbp>::ok(p))
}
fn main() {
    assert!(checked::<u8, E>(&[0u8, 1, 1]));
    assert!(!checked::<u8, E>(&[0u8, 7]));
}
