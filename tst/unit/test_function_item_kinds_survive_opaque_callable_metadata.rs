//@ aux-build: named_function_metadata_roots.rs
extern crate named_function_metadata_roots as roots;

fn invoke<F: Fn(u8) -> u8>(function: F, value: u8) -> u8 { function(value) }

fn main() {
    assert_eq!(invoke(roots::from_free::<3>(), 10), 13);
    assert_eq!(invoke(roots::from_inherent::<7>(), 10), 17);
    assert_eq!(invoke(roots::from_trait(), 10), 21);
}
