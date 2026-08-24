trait Padding<const N: usize> {}

macro_rules! padding {
    () => {{
        let mut max = 0usize;
        {
            let padding = 1usize;
            if padding > max {
                max = padding;
            }
        }
        max
    }};
}

fn define_local_impl() {
    union Local {
        byte: u8,
    }

    impl Padding<{ padding!() }> for Local {}
}

fn main() {
    define_local_impl();
}
