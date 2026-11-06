trait Width { const N: usize; }
impl Width for u16 { const N: usize = 6; }
const fn width<T: Width + Copy>(_: T) -> usize { T::N }
trait Extent { const N: usize; }
impl<const N: usize> Extent for [u8; N] { const N: usize = N; }
const LENGTH: usize = <[u8; width(0)] as Extent>::N;
fn main() {
    let value = [13u8; LENGTH];
    assert_eq!(value.len(), 6);
    assert_eq!(value[5], 13);
}
