#![feature(core_intrinsics)]

const ROWS: &[[u16; 2]] = &[[11, 13], [17, 19], [23, 29]];
const unsafe fn project_row(rows: &[[u16; 2]], index: usize) -> &[u16; 2] {
    core::intrinsics::slice_get_unchecked(rows, index)
}
const PICKED: u16 = unsafe { project_row(ROWS, 1)[1] };

fn main() {
    assert_eq!(PICKED, 19);
    unsafe {
        let row: &[u16; 2] = project_row(ROWS, 2);
        assert_eq!(*row, [23, 29]);
        let mut rows = [[31u16, 37], [41, 43], [47, 53]];
        let row: &mut [u16; 2] = core::intrinsics::slice_get_unchecked(&mut rows[..], std::hint::black_box(1usize));
        row[0] = 59;
        let index = std::hint::black_box(2usize);
        let row: *mut [u16; 2] = core::intrinsics::slice_get_unchecked(&mut rows[..] as *mut [[u16; 2]], index);
        (*row)[1] = 61;
        assert_eq!(rows, [[31, 37], [59, 43], [47, 61]]);
        let zero: &() = core::intrinsics::slice_get_unchecked(&[(); 4][..], std::hint::black_box(3usize));
        assert_eq!(*zero, ());
    }
}
