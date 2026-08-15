use std::ops::Deref;

struct Trace<T>(T);

impl<T> Deref for Trace<T> {
    type Target = T;

    fn deref(&self) -> &T {
        &self.0
    }
}

fn main() {
    let value = Trace(Trace(123));
    let result: &i32 = &value;
    assert_eq!(*result, 123);

    let _: &i32 = &&&&1;
    let _: &f64 = &&&&1.25;
}
