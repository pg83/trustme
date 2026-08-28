#![feature(specialization)]
#![allow(incomplete_features)]

// Matching either the Self type or the trait argument alone is possible, but
// matching both impl heads would require T = &T.  Coherence must get that
// occurs-check from the solver relation rather than a structural prefilter.
trait Select<Arg> {
    fn selected() -> u8;
}

struct Wrapper<T>(T);

impl<T> Select<T> for Wrapper<T> {
    fn selected() -> u8 {
        1
    }
}

impl<T> Select<&T> for Wrapper<T> {
    fn selected() -> u8 {
        2
    }
}

fn main() {
    assert_eq!(<Wrapper<u8> as Select<u8>>::selected(), 1);
    assert_eq!(<Wrapper<u8> as Select<&u8>>::selected(), 2);
}
