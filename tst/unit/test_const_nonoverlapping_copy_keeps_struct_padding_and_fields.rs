#[derive(Copy, Clone, Debug, PartialEq)]
struct Record { first: u8, second: u32 }
const fn copied() -> [Record; 2] {
    let source = [Record { first: 31, second: 12345 }, Record { first: 37, second: 67890 }];
    let mut target = [Record { first: 0, second: 0 }; 2];
    unsafe { core::ptr::copy_nonoverlapping(source.as_ptr(), target.as_mut_ptr(), 2); }
    target
}
const VALUE: [Record; 2] = copied();
fn main() {
    assert_eq!(VALUE, [Record { first: 31, second: 12345 }, Record { first: 37, second: 67890 }]);
}
