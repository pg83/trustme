use std::sync::atomic::{AtomicU16, Ordering};

mod values {
    pub struct Word(pub u16);
    pub const INITIAL: u16 = 397;
}

use values::{Word, INITIAL};

fn exercise(choose_left: bool) {
    let mut left = INITIAL;
    let right = 401;
    let borrowed = if choose_left { &mut left } else { &right };
    assert_eq!(*borrowed, if choose_left { INITIAL } else { 401 });
    let pointer = if choose_left { &mut left as *mut u16 } else { &right as *const u16 };
    assert_eq!(unsafe { *pointer }, if choose_left { INITIAL } else { 401 });

    let mut word = Word(403);
    let Word(ref mut value) = word;
    *value += 2;
    let Word(ref value) = word;
    assert_eq!(*value, 405);
    let Word(value) = word;
    assert_eq!(value, 405);
}

fn main() {
    exercise(true);
    exercise(false);
    let value = AtomicU16::new(409);
    assert_eq!(value.fetch_add(2, Ordering::Relaxed), 409);
    assert_eq!(value.fetch_add(2, Ordering::Acquire), 411);
    assert_eq!(value.fetch_add(2, Ordering::Release), 413);
    assert_eq!(value.fetch_add(2, Ordering::AcqRel), 415);
    assert_eq!(value.fetch_add(2, Ordering::SeqCst), 417);
    assert_eq!(value.compare_exchange(419, 421, Ordering::AcqRel, Ordering::Acquire), Ok(419));
    assert_eq!(value.compare_exchange(419, 423, Ordering::SeqCst, Ordering::Relaxed), Err(421));
    value.store(431, Ordering::Release);
    assert_eq!(value.load(Ordering::Acquire), 431);
}
