macro_rules! bridge {
    ($abi:literal) => {
        #[no_mangle]
        extern $abi fn macro_abi_bridge(value: u32) -> u32 {
            value + 6
        }

        extern $abi {
            #[link_name = "macro_abi_bridge"]
            fn imported(value: u32) -> u32;
        }
    };
}

bridge!("C");

fn main() {
    assert_eq!(unsafe { imported(17) }, 23);
    assert_eq!(macro_abi_bridge(31), 37);
}
