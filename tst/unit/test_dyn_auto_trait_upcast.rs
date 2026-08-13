use std::sync::atomic::{AtomicBool, Ordering};

static DROPPED: AtomicBool = AtomicBool::new(false);

trait Value {
    fn value(&self) -> u32;
}

struct Dropper;

impl Value for Dropper {
    fn value(&self) -> u32 {
        0
    }
}

impl Drop for Dropper {
    fn drop(&mut self) {
        DROPPED.store(true, Ordering::SeqCst);
    }
}

impl Value for u32 {
    fn value(&self) -> u32 {
        *self
    }
}

fn keep_send(value: &(dyn Value + Send)) -> &dyn Send {
    value
}

fn main() {
    let direct: &dyn Send = &7u64;
    assert_eq!(core::mem::size_of_val(direct), core::mem::size_of::<u64>());
    assert_eq!(core::mem::align_of_val(direct), core::mem::align_of::<u64>());

    let value: &(dyn Value + Send) = &42;
    let send = keep_send(value);
    assert_eq!(core::mem::size_of_val(send), core::mem::size_of::<u32>());
    assert_eq!(core::mem::align_of_val(send), core::mem::align_of::<u32>());

    let value: Box<dyn Value + Send> = Box::new(Dropper);
    let send: Box<dyn Send> = value;
    drop(send);
    assert!(DROPPED.load(Ordering::SeqCst));
}
