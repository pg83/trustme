// zerovec's `[self.sized.to_unaligned()].as_bytes()`: the element is
// `<A as AsULE>::ULE` for a type parameter `A`, a projection nothing
// normalizes further. core's inherent `[MaybeUninit<T>]::as_bytes` and
// `[AsciiChar]::as_bytes` cannot apply to it - upstream relates an inherent
// impl's self type to the normalized receiver, and a rigid projection is a
// type of its own - so the trait method is the one found.
pub unsafe trait ULE: Copy + 'static {}

pub unsafe trait VarULE: 'static {
    fn as_bytes(&self) -> &[u8] {
        unsafe { std::slice::from_raw_parts(self as *const Self as *const u8, std::mem::size_of_val(self)) }
    }
}

unsafe impl<T: ULE> VarULE for [T] {}
unsafe impl ULE for u8 {}

pub trait AsULE: Copy {
    type ULE: ULE;
    fn to_unaligned(self) -> Self::ULE;
}

impl AsULE for u8 {
    type ULE = u8;
    fn to_unaligned(self) -> u8 {
        self
    }
}

fn write<A: AsULE>(a: A, dst: &mut [u8]) {
    dst.clone_from_slice([a.to_unaligned()].as_bytes());
}

fn main() {
    let mut d = [0u8; 1];
    write(7u8, &mut d);
    assert_eq!(d, [7]);
}
