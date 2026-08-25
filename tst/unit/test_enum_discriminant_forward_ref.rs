// A variant's expression may name variants declared later, directly or
// through a chain; evaluation must follow the dependencies, not the
// declaration order.

#[repr(isize)]
enum E {
    A = E::C as isize + 1,
    B = 10,
    C = E::B as isize + 5,
}

const FROM_CONST: isize = E::C as isize;

#[repr(u8)]
enum Auto {
    X = Auto::Z as u8 + 1,
    Y,
    Z = 5,
}

fn main() {
    assert_eq!(E::A as isize, 16);
    assert_eq!(E::B as isize, 10);
    assert_eq!(E::C as isize, 15);
    assert_eq!(FROM_CONST, 15);
    assert_eq!(Auto::X as u8, 6);
    assert_eq!(Auto::Y as u8, 7);
    assert_eq!(Auto::Z as u8, 5);
}
