//@ native-static-aux-build: coverage_native_math.rs
//@ aux-build: coverage_native_interface.rs
fn main() {
    assert_eq!(coverage_native_interface::scale(11), 38);
    assert_eq!(coverage_native_interface::scale(u32::MAX), 2);
}
