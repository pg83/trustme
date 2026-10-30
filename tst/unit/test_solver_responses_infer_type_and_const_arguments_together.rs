trait Route<A, B, const N: usize, const M: usize> {
    fn route(&self, left: [A; N], right: [B; M]) -> ([A; N], [B; M]);
}
impl<A, B, const N: usize, const M: usize> Route<A, B, N, M> for () {
    fn route(&self, left: [A; N], right: [B; M]) -> ([A; N], [B; M]) { (left, right) }
}
fn main() {
    assert_eq!(().route([17u8; 3], [19u16; 5]), ([17; 3], [19; 5]));
    assert_eq!(().route([23u16; 7], [29u32; 2]), ([23; 7], [29; 2]));
}
