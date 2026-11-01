const VALUE: u16 = 101;
mod inner {
    use self::super::VALUE;
    pub fn value() -> u16 { VALUE }
}
fn main() { assert_eq!(inner::value(), 101); }
