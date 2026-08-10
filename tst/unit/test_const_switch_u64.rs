const EXP_MASK: u64 = 0x7ff0_0000_0000_0000;

const fn classify_mask(value: u64) -> u8 {
    match value {
        0 => 0,
        EXP_MASK => 1,
        _ => 2,
    }
}

const CLASS: u8 = classify_mask(EXP_MASK);

fn main() {
    assert_eq!(CLASS, 1);
}
