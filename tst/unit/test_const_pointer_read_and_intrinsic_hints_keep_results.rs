#![feature(core_intrinsics)]
const DATA: [u16; 3] = [17, 23, 37];
const READ: u16 = unsafe { std::intrinsics::read_via_copy(DATA.as_ptr().add(1)) };
const HINTS: (bool, bool) = (std::intrinsics::likely(true), std::intrinsics::unlikely(false));
const ASSUMED: u16 = unsafe {
    std::intrinsics::assume(READ == 23);
    READ
};
const DOUBLED: u16 = {
    let value = 17u16;
    if std::intrinsics::is_val_statically_known(value) { value << 1 } else { value * 2 }
};
fn main() {
    assert_eq!((READ, ASSUMED, DOUBLED), (23, 23, 34));
    assert_eq!(HINTS, (true, false));
}
