//@ compile-variant: -O
#[repr(C)]
#[derive(Copy, Clone)]
union Packet { number: u32, bytes: [u8; 4] }
const FIRST: Packet = Packet { number: 631 };
const SECOND: Packet = Packet { number: 641 };
#[inline(never)]
fn choose(flag: bool) -> Packet {
    let result;
    if flag { result = FIRST; } else { result = SECOND; }
    result
}
fn main() {
    assert_eq!(unsafe { choose(std::hint::black_box(true)).number }, 631);
    assert_eq!(unsafe { choose(std::hint::black_box(false)).number }, 641);
}
