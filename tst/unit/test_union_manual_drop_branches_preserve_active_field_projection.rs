//@ compile-variant: -O
use std::cell::Cell;
use std::mem::ManuallyDrop;
struct Payload<'a>(&'a Cell<u8>);
impl Drop for Payload<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
union Choice<'a> { payload: ManuallyDrop<Payload<'a>>, empty: () }
fn run(move_out: bool) {
    let count = Cell::new(0);
    let mut choice = Choice { payload: ManuallyDrop::new(Payload(&count)) };
    if move_out {
        let payload = ManuallyDrop::into_inner(unsafe { choice.payload });
        drop(payload);
    } else {
        unsafe { ManuallyDrop::drop(&mut choice.payload); }
    }
    assert_eq!(count.get(), 1);
}
fn main() { run(std::hint::black_box(true)); run(std::hint::black_box(false)); }
