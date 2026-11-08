//@ aux-build: legacy_braced_register_pair.rs
extern crate legacy_braced_register_pair;

fn main() {
    let (carried, output) = unsafe {
        legacy_braced_register_pair::bump([11u8, 13u8], 0)
    };
    assert_eq!(carried, [11, 13]);
    assert_eq!(output, 7);
}
