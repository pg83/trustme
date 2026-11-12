#![feature(unsized_fn_params)]

use std::cell::Cell;
use std::rc::Rc;

struct Payload(Rc<Cell<u32>>);

impl Drop for Payload {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

#[inline(never)]
fn later_argument(fail: bool) -> u16 {
    if fail {
        panic!("later argument interrupted");
    }
    17
}

#[inline(never)]
fn invoke(job: dyn FnOnce(u16) -> u16, value: u16) -> u16 {
    job(value)
}

fn main() {
    let dropped = Rc::new(Cell::new(0));
    let payload = Payload(dropped.clone());
    let job: Box<dyn FnOnce(u16) -> u16> = Box::new(move |value| {
        drop(payload);
        value + 5
    });
    let result = invoke(*job, later_argument(std::hint::black_box(false)));
    assert_eq!(result, 22);
    assert_eq!(dropped.get(), 1);
}
