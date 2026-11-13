type Twice<T> = (T, T);

trait Make {
    fn make() -> Twice<impl Fn() -> u32>;
}

struct Maker;

impl Make for Maker {
    fn make() -> Twice<impl Fn() -> u32> {
        let closure = || 21;
        (closure, closure)
    }
}

fn main() {
    let (left, right) = Maker::make();
    assert_eq!(left() + right(), 42);

    let duplicate = |value| (value, value);
    let closure = || 17;
    let (left, right) = duplicate(closure);
    assert_eq!(left() + right(), 34);
}
