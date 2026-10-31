trait Read<A> {
    type Output;
    fn read(&self, value: A) -> Self::Output;
}
impl<A: Into<u32>> Read<A> for u32 {
    type Output = u32;
    fn read(&self, value: A) -> u32 { *self + value.into() }
}
fn main() {
    let base = 17u32;
    let object: &dyn Read<_, Output = u32> = &base;
    assert_eq!(object.read(5u16), 22u32);
}
