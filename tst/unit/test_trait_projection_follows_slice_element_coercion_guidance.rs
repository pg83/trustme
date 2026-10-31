trait Segment {
    type Score;
    fn score(&self) -> Self::Score;
}
impl Segment for &[u16] {
    type Score = u32;
    fn score(&self) -> u32 { self.iter().map(|&n| n as u32).sum() }
}
fn score<S: Segment<Score = u32>>(groups: [S; 3]) -> u32 {
    groups[0].score() + 10 * groups[1].score() + 100 * groups[2].score()
}
fn main() {
    let a = [2u16, 3];
    let b = [5u16, 7, 11];
    let c = [13u16];
    let groups = [&a[..], &b, &c];
    assert_eq!(score(groups), 1535);
}
