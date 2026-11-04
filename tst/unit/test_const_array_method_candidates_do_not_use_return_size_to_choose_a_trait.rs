//@ compile-fail: error
struct Carrier<const N: usize>;
trait Left<const N: usize> { fn bytes(&self) -> [u8; N]; }
trait Right<const N: usize> { fn bytes(&self) -> [u8; N]; }
impl<const N: usize> Left<N> for Carrier<N> { fn bytes(&self) -> [u8; N] { [3; N] } }
impl<const N: usize> Right<N> for Carrier<N> { fn bytes(&self) -> [u8; N] { [5; N] } }
fn main() {
    let value = Carrier;
    let _: [u8; 9] = value.bytes();
    let _: Carrier<9> = value;
}
