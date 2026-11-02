//@ compile-variant: -O
mod numbers {
    pub const VALUE: u32 = 31;
    pub fn read() -> u32 {
        use self::VALUE as outer;
        { use self::VALUE as inner; assert_eq!(inner, outer); inner }
    }
}
fn main() { assert_eq!(numbers::read(), 31); }
