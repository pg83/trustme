//@ compile-fail: ValueUsage::Move for desugared Deref

use std::ops::Deref;

struct Payload(u8);
struct Wrapper(Payload);

impl Deref for Wrapper {
    type Target = Payload;

    fn deref(&self) -> &Payload {
        &self.0
    }
}

fn main() {
    let wrapper = Wrapper(Payload(7));
    let _moved: Payload = *wrapper;
}
