// C++ gives a `volatile` aggregate no assignment operator, so a composite
// volatile access has to be copied byte by byte.
use std::ptr::{read_volatile, write_volatile};

#[derive(Debug, Eq, PartialEq)]
struct Pair(u32, u32);

#[derive(Debug, Eq, PartialEq)]
struct Wide([u64; 8]);

fn main() {
    unsafe {
        let mut pair = Pair(0, 0);
        write_volatile(&mut pair, Pair(3, 4));
        assert_eq!(read_volatile(&pair), Pair(3, 4));

        let mut wide = Wide([0; 8]);
        write_volatile(&mut wide, Wide([7; 8]));
        assert_eq!(read_volatile(&wide), Wide([7; 8]));

        let mut slice_ptr: *const [u32] = &[1u32, 2, 3][..];
        write_volatile(&mut slice_ptr, &[9u32][..]);
        assert_eq!(read_volatile(&slice_ptr).len(), 1);

        // A scalar still goes through a plain volatile dereference.
        let mut scalar: u64 = 0;
        write_volatile(&mut scalar, 1);
        assert_eq!(read_volatile(&scalar), 1);
    }
}
