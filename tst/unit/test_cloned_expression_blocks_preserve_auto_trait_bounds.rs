#![feature(auto_traits)]
macro_rules! twice { ($body:expr) => { ($body, $body) }; }
fn main() {
    let pair = twice!({
        auto trait Flag {}
        fn require<T: Flag>(value: T) -> T { value }
        require(223u16)
    });
    assert_eq!(pair, (223, 223));
}
