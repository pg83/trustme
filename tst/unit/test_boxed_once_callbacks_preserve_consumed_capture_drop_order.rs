use std::cell::Cell;
use std::rc::Rc;

struct Stamp(Rc<Cell<u32>>, u8);

impl Drop for Stamp {
    fn drop(&mut self) {
        self.0.set(self.0.get() * 10 + self.1 as u32);
    }
}

#[inline(never)]
fn invoke(job: Box<dyn FnOnce(u16) -> u16>, value: u16) -> u16 {
    job(value)
}

fn main() {
    let events = Rc::new(Cell::new(0));
    let capture = Stamp(events.clone(), 7);
    let during_call = events.clone();
    let job = move |value| {
        let _guard = Stamp(during_call, 3);
        let offset = capture.1 as u16;
        drop(capture);
        value + offset
    };
    assert_eq!(events.get(), 0);
    assert_eq!(invoke(Box::new(job), 17), 24);
    assert_eq!(events.get(), 73);

    let capture = Stamp(events.clone(), 9);
    let unused: Box<dyn FnOnce()> = Box::new(move || drop(capture));
    drop(unused);
    assert_eq!(events.get(), 739);
}
