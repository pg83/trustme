//@ compile-variant: -O

const VALUE: (bool, u8, i16, u128) = (!false, !0x69u8, -117i16, !1u128);

fn main() {
    assert_eq!(VALUE, (true, 0x96, -117, u128::MAX - 1));
}
