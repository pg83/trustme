//@ compile-fail: type annotations needed
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Slot<T>(Option<T>);
struct Pending<T>(Option<T>);
static BYTES: [u8; 2] = [3, 5];

trait FirstView<T: ?Sized + 'static> {
    fn view(&self, bytes: &'static T) -> &'static T;
}

trait SecondView<T: ?Sized + 'static> {
    fn view(&self, bytes: &'static T) -> &'static T;
}

impl FirstView<[u8]> for Slot<u8> {
    fn view(&self, bytes: &'static [u8]) -> &'static [u8] { bytes }
}

impl SecondView<[u8]> for Slot<u16> {
    fn view(&self, bytes: &'static [u8]) -> &'static [u8] { bytes }
}

impl<T> Slot<T> {
    fn set(&mut self, value: T) { self.0 = Some(value); }
}

fn main() {
    let mut slot = Slot(None);
    let slice: &'static [u8] = slot.view(&BYTES);
    slot.set(7u8);
    let _ = slice;
    let pending: Pending<_> = Pending(None);
    pending.missing_method();
}
