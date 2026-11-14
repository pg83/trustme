#![feature(arbitrary_self_types)]

use std::ops::Deref;

struct Value(u32);
struct Handle(Value);

impl Deref for Handle {
    type Target = Value;

    fn deref(&self) -> &Value {
        &self.0
    }
}

trait HasReceiver {
    type Receiver;
}

impl HasReceiver for Value {
    type Receiver = Handle;
}

impl Value {
    fn take(self: <Self as HasReceiver>::Receiver) -> u32 {
        self.0.0
    }
}

fn main() {
    assert_eq!(Value::take(Handle(Value(17))), 17);
}
