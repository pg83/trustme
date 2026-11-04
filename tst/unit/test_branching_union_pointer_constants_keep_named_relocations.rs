//@ compile-variant: -O
#[repr(C)]
#[derive(Copy, Clone)]
union Address { pointer: *const u32, word: usize }
static FIRST: [u32; 3] = [647, 653, 659];
static SECOND: [u32; 3] = [661, 673, 677];
const LEFT: Address = Address { pointer: unsafe { FIRST.as_ptr().add(1) } };
const RIGHT: Address = Address { pointer: unsafe { SECOND.as_ptr().add(1) } };
#[inline(never)]
fn choose(flag: bool) -> Address {
    let result;
    if flag { result = LEFT; } else { result = RIGHT; }
    result
}
fn main() {
    assert_eq!(unsafe { *choose(std::hint::black_box(true)).pointer }, 653);
    assert_eq!(unsafe { *choose(std::hint::black_box(false)).pointer }, 673);
}
