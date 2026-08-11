
#![feature(lang_items)]
trait Valuable {
    const VALUE: i32;
}

struct Something;

macro_rules! implement {
    () => {
        const VALUE: i32 = 18;
    };
}

impl Valuable for Something {
    implement!();
}

fn gccrs_main() -> i32 {
    Something::VALUE - 18
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
