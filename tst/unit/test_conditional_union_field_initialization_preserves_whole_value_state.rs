//@ compile-fail: used binding isn't initialized
#[repr(C)]
union Data { words: [u32; 2], scalar: u32 }

fn initialized(left: bool) -> Data {
    let mut data: Data;
    if left { data.words = [53, 59]; } else { data.scalar = 61; }
    data
}

fn main() {
    assert_eq!(unsafe { initialized(std::hint::black_box(true)).words }, [53, 59]);
    assert_eq!(unsafe { initialized(std::hint::black_box(false)).scalar }, 61);
    let mut data = Data { words: [0, 0] };
    for index in 0..3 {
        if index % 2 == 0 { data.words = [67 + index, 71]; } else { data.scalar = 73; }
    }
    assert_eq!(unsafe { data.words }, [69, 71]);
}
