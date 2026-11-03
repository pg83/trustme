//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Zmir-opt-level=0
//@ input: emulated_i128_target.toml
#[inline(never)]
fn select(value: i128) -> u32 {
    match value {
        -601 => 3,
        -17 => 5,
        0 => 7,
        13 => 11,
        607 => 13,
        _ => 17,
    }
}
fn main() {
    for (input, output) in [(-601, 3), (-17, 5), (0, 7), (13, 11), (607, 13), (-1, 17), (1i128 << 96, 17), (-(1i128 << 96), 17)] {
        assert_eq!(select(std::hint::black_box(input)), output);
    }
}
