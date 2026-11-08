//@ compile-fail:error
macro_rules! define {
    ($abi:expr) => {
        extern $abi fn invalid() {}
    };
}

define!(17);

fn main() {}
