// Extracted from library/core/src/hint.rs:39
#![allow(unused)]
fn main() {
    fn prepare_inputs(divisors: &mut Vec<u32>) {
        // Note to future-self when making changes: The invariant established
        // here is NOT checked in `do_computation()`; if this changes, you HAVE
        // to change `do_computation()`.
        divisors.retain(|divisor| *divisor != 0)
    }

    /// # Safety
    /// All elements of `divisor` must be non-zero.
    unsafe fn do_computation(i: u32, divisors: &[u32]) -> u32 {
        divisors.iter().fold(i, |acc, divisor| {
            // Convince the compiler that a division by zero can't happen here
            // and a check is not needed below.
            if *divisor == 0 {
                // Safety: `divisor` can't be zero because of `prepare_inputs`,
                // but the compiler does not know about this. We *promise*
                // that we always call `prepare_inputs`.
                unsafe { std::hint::unreachable_unchecked() }
            }
            // The compiler would normally introduce a check here that prevents
            // a division by zero. However, if `divisor` was zero, the branch
            // above would reach what we explicitly marked as unreachable.
            // The compiler concludes that `divisor` can't be zero at this point
            // and removes the - now proven useless - check.
            acc / divisor
        })
    }

    let mut divisors = vec![2, 0, 4];
    prepare_inputs(&mut divisors);
    let result = unsafe {
        // Safety: prepare_inputs() guarantees that divisors is non-zero
        do_computation(100, &divisors)
    };
    assert_eq!(result, 12);
}
