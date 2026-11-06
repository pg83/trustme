use std::mem::{align_of, size_of};
use std::sync::atomic::{AtomicUsize, Ordering};

static EVENTS: AtomicUsize = AtomicUsize::new(0);

#[repr(align(32))]
struct Empty {}

impl Drop for Empty {
    fn drop(&mut self) { EVENTS.fetch_add(10, Ordering::SeqCst); }
}

#[inline(never)]
fn empty() -> Empty {
    EVENTS.fetch_add(1, Ordering::SeqCst);
    Empty {}
}

#[inline(never)]
fn repeat<const N: usize>(counter: &mut u32) -> [u32; N] {
    [{ *counter += 1; std::hint::black_box(27) }; N]
}

#[repr(C)]
struct Record { empty: Empty, value: u32 }

fn main() {
    let none = [empty(); 0];
    assert_eq!(EVENTS.load(Ordering::SeqCst), 11);
    assert_eq!(size_of::<Empty>(), 0);
    assert_eq!(align_of::<[Empty; 0]>(), 32);
    std::hint::black_box(none);
    assert_eq!(EVENTS.load(Ordering::SeqCst), 11);
    let mut counter = 0;
    assert_eq!(repeat::<0>(&mut counter), []);
    assert_eq!(repeat::<1>(&mut counter), [27]);
    assert_eq!(repeat::<2>(&mut counter), [27, 27]);
    assert_eq!(repeat::<3>(&mut counter), [27, 27, 27]);
    assert_eq!(repeat::<5>(&mut counter), [27, 27, 27, 27, 27]);
    assert_eq!(counter, 5);
    let record = Record { empty: empty(), value: 39 };
    assert_eq!(std::hint::black_box(record.value), 39);
    assert_eq!(EVENTS.load(Ordering::SeqCst), 12);
    drop(record);
    assert_eq!(EVENTS.load(Ordering::SeqCst), 22);
}
