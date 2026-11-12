//@ compile-fail: Settings
#![feature(associated_const_equality)]

trait Settings {
    const ENABLED: bool;
}

struct Off;
impl Settings for Off {
    const ENABLED: bool = false;
}

fn require_enabled<T: Settings<ENABLED = true>>() {}

fn main() {
    require_enabled::<Off>();
}
