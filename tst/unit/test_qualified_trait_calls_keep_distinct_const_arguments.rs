trait First<const N: usize> { fn probe(&self) -> [u8; N]; }
trait Second<const N: usize> { fn probe(&self) -> [u8; N]; }
struct Token;
impl First<3> for Token { fn probe(&self) -> [u8; 3] { [17; 3] } }
impl Second<5> for Token { fn probe(&self) -> [u8; 5] { [19; 5] } }
fn main() {
    assert_eq!(First::<3>::probe(&Token), [17; 3]);
    assert_eq!(Second::<5>::probe(&Token), [19; 5]);
}
