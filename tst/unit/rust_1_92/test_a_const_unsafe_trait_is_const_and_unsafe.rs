#![feature(const_trait_impl)]

pub const unsafe trait Halve: Copy {
    fn halve(self) -> Self;
}

unsafe impl const Halve for u32 {
    fn halve(self) -> u32 {
        self / 2
    }
}

const fn quarter<T: [const] Halve>(x: T) -> T {
    x.halve().halve()
}

const TWO: u32 = quarter(8u32);

fn main() {
    assert_eq!(TWO, 2);
    assert_eq!(quarter(20u32), 5);
}
