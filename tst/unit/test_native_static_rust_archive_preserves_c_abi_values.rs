//@ native-static-aux-build: coverage_native_math.rs
#[link(name = "coverage_native_math", kind = "static")]
extern "C" { fn coverage_native_scale(value: u32) -> u32; }
fn main() {
    unsafe {
        assert_eq!(coverage_native_scale(41), 128);
        assert_eq!(coverage_native_scale(u32::MAX), 2);
    }
}
