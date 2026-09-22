// time's `year.narrow::<-99, 99>()`: a method call's turbofish whose first
// argument is a negative literal. The lexer makes `<-` one token, and
// upstream's path parser accepts it where `<` opens generic arguments
// (`LArrow` beside `Lt` and `Shl` in `parse_path_segment`) and splits it
// back into `<` and `-` (`break_two_token_op`).
struct Year(i32);
impl Year {
    fn narrow<const A: i32, const B: i32>(&self) -> Option<i32> {
        (A..=B).contains(&self.0).then_some(self.0)
    }
    fn make<const A: i32>() -> Year {
        Year(A)
    }
}
fn clamp<const A: i32, const B: i32>(v: i32) -> i32 {
    v.max(A).min(B)
}
fn main() {
    let year = Year(-7);
    assert_eq!(year.narrow::<-99, 99>(), Some(-7));
    assert_eq!(year.narrow::<-5, 5>(), None);
    assert_eq!(Year::make::<-3>().0, -3);
    assert_eq!(Year::<>::make::<-4>().0, -4);
    assert_eq!(clamp::<-1, 1>(-8), -1);
}
