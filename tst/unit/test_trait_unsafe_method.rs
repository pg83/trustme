trait AddUnchecked {
    unsafe fn add_unchecked(self, other: Self) -> Self;
}

impl AddUnchecked for u32 {
    unsafe fn add_unchecked(self, other: Self) -> Self {
        self + other
    }
}

struct Operations;

impl Operations {
    const VALUE: u32 = 3;

    const unsafe fn double(value: u32) -> u32 {
        value * 2
    }

    extern "C" fn increment(value: u32) -> u32 {
        value + 1
    }
}

fn main() {
    assert_eq!(unsafe { 1u32.add_unchecked(2) }, 3);
    assert_eq!(Operations::VALUE, 3);
    assert_eq!(unsafe { Operations::double(3) }, 6);
    assert_eq!(Operations::increment(6), 7);
}
