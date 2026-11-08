macro_rules! define {
    ($abi:expr) => {
        extern $abi fn increment(value: u16) -> u16 {
            value + 3
        }
    };
}

define!("C");

fn main() {
    let callback: extern "C" fn(u16) -> u16 = increment;
    assert_eq!(callback(19), 22);
}
