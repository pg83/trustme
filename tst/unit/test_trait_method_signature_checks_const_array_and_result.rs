//@ compile-fail: Method eval doesn't match trait:
trait Compute<const N: usize> {
    fn eval<T: Copy>(&self, values: [T; N], enabled: bool) -> u16;
}
struct Worker;
impl Compute<3> for Worker {
    fn eval<T: Copy>(&self, values: [T; 4], enabled: u8) -> u32 { let _ = (values, enabled); 0 }
}
fn main() {}
