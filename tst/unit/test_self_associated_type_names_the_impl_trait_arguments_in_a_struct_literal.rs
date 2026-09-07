/* rustc `lower_assoc_path` (`Res::SelfTyAlias` of a trait impl): `Self::Output` as a
   type is the impl's own trait reference with its arguments, `<usize as Add<Span>>::Output`,
   also when it heads a struct literal inside a body.  Lowering it with the arguments
   left to infer made the literal's type `<usize as Add<_>>::Output`, which no single
   impl decides (`usize` also adds `usize` and `&usize`). */
#[derive(Debug, PartialEq)]
struct Span {
    start: usize,
    end: usize,
}

impl core::ops::Add<Span> for usize {
    type Output = Span;
    fn add(self, span: Span) -> Self::Output {
        Self::Output { start: span.start + self, end: span.end + self }
    }
}

fn main() {
    assert_eq!(2 + Span { start: 1, end: 3 }, Span { start: 3, end: 5 });
}
