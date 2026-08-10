//@ compile-fail: cannot be evaluated at compile-time

struct CountDrop;

impl Drop for CountDrop {
    fn drop(&mut self) {}
}

const fn maybe_create(create: bool) {
    let value;
    if create {
        value = CountDrop;
    }
}

const BAD: () = maybe_create(true);

fn main() {}
