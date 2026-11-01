//@ compile-flags: -Coverflow-checks=off

macro_rules! check {
    ($ty:ty) => {{
        const LEFT: $ty = 29;
        const RIGHT: $ty = 3;
        let result = (
            LEFT + RIGHT, LEFT - RIGHT, LEFT * RIGHT,
            LEFT / RIGHT, LEFT % RIGHT,
            LEFT & RIGHT, LEFT | RIGHT, LEFT ^ RIGHT,
            LEFT << 2u8, LEFT >> 2i16,
        );
        assert_eq!(result, (32, 26, 87, 9, 2, 1, 31, 30, 116, 7));
    }};
}

fn main() {
    check!(u8); check!(u16); check!(u32);
    check!(u64); check!(u128); check!(usize);
}
