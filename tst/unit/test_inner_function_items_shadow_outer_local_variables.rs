//@ compile-variant: -O

fn main() {
    let captured = 373u16;
    {
        fn captured() -> u16 { 379 }
        assert_eq!(captured(), 379);
    }
    assert_eq!(captured, 373);
}
