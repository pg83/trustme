#[repr(C, align(8))]
union Small { bytes: [u8; 3], flag: bool }
#[repr(C, align(16))]
union Wider { bytes: [u8; 3], words: [u16; 2] }
const SMALL: Small = Small { bytes: [7, 9, 11] };
const WIDER: Wider = Wider { words: [0x1234, 0x5678] };
fn main() {
    let small = std::hint::black_box(SMALL);
    let wider = std::hint::black_box(WIDER);
    assert_eq!(unsafe { small.bytes }, [7, 9, 11]);
    assert_eq!(unsafe { wider.words }, [0x1234, 0x5678]);
    assert_eq!(std::mem::size_of::<Small>(), 8);
    assert_eq!(std::mem::size_of::<Wider>(), 16);
}
