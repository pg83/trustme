// A module may lower a crate-level `deny(unsafe_code)` for the unsafe code it
// deliberately contains. This is the shape used by base64's SIMD module.
#![deny(unsafe_code)]

mod simd {
    #![allow(unsafe_code)]

    pub unsafe fn accelerated() {}

    pub struct Engine;

    impl Engine {
        pub fn call() {
            unsafe { accelerated() }
        }
    }

    pub fn call() {
        unsafe { accelerated() }
    }
}

fn main() {
    simd::call();
    simd::Engine::call();
}
