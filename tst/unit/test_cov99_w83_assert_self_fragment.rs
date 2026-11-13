macro_rules! panic {
    ($message:tt, $value:tt) => {{
        macro_rules! cov99_w83_saved_self {
            () => { $value };
        }
    }};
}

trait W83Probe {
    fn check(self) -> u32;
}

impl W83Probe for u32 {
    fn check(self) -> u32 {
        assert!(true, "{}", self);
        self
    }
}

fn main() {
    std::process::exit((41_u32.check() != 41) as i32);
}
