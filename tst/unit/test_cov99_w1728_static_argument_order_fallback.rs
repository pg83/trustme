//@ compile-fail: type annotations needed
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Slot<T>(Option<T>);
struct Pending<T>(Option<T>);
static BYTES: [u8; 2] = [3, 5];

impl Slot<u8> {
    fn view<U>(&self, bytes: &'static [u8]) -> Pending<U> {
        let _ = bytes;
        Pending(None)
    }
}

impl Slot<u16> {
    fn view<U>(&self, bytes: &'static [u8]) -> Pending<U> {
        let _ = bytes;
        Pending(None)
    }
}

impl<T> Slot<T> {
    fn set(&mut self, value: T) { self.0 = Some(value); }
}

fn main() {
    let mut slot = Slot(None);
    let pending = slot.view(&BYTES);
    slot.set(7u8);
    pending.missing_method();
}
