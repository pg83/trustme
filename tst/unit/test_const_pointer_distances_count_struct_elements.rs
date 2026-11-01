#[repr(C)]
struct Record { first: u8, second: u32 }
const fn distances() -> (isize, usize, bool) {
    let values = [Record { first: 1, second: 11 }, Record { first: 2, second: 13 }, Record { first: 3, second: 17 }, Record { first: 4, second: 19 }];
    let start = values.as_ptr();
    let end = unsafe { start.add(3) };
    let backward = unsafe { start.offset_from(end) };
    let forward = unsafe { end.offset_from_unsigned(start) };
    let restored = start.wrapping_offset(-17).wrapping_offset(17);
    (backward, forward, unsafe { (*restored).first == 1 && (*restored).second == 11 })
}
const VALUE: (isize, usize, bool) = distances();
fn main() { assert_eq!(VALUE, (-3, 3, true)); }
