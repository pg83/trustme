#![feature(generic_arg_infer)]
trait Build { type Output; fn build(self) -> Self::Output; }
struct Seed<'a, const N: usize>(&'a [u16; N]);
impl<'a, const N: usize> Build for Seed<'a, N> {
    type Output = *const [u16; N];
    fn build(self) -> Self::Output { self.0 }
}
fn pointer<const N: usize, B: Build<Output = *const [u16; N]>>(seed: B) -> *const [u16; N] { seed.build() }
fn main() {
    let values = [2u16, 3, 5];
    let result = pointer::<_, _>(Seed(&values));
    assert_eq!(unsafe { (&*result).len() }, 3);
    assert_eq!(unsafe { (*result)[2] }, 5);
}
