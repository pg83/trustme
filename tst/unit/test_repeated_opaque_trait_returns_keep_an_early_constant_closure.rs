#![feature(type_alias_impl_trait)]

type Callback = impl Fn() -> u16 + Copy;

#[define_opaque(Callback)]
const fn callback() -> Callback {
    || 119
}

const READY: Callback = callback();
type Twice<T> = (T, T);

trait Repeat {
    fn repeat() -> Twice<impl Fn() -> u16 + Copy>;
}

struct Source;

impl Repeat for Source {
    fn repeat() -> Twice<Callback> {
        (READY, READY)
    }
}

fn main() {
    let (first, second) = Source::repeat();
    assert_eq!(first(), 119);
    assert_eq!(second(), 119);
}
