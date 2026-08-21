use std::mem;

#[derive(Copy, Clone)]
struct Aligned(u64);

impl PartialEq for Aligned {
    fn eq(&self, other: &Self) -> bool {
        assert_eq!(self as *const Aligned as usize % mem::align_of::<Aligned>(), 0);
        assert_eq!(other as *const Aligned as usize % mem::align_of::<Aligned>(), 0);
        self.0 == other.0
    }
}

#[repr(packed)]
#[derive(Copy, Clone, PartialEq)]
struct Packed(Aligned);

#[repr(C)]
struct Offset(u8, Packed);

fn main() {
    let value = Offset(0, Packed(Aligned(7)));
    assert!(value.1 == value.1);
}
