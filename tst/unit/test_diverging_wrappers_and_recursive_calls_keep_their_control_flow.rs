#![allow(unconditional_recursion)]
//@ compile-flags: -O
extern "C" {
    fn abort() -> !;
}

fn terminate() -> ! {
    unsafe { abort() }
}

fn recurse() -> ! {
    recurse()
}

fn main() {
    let stop = std::hint::black_box(false);
    if stop {
        terminate();
    }
    if stop {
        recurse();
    }
    assert!(!stop);
}
