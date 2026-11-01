//@ compile-flags: -Coverflow-checks=off

macro_rules! check {
    ($ty:ty) => {{
        const LEFT: $ty = -29;
        const RIGHT: $ty = 3;
        let result = (
            LEFT + RIGHT, LEFT - RIGHT, LEFT * RIGHT,
            LEFT / RIGHT, LEFT % RIGHT,
            LEFT & RIGHT, LEFT | RIGHT, LEFT ^ RIGHT,
            LEFT << 2u8, LEFT >> 2i16,
        );
        assert_eq!(result, (-26, -32, -87, -9, -2, 3, -29, -32, -116, -8));
    }};
}

fn main() {
    check!(i8); check!(i16); check!(i32);
    check!(i64); check!(i128); check!(isize);
}
