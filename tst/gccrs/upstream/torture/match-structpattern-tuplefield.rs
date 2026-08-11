
pub struct TupStruct (i32, i32);

pub fn gccrs_main() -> i32 {
    let mut t = TupStruct (1, 1);
    let mut ret = 1;
    match t {
        TupStruct { 0: 1, 1: mut b } => { b -= 1; ret = b }
        _ => {}
    }
    ret
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
