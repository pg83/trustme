#![feature(core_intrinsics)]
#![allow(internal_features)]
const fn copied() -> (bool, [u16; 4], u16, isize) {
    let source = [11u16, 19, 23, 31];
    let mut output = [0u16; 4];
    unsafe {
        std::ptr::copy_nonoverlapping(source.as_ptr(), output.as_mut_ptr(), 4);
        let equal = std::intrinsics::raw_eq(&source, &output);
        let base = output.as_mut_ptr();
        std::ptr::copy(base, base.add(1), 3);
        let first = std::ptr::read(base);
        let distance = base.add(3).offset_from(base);
        (equal, output, first, distance)
    }
}
const RESULT: (bool, [u16; 4], u16, isize) = copied();
fn main() { assert_eq!(RESULT, (true, [11, 11, 19, 23], 11, 3)); }
