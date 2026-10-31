fn main() {
    macro_rules! first( () => { 17u8 } );
    macro_rules! second[ () => { 23u8 } ];
    assert_eq!(first!() + second![], 40);
}
