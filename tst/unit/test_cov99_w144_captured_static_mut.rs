//@ proc-macro-aux-build: cov99_w144_static_mut_bridge.rs

macro_rules! forward_item {
    ($item:item) => {
        cov99_w144_static_mut_bridge::reprint! { $item }
    };
}

forward_item! {
    static mut VALUE: u32 = 7;
}

fn main() {
    unsafe {
        let before = VALUE;
        assert_eq!(before, 7);
        VALUE = 19;
        let after = VALUE;
        assert_eq!(after, 19);
    }
}
