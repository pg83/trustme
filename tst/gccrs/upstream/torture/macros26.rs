
macro_rules! count_tt {
    ($t:tt) => { 1 };
    ($t:tt $($ts:tt)*) => { 1 + count_tt!($($ts)*) };
}

fn gccrs_main() -> i32 {
    let count = count_tt!(1 2 let a = 15) + count_tt!(1 2 (let a = 15));
    //                    ^ ^ ^^^ ^ ^ ^^              ^ ^ ^^^^^^^^^^^^
    //                    6 token-trees               3 token-trees

    count - 9
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
