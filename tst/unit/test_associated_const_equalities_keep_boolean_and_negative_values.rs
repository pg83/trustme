#![feature(associated_const_equality)]

trait Settings {
    const ENABLED: bool;
    const DELTA: i16;
}

struct On;
struct Off;

impl Settings for On {
    const ENABLED: bool = true;
    const DELTA: i16 = -1027;
}

impl Settings for Off {
    const ENABLED: bool = false;
    const DELTA: i16 = -1031;
}

fn enabled<T: Settings<ENABLED = true>>() -> bool {
    T::ENABLED
}

fn disabled<T: Settings<ENABLED = false>>() -> bool {
    T::ENABLED
}

fn delta<T: Settings<DELTA = -1027>>() -> i16 {
    T::DELTA
}

fn main() {
    assert!(enabled::<On>());
    assert!(!disabled::<Off>());
    assert_eq!(delta::<On>(), -1027);
}
