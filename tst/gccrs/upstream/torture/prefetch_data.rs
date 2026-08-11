#[cfg(target_arch = "x86_64")]
fn prefetch(address: *const i32) {
    unsafe {
        std::arch::x86_64::_mm_prefetch(
            address.cast::<i8>(),
            std::arch::x86_64::_MM_HINT_T0,
        );
    }
}

#[cfg(not(target_arch = "x86_64"))]
fn prefetch(address: *const i32) {
    std::hint::black_box(address);
}

fn gccrs_main() -> i32 {
    let values = [1, 2, 3, 4];
    prefetch(values.as_ptr());
    std::hint::black_box(values.as_ptr());
    0
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
