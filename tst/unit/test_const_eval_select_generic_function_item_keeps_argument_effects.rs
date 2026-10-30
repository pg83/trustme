#![feature(core_intrinsics, const_eval_select, const_trait_impl)]
#![allow(internal_features)]

const fn compile(value: u32) -> u32 { value * 3 + 1 }
fn runtime(value: u32) -> u32 { value * 3 + 1 }

const fn record<F>(callback: F, evaluations: &mut u32) -> F {
    *evaluations += 1;
    callback
}

const fn forward<F: const FnOnce(u32) -> u32>(value: u32, callback: F) -> u32 {
    let mut evaluations = 0;
    let result = std::intrinsics::const_eval_select(
        (value,), record(callback, &mut evaluations), runtime,
    );
    result + evaluations
}

const VALUE: u32 = forward(7, compile);

fn main() {
    assert_eq!(VALUE, 23);
    assert_eq!(forward(std::hint::black_box(9), compile), 29);
}
