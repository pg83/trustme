use std::sync::atomic::{AtomicU32, Ordering};

fn gccrs_main() -> u32 {
    let source = AtomicU32::new(1);
    let one = source.load(Ordering::SeqCst);
    source.store(2, Ordering::Release);
    let two = source.load(Ordering::Acquire);
    source.store(3, Ordering::Relaxed);
    let three = source.load(Ordering::Relaxed);
    source.store(4, Ordering::Relaxed);
    let four = source.load(Ordering::Relaxed);
    four + three + two + one - 10
}

fn main() {
    let code = gccrs_main() as i32;
    if code != 0 {
        std::process::exit(code);
    }
}
