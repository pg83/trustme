//@ compile-variant: -O
fn main() {
    for first in [true, false] {
        let value: &[u8] = if first { &[17u8; 2] } else { &[23u8; 3] };
        assert_eq!(value.len(), if first { 2 } else { 3 });
        assert_eq!(value[0], if first { 17 } else { 23 });
    }
}
