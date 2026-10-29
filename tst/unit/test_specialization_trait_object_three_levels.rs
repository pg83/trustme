#![feature(specialization)]
trait View<A, B> {}
struct Value;
impl<A, B> View<A, B> for Value {}
trait Rank { fn rank(&self) -> u8; }
impl Rank for dyn View<u8, u16> + Send + Sync { fn rank(&self) -> u8 { 3 } }
impl<A, B> Rank for dyn View<A, B> + Send + Sync { default fn rank(&self) -> u8 { 1 } }
impl<B> Rank for dyn View<u8, B> + Send + Sync { default fn rank(&self) -> u8 { 2 } }
fn main() {
    let leaf: &(dyn View<u8, u16> + Send + Sync) = &Value;
    let middle: &(dyn View<u8, u32> + Send + Sync) = &Value;
    let root: &(dyn View<u16, u32> + Send + Sync) = &Value;
    assert_eq!(leaf.rank(), 3);
    assert_eq!(middle.rank(), 2);
    assert_eq!(root.rank(), 1);
}
