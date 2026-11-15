//@ compile-flags: -Cpanic=abort

#![feature(default_field_values)]

#[derive(Default)]
struct MixedDefaults {
    preset: u32 = 17,
    ordinary: u32,
}

fn main() {
    let value = MixedDefaults::default();
    assert_eq!(value.preset, 17);
    assert_eq!(value.ordinary, 0);
}
