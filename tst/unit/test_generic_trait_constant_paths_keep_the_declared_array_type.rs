trait Shape { const VALUES: [u16; 3]; }
impl Shape for u8 { const VALUES: [u16; 3] = [2, 5, 11]; }
impl Shape for u16 { const VALUES: [u16; 3] = [3, 7, 13]; }
fn checksum<T: Shape>() -> u16 { T::VALUES[0] + 10 * T::VALUES[1] + 100 * T::VALUES[2] }
fn main() { assert_eq!(checksum::<u8>(), 1152); assert_eq!(checksum::<u16>(), 1373); }
