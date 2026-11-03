//@ compile-variant: -O
#![feature(more_qualified_paths)]

struct Record<T> { value: T }
struct Source<const FLAG: bool, const TAG: char>;
trait Project { type Output; fn build() -> Self::Output; }
impl<const FLAG: bool, const TAG: char> Project for Source<FLAG, TAG> {
    type Output = Record<(bool, char)>;
    fn build() -> Self::Output { Record { value: (FLAG, TAG) } }
}

fn main() {
    let <Source<{ !false }, 'Я'> as Project>::Output { value } = Source::<true, 'Я'>::build();
    assert_eq!(value, (true, 'Я'));
    let <Source<false, 'ж'> as Project>::Output { value } = Source::<false, 'ж'>::build();
    assert_eq!(value, (false, 'ж'));
}
