fn select<'a>(callback: fn(&'a u32), input: &u32) -> &u32 {
    let _ = callback;
    input
}

fn main() {}
