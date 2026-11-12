//@ compile-fail: Copy

type Twice<T> = (T, T);

trait Repeat {
    fn repeat() -> Twice<impl Copy>;
}

struct Payload(u16);
struct Source;

impl Repeat for Source {
    fn repeat() -> Twice<Payload> {
        (Payload(127), Payload(131))
    }
}

fn main() {
    let _ = Source::repeat();
}
