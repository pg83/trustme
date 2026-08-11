#[cfg(target_arch = "x86_64")]
fn main() {
    let values = [1i32, 2, 3, 4];
    unsafe {
        std::arch::x86_64::_mm_prefetch(
            values.as_ptr().cast::<i8>(),
            std::arch::x86_64::_MM_HINT_T0,
        );
    }
}

#[cfg(not(target_arch = "x86_64"))]
fn main() {}
