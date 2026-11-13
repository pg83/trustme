#[inline(never)]
fn tick() {
    std::sync::atomic::compiler_fence(std::sync::atomic::Ordering::SeqCst);
}

#[inline(never)]
fn goto_cycle<const SPIN: bool>(input: u16) -> u16 {
    let saved = input;
    if SPIN {
        loop {}
    }
    saved
}

#[inline(never)]
fn call_cycle<const SPIN: bool>(input: u16) -> u16 {
    let saved = input;
    if SPIN {
        loop {
            tick();
        }
    }
    saved
}

fn main() {
    assert_eq!(goto_cycle::<false>(67), 67);
    assert_eq!(call_cycle::<false>(71), 71);
    std::hint::black_box(goto_cycle::<true> as fn(u16) -> u16);
    std::hint::black_box(call_cycle::<true> as fn(u16) -> u16);
}
