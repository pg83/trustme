#![feature(ergonomic_clones)]

macro_rules! check_less {
    ($left:expr, $right:expr) => {
        assert!($left < $right);
    };
}

macro_rules! check_shift {
    ($left:expr) => {
        assert!($left << 1 == 2162u32);
    };
}

fn ready() -> bool {
    true
}

fn main() {
    check_less!(1081u16 as u32, 1083u32);
    check_shift!(1081u16 as u32);
    assert!(if 1081 == 0 {
        false
    } else if 1081 == 1081 {
        true
    } else {
        false
    });
    assert!(loop {
        break ready();
    });
    assert!({
        let callback = { use || 1087u16 };
        callback() == 1087
    });
}
