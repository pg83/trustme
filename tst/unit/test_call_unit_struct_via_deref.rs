use std::ops::Deref;

struct Callable;

impl Deref for Callable {
    type Target = fn() -> Self;

    fn deref(&self) -> &Self::Target {
        &((|| Callable) as fn() -> Callable)
    }
}

fn main() {
    let _value = Callable()()();
}
