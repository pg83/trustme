// async-trait's tests write `let Self: Self = *self;` in an impl for a unit
// struct. Upstream resolves `Self` in a pattern to the self type's
// constructor, a constant pattern that binds and moves nothing; we took it
// for a new binding named `Self` and moved out of `*self`.
#[derive(PartialEq)]
struct Unit;

impl Unit {
    fn check(&self) -> u8 {
        let Self = *self;
        match *self {
            Self => 1,
        }
    }
}

fn main() {
    assert_eq!(Unit.check(), 1);
}
