use std::sync::atomic::{AtomicU32, Ordering};

fn gccrs_main() -> u32 {
    let destination = AtomicU32::new(15);
    destination.store(1, Ordering::SeqCst);
    let one = destination.load(Ordering::SeqCst);
    destination.store(2, Ordering::Release);
    let two = destination.load(Ordering::Acquire);
    destination.store(3, Ordering::Relaxed);
    let three = destination.load(Ordering::Relaxed);
    destination.store(4, Ordering::Relaxed);
    let four = destination.load(Ordering::Relaxed);
    four + three + two + one - 10
}

fn main() {
    let code = gccrs_main() as i32;
    if code != 0 {
        std::process::exit(code);
    }
}
