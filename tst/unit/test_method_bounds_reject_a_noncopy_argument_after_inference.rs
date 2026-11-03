//@ compile-fail: No applicable methods
struct Copier;
impl Copier {
    fn retain<T: Copy>(&self, value: T) -> T { value }
}
fn main() {
    let _ = Copier.retain(vec![2u32, 3, 5]);
}
