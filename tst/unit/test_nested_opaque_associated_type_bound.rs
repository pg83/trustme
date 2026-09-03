// `impl Tr1<As1: Iterator<Item: Into<u32>>>` bounds an item of an item. The
// inner bound is recorded on the trait path that bounds `As1`, so it is reached
// by way of the bounds on `As1` rather than on the opaque directly: nothing in
// the declaration of `Iterator::Item` says it converts to u32, and the hidden
// type is not visible. Read off the self type alone, one level was all that was
// ever found.

trait Tr1 {
    type As1;
    fn mk(self) -> Self::As1;
}

struct S1;

impl Tr1 for S1 {
    type As1 = core::ops::Range<u8>;
    fn mk(self) -> Self::As1 { 0..10 }
}

fn def() -> impl Tr1<As1: Iterator<Item: Into<u32>>> { S1 }

fn main() {
    let mut total = 0u32;
    for item in def().mk() {
        total += item.into();
    }
    assert_eq!(total, 45);
}
