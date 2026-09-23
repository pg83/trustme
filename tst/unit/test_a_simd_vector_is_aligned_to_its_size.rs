// A `#[repr(simd)]` vector is aligned to its size rounded up to a power of
// two (upstream's `vector_align`), not to its elements: `__m128i` to 16,
// `__m256i` to 32. It was laid out like a `repr(C)` struct of its lanes,
// aligned to 8, so hashbrown's empty-table control bytes - a static aligned
// through a `[Group; 0]` field, `Group` being `__m128i` - sat 8 bytes off
// and its `ctrl as usize % Group::WIDTH == 0` check failed.
#[cfg(target_arch = "x86_64")]
use std::arch::x86_64::{__m128i, __m256i};

#[cfg(target_arch = "x86_64")]
#[repr(C)]
struct AlignedBytes {
    _align: [__m128i; 0],
    bytes: [u8; 16],
}

#[cfg(target_arch = "x86_64")]
static EMPTY: AlignedBytes = AlignedBytes { _align: [], bytes: [0xff; 16] };

#[cfg(target_arch = "x86_64")]
fn main() {
    use std::mem::{align_of, size_of};
    assert_eq!((size_of::<__m128i>(), align_of::<__m128i>()), (16, 16));
    assert_eq!((size_of::<__m256i>(), align_of::<__m256i>()), (32, 32));
    assert_eq!(align_of::<AlignedBytes>(), 16);
    assert_eq!(&EMPTY as *const AlignedBytes as usize % 16, 0);
    assert_eq!(EMPTY.bytes[15], 0xff);
}

#[cfg(not(target_arch = "x86_64"))]
fn main() {}
