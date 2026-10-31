#[derive(Clone, Copy)]
struct Words([u64; 3]);
impl std::ops::Not for Words {
    type Output = Self;
    fn not(self) -> Self { Words([!self.0[0], !self.0[1], !self.0[2]]) }
}
#[derive(Clone, Copy)]
struct Signed([i128; 3]);
impl std::ops::Neg for Signed {
    type Output = Self;
    fn neg(self) -> Self { Signed([-self.0[0], -self.0[1], -self.0[2]]) }
}
const WORDS: Words = Words([0, 17, u64::MAX]);
const SIGNED: Signed = Signed([17, -23, i128::MAX]);
fn main() {
    assert_eq!((!WORDS).0, [u64::MAX, !17, 0]);
    assert_eq!((-SIGNED).0, [-17, 23, -i128::MAX]);
    assert_eq!((!WORDS.0[1], -SIGNED.0[1]), (!17u64, 23));
}
