//@ compile-variant: -O
mod numbers {
    pub const VALUE: u32 = 29;
    pub fn read() -> u32 { use self::VALUE; VALUE }
}
fn main() { assert_eq!(numbers::read(), 29); }
