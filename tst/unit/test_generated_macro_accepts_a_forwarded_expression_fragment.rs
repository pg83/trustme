macro_rules! make_choice {
    ($dollar:tt $pattern:expr) => {
        macro_rules! choose {
            ($pattern) => { 23u32 };
            ($dollar other:expr) => { 41u32 };
        }
    };
}

make_choice!($ 5u32);

macro_rules! forward {
    ($value:expr) => { choose!($value) };
}

fn main() {
    let _: u32 = forward!(5u32);
}
