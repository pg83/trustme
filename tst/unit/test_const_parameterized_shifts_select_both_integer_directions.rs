const fn unsigned(value: u128, left: u32, right: u32) -> (u128, u128) {
    let shifted = value << left;
    (shifted, shifted >> right)
}

const fn signed(value: i128, left: u32, right: u32) -> (i128, i128) {
    let shifted = value << left;
    (shifted, shifted >> right)
}

const UNSIGNED: (u128, u128) = unsigned(0x1234_5678_9abc_def0_1357_9bdf_2468, 4, 96);
const SIGNED: (i128, i128) = signed(-(1i128 << 96) + 0xabcd, 5, 69);

fn main() {
    assert_eq!(UNSIGNED, (5907679981266292691619947069195904u128, 74565u128));
    assert_eq!(SIGNED, (-2535301200456458802993405003360i128, -4294967296i128));
}
