trait Length { type Count; fn length(&self) -> Self::Count; }
impl Length for &[u8] { type Count = usize; fn length(&self) -> usize { self.len() } }
fn lengths<T: Length<Count = usize>>(xs: [T; 2]) -> [usize; 2] { xs.map(|x| x.length()) }
fn main() {
    let a = [7u8; 2];
    let b = [9u8; 5];
    let xs = [&a[..], &b[..]];
    assert_eq!(lengths(xs), [2, 5]);
}
